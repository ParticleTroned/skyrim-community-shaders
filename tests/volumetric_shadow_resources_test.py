"""Fault-inject the production VSM resource setup and draw-time binding paths.

The fixture supplies COM/D3D owners; allocation, rollback, retry and publication
logic are extracted from the production sources. It does not execute GPU work.
"""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from extract_adaptive_balance_toggle import between, function


ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
using UINT = unsigned;
using HRESULT = long;
constexpr HRESULT S_OK = 0, E_FAIL = -1;
constexpr UINT D3D11_FILTER_MIN_MAG_MIP_LINEAR = 1, D3D11_TEXTURE_ADDRESS_CLAMP = 2;
constexpr UINT DXGI_FORMAT_R16G16_UNORM = 3, D3D11_USAGE_IMMUTABLE = 4;
constexpr UINT D3D11_BIND_SHADER_RESOURCE = 5, D3D11_SRV_DIMENSION_TEXTURE2D = 6;
constexpr float D3D11_FLOAT32_MAX = std::numeric_limits<float>::max();
struct D3D11_SAMPLER_DESC {
    UINT Filter{}, AddressU{}, AddressV{}, AddressW{}, MaxAnisotropy{};
    float MinLOD{}, MaxLOD{};
};
struct D3D11_TEXTURE2D_DESC {
    UINT Width{}, Height{}, MipLevels{}, ArraySize{}, Format{};
    struct { UINT Count{}, Quality{}; } SampleDesc;
    UINT Usage{}, BindFlags{};
};
struct D3D11_SUBRESOURCE_DATA { const void* pSysMem{}; UINT SysMemPitch{}; };
struct D3D11_SHADER_RESOURCE_VIEW_DESC {
    UINT Format{}, ViewDimension{};
    struct { UINT MostDetailedMip{}, MipLevels{}; } Texture2D;
};
struct Resource {
    static inline unsigned live = 0;
    unsigned references = 1;
    std::string name;
    Resource() { ++live; }
    virtual ~Resource() { --live; }
    void AddRef() { ++references; }
    void Release() { if (--references == 0) delete this; }
};
struct ID3D11Texture2D : Resource {};
struct ID3D11SamplerState : Resource {};
struct ID3D11UnorderedAccessView : Resource {};
struct ID3D11ShaderResourceView : Resource {
    ID3D11Texture2D* texture;
    explicit ID3D11ShaderResourceView(ID3D11Texture2D* owner) : texture(owner) { texture->AddRef(); }
    ~ID3D11ShaderResourceView() override { texture->Release(); }
};
namespace winrt {
template<class T> class com_ptr {
    T* value = nullptr;
public:
    com_ptr() = default;
    com_ptr(const com_ptr&) = delete;
    com_ptr& operator=(const com_ptr&) = delete;
    ~com_ptr() { if (value) value->Release(); }
    T* get() const { return value; }
    T** put() { assert(!value); return &value; }
    T* detach() { return std::exchange(value, nullptr); }
};
}
namespace DX {
struct com_exception : std::runtime_error { com_exception() : std::runtime_error("injected allocation failure") {} };
void ThrowIfFailed(HRESULT result) { if (result != S_OK) throw com_exception(); }
}
namespace logger {
unsigned errors = 0;
template<class... Args> void error(const char*, Args&&...) { ++errors; }
}
namespace Util {
template<class T> void SetResourceName(T* resource, const char* name) {
    assert(resource); resource->name = name;
}
}
struct ID3D11Device {
    unsigned calls = 0;
    int failAt = -1;
    bool Fail() { return static_cast<int>(calls++) == failAt; }
    HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** out) {
        assert(desc->AddressU == D3D11_TEXTURE_ADDRESS_CLAMP);
        if (Fail()) return E_FAIL;
        *out = new ID3D11SamplerState;
        return S_OK;
    }
    HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* initial,
                            ID3D11Texture2D** out) {
        assert(desc->Width == 2 && desc->Height == 2 && desc->MipLevels == 2);
        assert(desc->Format == DXGI_FORMAT_R16G16_UNORM && desc->Usage == D3D11_USAGE_IMMUTABLE);
        assert(initial[0].SysMemPitch == 8 && initial[1].SysMemPitch == 4);
        for (unsigned mip = 0; mip < 2; ++mip) {
            const auto* values = static_cast<const uint16_t*>(initial[mip].pSysMem);
            for (unsigned i = 0; i < (mip ? 2u : 8u); ++i)
                assert(values[i] == std::numeric_limits<uint16_t>::max());
        }
        if (Fail()) return E_FAIL;
        *out = new ID3D11Texture2D;
        return S_OK;
    }
    HRESULT CreateShaderResourceView(ID3D11Texture2D* texture, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,
                                     ID3D11ShaderResourceView** out) {
        assert(texture && !texture->name.empty());
        assert(desc->Texture2D.MipLevels == 2 && desc->Texture2D.MostDetailedMip == 0);
        if (Fail()) return E_FAIL;
        *out = new ID3D11ShaderResourceView(texture);
        return S_OK;
    }
};
struct ID3D11DeviceContext {
    ID3D11ShaderResourceView* published = nullptr;
    void PSSetShaderResources(UINT slot, UINT count, ID3D11ShaderResourceView* const* values) {
        assert(slot == 18 && count == 1); published = values[0];
    }
};
namespace RE { struct BSShader {
    enum class Type { None, Grass, Sky, BloodSplatter, Utility, DistantTree, Lighting };
}; }
struct State { uint32_t frameCount = 0; bool directional = true; bool HasDirectionalShadows() const { return directional; } };
namespace globals {
State stateStorage;
State* state = &stateStorage;
namespace d3d { ID3D11Device* device = nullptr; }
}
'''

CHECKS = r'''
void CheckEmpty(const VolumetricShadows& effect) {
    assert(!effect.HasSharedShadowMapResources());
    assert(!effect.linearSampler && !effect.fallbackShadowTexture && !effect.fallbackShadowSRV);
    assert(Resource::live == 0);
}
int main() {
    VolumetricShadows noDevice;
    noDevice.SetupResources();
    CheckEmpty(noDevice);
    assert(!noDevice.fixedResourceAllocationFailed);

    for (int failure = 0; failure < 3; ++failure) {
        ID3D11Device device;
        device.failAt = failure;
        globals::d3d::device = &device;
        VolumetricShadows effect;
        ID3D11DeviceContext context;
        const auto errorsBefore = logger::errors;
        effect.SetShaderResources(&context, RE::BSShader::Type::Lighting);
        assert(effect.fixedResourceAllocationFailed);
        CheckEmpty(effect);
        assert(logger::errors == errorsBefore + 1);
        const auto failedCalls = device.calls;
        for (unsigned draw = 0; draw < 128; ++draw)
            effect.SetShaderResources(&context, RE::BSShader::Type::Lighting);
        assert(device.calls == failedCalls && logger::errors == errorsBefore + 1);
        assert(!context.published);

        device.failAt = -1;
        effect.ClearShaderCache();
        effect.SetShaderResources(&context, RE::BSShader::Type::Lighting);
        assert(!effect.fixedResourceAllocationFailed && effect.HasSharedShadowMapResources());
        assert(context.published == effect.fallbackShadowSRV);
        assert(Resource::live == 3 && device.calls == failedCalls + 3);
        assert(!effect.fallbackShadowTexture->name.empty() && !effect.fallbackShadowSRV->name.empty());
        assert(!effect.linearSampler->name.empty());
        const auto successfulCalls = device.calls;
        for (unsigned draw = 0; draw < 128; ++draw) effect.SetupResources();
        assert(device.calls == successfulCalls);
        effect.ReleaseResources();
        CheckEmpty(effect);
    }

    ID3D11Device first, second;
    globals::d3d::device = &first;
    VolumetricShadows effect;
    effect.SetupResources();
    assert(first.calls == 3 && effect.HasSharedShadowMapResources());
    globals::d3d::device = &second;
    effect.SetupResources();
    assert(second.calls == 3 && Resource::live == 3 && effect.HasSharedShadowMapResources());
    // An incomplete pair must be repaired; the old texture is released only after publication.
    effect.fallbackShadowSRV->Release(); effect.fallbackShadowSRV = nullptr;
    assert(Resource::live == 2);
    effect.SetupResources();
    assert(second.calls == 5 && Resource::live == 3 && effect.HasSharedShadowMapResources());
    effect.ReleaseResources();
    CheckEmpty(effect);

    // Device recreation also recovers a latched failure without a shader-cache reset.
    first = {}; second = {}; first.failAt = 2;
    globals::d3d::device = &first;
    VolumetricShadows failed;
    failed.SetupResources();
    CheckEmpty(failed);
    globals::d3d::device = &second;
    failed.SetupResources();
    assert(!failed.fixedResourceAllocationFailed && failed.HasSharedShadowMapResources());
    assert(second.calls == 3 && Resource::live == 3);
    failed.ReleaseResources();
    CheckEmpty(failed);
}
'''


def main():
    source = (ROOT / "src/Features/VolumetricShadows.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/Features/VolumetricShadows.h").read_text(encoding="utf-8")
    signatures = [
        "void VolumetricShadows::SetupResources(",
        "void VolumetricShadows::EnsureFallbackShadowTexture(",
        "void VolumetricShadows::ClearShaderCache(",
        "void VolumetricShadows::ReleaseShadowCopyTextures(",
        "void VolumetricShadows::ReleaseTextures(",
        "void VolumetricShadows::ReleaseSampler(",
        "void VolumetricShadows::ReleaseResources(",
        "void VolumetricShadows::SetSharedShadowMapSRV(",
        "void VolumetricShadows::SetShaderResources(",
    ]
    production = function(source, "\tbool HasActiveDirectionalShadows(")
    production += "template<class T>\n" + function(source, "\tvoid SafeRelease(")
    production += "struct VolumetricShadows {\n"
    production += between(header, "\tstruct Settings", "\tvirtual std::pair")
    production += between(header, "\twinrt::com_ptr<ID3D11ShaderResourceView> shadowView;", "\nprivate:")
    production += "ID3D11Device* resourceDevice = nullptr;\n"
    production += "void CompileComputeShaders() {} void ReleaseComputeShaders() {}\n"
    production += function(header, "\tbool HasSharedShadowMapResources(")
    bodies = []
    for signature in signatures:
        body = function(source, signature)
        declaration = body[:body.index("{")].replace("VolumetricShadows::", "").strip()
        production += declaration + ";\n"
        bodies.append(body)
    production += "};\n" + "\n".join(bodies)

    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required for the VSM resource fixture")
    with tempfile.TemporaryDirectory(prefix="se-vsm-resources-") as temporary:
        output = Path(temporary)
        source_path = output / "fixture.cpp"
        source_path.write_text(PRELUDE + production + CHECKS, encoding="utf-8")
        executable = output / ("fixture.exe" if os.name == "nt" else "fixture")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", str(source_path), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source_path), "-o", str(executable)]
        subprocess.run(command, cwd=output, check=True)
        subprocess.run([str(executable)], check=True)
    print("Production VSM draw-time allocation failure, rollback, retry, device recovery and lit-fallback checks passed")


if __name__ == "__main__":
    main()
