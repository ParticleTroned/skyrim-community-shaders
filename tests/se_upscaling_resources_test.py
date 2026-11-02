"""Compile the production SE descriptor ownership, shader selection and dispatch code.

D3D calls are deterministic stand-ins; real shader execution is covered separately
by the Windows WARP depth-encoder test.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from extract_adaptive_balance_toggle import function


ROOT = Path(__file__).resolve().parents[1]


def header(relative: str) -> str:
    source = (ROOT / relative).read_text(encoding="utf-8")
    return re.sub(r"^#(?:include|pragma once).*\n", "", source, flags=re.MULTILINE)


PRELUDE = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>
using uint = unsigned;
using UINT = unsigned;
#define IID_PPV_ARGS(value) value
#define FAILED(value) ((value) < 0)
struct D3D11_TEXTURE2D_DESC { uint32_t Width{}, Height{}, MipLevels{}; };
struct ID3D11Texture2D;
struct ID3D11Resource {
    virtual int QueryInterface(ID3D11Texture2D**) = 0;
};
struct ID3D11Texture2D : ID3D11Resource {
    D3D11_TEXTURE2D_DESC desc{13,11,1};
    unsigned refs=1, queries=0, descriptions=0;
    bool fail=false, empty=false;
    int QueryInterface(ID3D11Texture2D** result) override {
        ++queries; *result=nullptr;
        if(fail) return -1;
        if(!empty) { *result=this; AddRef(); }
        return 0;
    }
    void AddRef() { ++refs; }
    void Release() { assert(refs); --refs; }
    void GetDesc(D3D11_TEXTURE2D_DESC* result) { ++descriptions; *result=desc; }
};
namespace winrt {
template<class T> class com_ptr {
    T* value=nullptr;
public:
    com_ptr()=default;
    ~com_ptr() { if(value) value->Release(); }
    com_ptr(com_ptr&& other) noexcept: value(std::exchange(other.value,nullptr)) {}
    com_ptr& operator=(com_ptr&& other) noexcept {
        if(this!=&other) { if(value) value->Release(); value=std::exchange(other.value,nullptr); }
        return *this;
    }
    com_ptr(const com_ptr& other): value(other.value) { if(value) value->AddRef(); }
    com_ptr& operator=(const com_ptr& other) {
        if(this!=&other) { com_ptr copy(other); *this=std::move(copy); } return *this;
    }
    T* get() const { return value; }
    T* operator->() const { return value; }
    explicit operator bool() const { return value!=nullptr; }
    T** put() { assert(!value); return &value; }
};
}
enum { D3D11_UAV_DIMENSION_TEXTURE2D=1, D3D11_UAV_DIMENSION_TEXTURE2DARRAY=2 };
struct D3D11_UNORDERED_ACCESS_VIEW_DESC {
    unsigned ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    struct { unsigned MipSlice=0; } Texture2D;
};
struct ID3D11ShaderResourceView {};
struct ID3D11ComputeShader {};
struct ID3D11Buffer {
    unsigned refs=1;
    void AddRef() { ++refs; }
    void Release() { assert(refs); --refs; }
};
struct ID3D11UnorderedAccessView {
    D3D11_TEXTURE2D_DESC texture{13,11,1};
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    unsigned descriptions=0, textureDescriptions=0;
    void GetDesc(D3D11_UNORDERED_ACCESS_VIEW_DESC* out) { ++descriptions; *out=desc; }
};
struct ConstantBuffer {
    ID3D11Buffer buffer;
    unsigned updates=0;
    template<class T> void Update(const T&) { ++updates; }
    ID3D11Buffer* CB() { return &buffer; }
};
constexpr UINT D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT=4096;
struct ID3D11DeviceContext1;
struct ID3D11DeviceContext {
    struct Binding {
        ID3D11Buffer* buffer=nullptr;
        UINT first=0,count=D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
        bool operator==(const Binding&) const = default;
    };
    std::array<Binding,2> frameBuffers{};
    unsigned refs=1;
    bool supportsWindows=false;
    int QueryInterface(ID3D11DeviceContext1**);
    void AddRef() { ++refs; }
    void Release() { assert(refs); --refs; }
    unsigned dispatches=0, x=0, y=0;
    ID3D11ComputeShader* shader=nullptr;
    std::array<ID3D11ShaderResourceView*,2> sources{};
    ID3D11UnorderedAccessView* target=nullptr;
    void CSSetShader(ID3D11ComputeShader* value, void*, unsigned) { shader=value; }
    void GetBinding(unsigned index,ID3D11Buffer** out,UINT* first=nullptr,UINT* count=nullptr) {
        const auto& binding=frameBuffers[index]; *out=binding.buffer;
        if(*out) (*out)->AddRef();
        if(first) *first=binding.first;
        if(count) *count=binding.count;
    }
    void CSGetConstantBuffers(UINT slot,UINT count,ID3D11Buffer** out) { assert(slot==12 && count==1); GetBinding(0,out); }
    void PSGetConstantBuffers(UINT slot,UINT count,ID3D11Buffer** out) { assert(slot==12 && count==1); GetBinding(1,out); }
    void CSSetConstantBuffers(UINT slot,UINT count,ID3D11Buffer* const* in) {
        if(slot==12) { assert(count==1); frameBuffers[0]={*in}; }
    }
    void PSSetConstantBuffers(UINT slot,UINT count,ID3D11Buffer* const* in) {
        assert(slot==12 && count==1); frameBuffers[1]={*in};
    }
    void CSSetShaderResources(unsigned first, unsigned count, ID3D11ShaderResourceView* const* views) {
        for(unsigned i=0;i<count;++i) sources.at(first+i)=views[i];
    }
    void CSSetUnorderedAccessViews(unsigned, unsigned, ID3D11UnorderedAccessView* const* views, const unsigned*) { target=*views; }
    void Dispatch(unsigned width, unsigned height, unsigned depth) {
        assert(shader && sources[0] && target && depth==1); ++dispatches; x=width; y=height;
    }
};
struct ID3D11DeviceContext1 : ID3D11DeviceContext {
    void CSGetConstantBuffers1(UINT slot,UINT count,ID3D11Buffer** out,UINT* first,UINT* length) { assert(slot==12 && count==1); GetBinding(0,out,first,length); }
    void PSGetConstantBuffers1(UINT slot,UINT count,ID3D11Buffer** out,UINT* first,UINT* length) { assert(slot==12 && count==1); GetBinding(1,out,first,length); }
    void CSSetConstantBuffers1(UINT slot,UINT count,ID3D11Buffer* const* in,const UINT* first,const UINT* length) { assert(slot==12 && count==1); frameBuffers[0]={*in,*first,*length}; }
    void PSSetConstantBuffers1(UINT slot,UINT count,ID3D11Buffer* const* in,const UINT* first,const UINT* length) { assert(slot==12 && count==1); frameBuffers[1]={*in,*first,*length}; }
};
int ID3D11DeviceContext::QueryInterface(ID3D11DeviceContext1** out) {
    *out=nullptr;
    if(!supportsWindows) return -1;
    AddRef(); *out=static_cast<ID3D11DeviceContext1*>(this); return 0;
}
namespace globals::d3d { ID3D11DeviceContext* context=nullptr; }
namespace Util {
    bool GetTexture2DDesc(ID3D11UnorderedAccessView* view, D3D11_TEXTURE2D_DESC& out) {
        if(!view) return false;
        ++view->textureDescriptions; out=view->texture; return true;
    }
}
#define CS_GPU_PASS_SELECT(condition, first, second) (void)(condition)
'''


SELECTION = r'''
struct ShaderCacheFixture {
    ID3D11ComputeShader shader;
    unsigned fixedCalls=0, vectorCalls=0;
    std::vector<std::string> lastDefines;
    ID3D11ComputeShader* Get(const wchar_t*, std::initializer_list<std::pair<const char*,const char*>> macros, const char*) {
        ++fixedCalls; lastDefines.clear(); for(const auto& [key,value]:macros) { (void)value; lastDefines.emplace_back(key); } return &shader;
    }
    ID3D11ComputeShader* Get(const wchar_t*, const std::vector<std::pair<const char*,const char*>>&, const char*) {
        ++vectorCalls; return &shader;
    }
};
struct Upscaling {
    enum class UpscaleMethod { kNONE, kTAA, kFSR, kDLSS };
    UpscaleMethod method=UpscaleMethod::kTAA;
    bool fsrDepthTexture=false;
    std::array<ShaderCacheFixture,4> encodeTexturesCS;
    ShaderCacheFixture encodeTexturesCSDepthOutput;
    UpscaleMethod GetUpscaleMethod() const { return method; }
    ID3D11ComputeShader* GetEncodeTexturesCS();
};
'''


CHECKS = r'''
void CheckDescriptions() {
    ID3D11Texture2D original, replacement, motion, invalid;
    invalid.fail=true;
    {
        UpscalingTextures::TextureSourceDescription description;
        assert(!description.Capture(nullptr));
        assert(description.Capture(&original) && description.Matches(&original));
        assert(original.refs==2 && original.queries==1 && original.descriptions==1);
        original.Release(); // The operation-local snapshot now owns the last reference.
        assert(description.Capture(&original) && original.refs==1 && original.queries==1);
        assert(!description.Matches(&replacement)); // Equal dimensions never imply identity.
        assert(description.Capture(&replacement));
        assert(original.refs==0 && replacement.refs==2);
        assert(!description.Capture(&invalid) && replacement.refs==1);
        assert(!description.Matches(&replacement) && description.Desc().Width==0);
        invalid.fail=false; invalid.empty=true;
        assert(!description.Capture(&invalid) && description.Desc().Height==0);
    }
    {
        UpscalingTextures::SourceDescriptions inputs;
        assert(inputs.Capture(&replacement,&motion));
        const auto queries=replacement.queries+motion.queries;
        assert(inputs.Capture(&replacement,&motion));
        assert(replacement.queries+motion.queries==queries);
        assert(!inputs.Capture(&replacement,nullptr));
        assert(!inputs.Matches(&replacement,&motion));
        assert(inputs.color.Desc().Width==0 && inputs.motion.Desc().Width==0);
        assert(replacement.refs==1 && motion.refs==1);
    }
}
void CheckShaderSelection() {
    Upscaling owner;
    for(unsigned index=0;index<4;++index) {
        owner.method=static_cast<Upscaling::UpscaleMethod>(index);
        for(unsigned request=0;request<3;++request)
            assert(owner.GetEncodeTexturesCS()==&owner.encodeTexturesCS[index].shader);
        const auto& cache=owner.encodeTexturesCS[index];
        assert(cache.fixedCalls==3 && cache.vectorCalls==0);
        assert(cache.lastDefines==(index==2?std::vector<std::string>{"FSR"}:index==3?std::vector<std::string>{"DLSS"}:std::vector<std::string>{}));
    }
    owner.method=Upscaling::UpscaleMethod::kFSR; owner.fsrDepthTexture=true;
    assert(owner.GetEncodeTexturesCS()==&owner.encodeTexturesCSDepthOutput.shader);
    assert(owner.encodeTexturesCSDepthOutput.vectorCalls==0);
    assert((owner.encodeTexturesCSDepthOutput.lastDefines==std::vector<std::string>{"FSR","DEPTH_OUTPUT"}));
}
void CheckFixedDispatch() {
    using namespace UpscalingSharpener;
    ID3D11DeviceContext context;
    globals::d3d::context=&context;
    ID3D11ShaderResourceView source, motion;
    ID3D11UnorderedAccessView output;
    ID3D11ComputeShader shader;
    ConstantBuffer constants;
    const std::array<MotionSharpening::Region,1> regions{MotionSharpening::Region{{0,0,13,11},{},{}}};
    const FixedConfig config{};
    for(auto pass: {Pass::RCAS,Pass::LumaSharpen}) {
        const auto before=output.textureDescriptions;
        assert(DispatchFixedComputePass(&shader,&constants,config,&source,&output,pass,regions));
        assert(output.textureDescriptions==before+1); // Dispatch must reuse validated dimensions.
        assert(context.x==2 && context.y==2 && !context.shader && !context.target && !context.sources[0]);
    }
    const auto submitted=context.dispatches;
    const std::array<MotionSharpening::Region,1> partial{MotionSharpening::Region{{1,0,12,11},{},{}}};
    assert(!DispatchFixedComputePass(&shader,&constants,config,&source,&output,Pass::RCAS,partial));
    assert(!DispatchFixedComputePass(&shader,&constants,config,&source,nullptr,Pass::RCAS,regions));
    output.desc.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    assert(!DispatchFixedComputePass(&shader,&constants,config,&source,&output,Pass::RCAS,regions));
    output.desc.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    output.desc.Texture2D.MipSlice=1;
    assert(!DispatchFixedComputePass(&shader,&constants,config,&source,&output,Pass::RCAS,regions));
    assert(context.dispatches==submitted && constants.updates==submitted);
    output.texture={26,22,2};
    assert(DispatchFixedComputePass(&shader,&constants,config,&source,&output,Pass::RCAS,regions));
    assert(context.x==2 && context.y==2); // The output UAV mip controls dimensions.
    assert(DispatchComputePass(&shader,&constants,config,&source,&output,Pass::RCAS,13,11,&motion));
    assert(!context.sources[0] && !context.sources[1] && !context.target);
    globals::d3d::context=nullptr;
    assert(!DispatchFixedComputePass(&shader,&constants,config,&source,&output,Pass::RCAS,regions));
}
void CheckSceneFrameBufferBindings() {
    using namespace UpscalingDepth;
    for(bool windowSupport: {false,true}) for(unsigned stage=0;stage<2;++stage) for(unsigned state=0;state<3;++state) {
        if(state==2 && !windowSupport) continue;
        ID3D11DeviceContext1 context;
        context.supportsWindows=windowSupport;
        ID3D11Buffer previous,replacement,other;
        context.frameBuffers[stage]={state ? &previous : nullptr,state==2 ? 16u : 0u,state==2 ? 32u : 4096u};
        context.frameBuffers[1-stage]={&other,16,64};
        const auto before=context.frameBuffers;
        try {
            const ScopedFrameBuffer binding(&context,&replacement,stage==0 ? Stage::Compute : Stage::Pixel);
            assert((context.frameBuffers[stage]==ID3D11DeviceContext::Binding{&replacement}));
            assert(context.frameBuffers[1-stage]==before[1-stage]);
            throw 1; // Early exits restore the same pointer, window and owning references.
        } catch(int) {}
        assert(context.frameBuffers==before && context.refs==1 && previous.refs==1);
    }
    const ScopedFrameBuffer absent(nullptr,nullptr,Stage::Pixel);
}
int main() { CheckDescriptions(); CheckShaderSelection(); CheckFixedDispatch(); CheckSceneFrameBufferBindings(); }
'''


def main() -> None:
    production = "\n".join(header(path) for path in (
        "src/Features/Upscaling/TextureSourceDescriptions.h",
        "src/Features/Upscaling/MotionSharpeningPolicy.h",
        "src/Features/Upscaling/SharpenerBindings.h",
        "src/Features/Upscaling/SharpenerDispatch.h",
        "src/Features/Upscaling/SceneDepthBindings.h",
    ))
    source = (ROOT / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")
    selection = function(source, "ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()")
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required")
    with tempfile.TemporaryDirectory(prefix="se-upscaling-resources-") as directory:
        root = Path(directory)
        unit = root / "resources.cpp"
        unit.write_text(PRELUDE + production + SELECTION + selection + CHECKS, encoding="utf-8")
        executable = root / ("resources.exe" if os.name == "nt" else "resources")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", str(unit), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror", str(unit), "-o", str(executable)]
        subprocess.run(command, check=True, cwd=root)
        subprocess.run([str(executable)], check=True, cwd=root)
    print("Production source ownership, fixed shader macros, complete mono output, dispatch reuse and scene-buffer binding checks passed")


if __name__ == "__main__":
    main()
