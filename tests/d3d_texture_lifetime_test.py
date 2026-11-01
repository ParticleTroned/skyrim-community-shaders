#!/usr/bin/env python3
"""Compile production COM-sentinel/lifecycle code with deterministic COM objects.

Native scene access, Windows stack capture and SEH are outside this portable
fixture; it does not claim to replace an SE/AE in-game ownership exercise.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "src/Diagnostics/D3DTextureLifetimeTracker.cpp"


def region(text: str, first: str, last: str) -> str:
    start = text.index(first)
    end = text.index(last, start)
    return text[start:end]


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    constants = region(source, "\t\tconstexpr std::size_t kCapturedStackDepth", "\t\tnlohmann::json DescribeAddress")
    estimator = region(source, "\t\tuint32_t BytesPerBlock", "\t\t// Skyrim publishes a source texture")
    sentinel = region(source, "\t\tvoid RecordDestroyed(", "\n\t}\n\n\tbool Start()")
    lifecycle = region(source, "\tbool Start()", "\tvoid OnSourceTextureCreated(")
    bridge = (REPO / "src/Diagnostics/D3DTextureLifetimeDevBenchBridge.cpp").read_text(encoding="utf-8")
    descriptor = json.loads(re.search(r'R"json\((.*?)\)json"', bridge, re.S).group(1))
    assert descriptor["inputSchema"]["properties"]["action"]["enum"] == ["start", "status", "checkpoint", "stop", "reset"]
    handler = region(bridge, "\tjson Handle(", "\n\tvoid ToolHandler(")
    status = region(source, "\tnlohmann::json BuildStatus()", "\n}\n\n#endif")
    formats = sorted(set(re.findall(r"DXGI_FORMAT_\w+", estimator)) | {"DXGI_FORMAT_UNKNOWN"})
    prelude = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <nlohmann/json.hpp>
#include <set>
#include <string_view>
#include <utility>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define STDMETHODCALLTYPE
using HRESULT = long;
using ULONG = unsigned long;
using DWORD = unsigned long;
inline constexpr HRESULT S_OK=0, E_FAIL=-1, E_POINTER=-2, E_NOINTERFACE=-3;
inline bool FAILED(HRESULT result) { return result < 0; }
struct GUID {
    std::uint32_t a; std::uint16_t b,c; std::array<std::uint8_t,8> d;
    bool operator==(const GUID&) const = default;
};
using REFIID = const GUID&;
constexpr GUID kIUnknown{};
#define __uuidof(Type) kIUnknown
struct IUnknown {
    virtual HRESULT QueryInterface(REFIID, void**) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};
unsigned short CaptureStackBackTrace(DWORD, DWORD, void**, DWORD*) { return 0; }
'''
    prelude += "enum DXGI_FORMAT { " + ",".join(formats) + " };\n"
    prelude += r'''
struct D3D11_TEXTURE2D_DESC {
    std::uint32_t Width=4, Height=2, MipLevels=1, ArraySize=1;
    DXGI_FORMAT Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    struct { std::uint32_t Count=1, Quality=0; } SampleDesc;
    std::uint32_t Usage=0, BindFlags=0, CPUAccessFlags=0, MiscFlags=0;
};
struct ID3D11Texture2D {
    IUnknown* sentinel=nullptr;
    unsigned attempts=0;
    bool fail=false, throws=false;
    HRESULT SetPrivateDataInterface(const GUID&, IUnknown* value) {
        ++attempts;
        if (throws) throw std::runtime_error("test attachment error");
        if (fail) return E_FAIL;
        if (value) value->AddRef();
        if (sentinel) sentinel->Release();
        sentinel=value;
        return S_OK;
    }
    ~ID3D11Texture2D() { if (sentinel) sentinel->Release(); }
};
'''
    production = "namespace Diagnostics::D3DTextureLifetimeTracker {\nnamespace {\n" + constants + estimator + sentinel + "\n}\n" + lifecycle + "\n}\n"
    # The command handler is production code. Only JSON envelopes, native
    # traversal and queued scheduling are stand-ins in this fixture.
    commands = r'''
using json = nlohmann::json;
namespace Diagnostics::D3DTextureLifetimeTracker {
NiSourceTextureOwnerSnapshot nativeSnapshot;
unsigned nativeScans=0;
bool nativeThrows=false, nativeEndsCapture=false;
NiSourceTextureOwnerSnapshot CollectNiSourceTextureOwners() {
    ++nativeScans;
    if(nativeThrows) throw std::runtime_error("native fixture failure");
    if(nativeEndsCapture) Stop(GetState().sessionID.load());
    return nativeSnapshot;
}
json DescribeAddress(std::uint64_t address) { return {{"address",address},{"module","fixture"},{"offset",address}}; }
''' + status + r'''
}
namespace Tracker = Diagnostics::D3DTextureLifetimeTracker;
struct EnvelopeFixture {
    json MakeEnvelope(const json&, bool ok) { return {{"ok",ok}}; }
    json MakeError(const json&, std::string_view code, std::string_view message,
                   std::string_view phase="validation", bool retryable=false, std::string_view field={}) {
        return {{"ok",false},{"error",{{"code",code},{"message",message},{"phase",phase},{"retryable",retryable},{"field",field}}}};
    }
};
EnvelopeFixture& Foundation() { static EnvelopeFixture fixture; return fixture; }
namespace SKSE { int* GetTaskInterface() { static int interface; return &interface; } }
namespace CSX::Api {
bool failDispatch=false;
template<class Task> json RunDevBenchMainThreadTask(int*, Task run) {
    if(failDispatch) return {{"error","unavailable"}};
    return run();
}
}
''' + handler + r'''
void TestStatus() {
    namespace T = Diagnostics::D3DTextureLifetimeTracker;
    auto status=T::BuildStatus();
    assert(status.at("supported").get<bool>() && status.at("statisticsFrozen").get<bool>());
    assert(status.at("niSourceTextureOwnerScanSkipped").get<bool>() && T::nativeScans==0);
    assert(T::Start());
    const auto session=T::GetState().sessionID.load();
    {
        ID3D11Texture2D texture;
        const auto address=reinterpret_cast<std::uint64_t>(&texture);
        T::OnTextureCreated(&texture, D3D11_TEXTURE2D_DESC{}, 700);
        T::GetState().nativeOrigins.emplace(address,T::NiSourceTextureOwner{100,2,"creation.dds"});
        T::nativeSnapshot.owners[address].push_back({100,3,"reachable.dds"});
        T::nativeSnapshot.faceGenOwners.emplace(address,T::FaceGenTintOwnerObservation{100,200,300,400,500,600});
        T::nativeSnapshot.observedAtMs=123;
        assert(T::Checkpoint(session));
        status=T::BuildStatus();
        assert(status.at("outstandingCount")==1 && status.at("niSourceTextureMatchedCount")==1);
        assert(status.at("nativeSourceCreationObservations").at(0).at("name")=="creation.dds");
        const auto face=status.at("nonCurrentNiSourceTextureMatches").at(0).at("faceGenOwnerObservation");
        assert(face.at("headPart").is_null() && face.at("observedAtMs")==123 && face.at("npcFormID")==500);
        T::nativeThrows=true;
        status=T::BuildStatus();
        assert(status.at("niSourceTextureOwnerScanReason")=="scene_inventory_failed");
        assert(status.at("outstandingCount")==1 && status.at("niSourceTextureMatchedCount")==0);
        T::nativeThrows=false;
        T::nativeEndsCapture=true;
        status=T::BuildStatus();
        assert(status.at("niSourceTextureOwnerScanReason")=="capture_changed");
        assert(status.at("statisticsFrozen").get<bool>() && status.at("niSourceTextureMatchedCount")==0);
        T::nativeEndsCapture=false;
        const auto scanCount=T::nativeScans;
        status=T::BuildStatus();
        assert(status.at("statisticsFrozen").get<bool>() && T::nativeScans==scanCount);
        assert(status.at("niSourceTextureOwnerScanReason")=="capture_inactive_or_empty");
        assert(T::Reset(session));
    }
    T::nativeSnapshot={};
}
void TestCommands() {
    auto invalid=[](json request, std::string_view field) {
        const auto result=Handle(request);
        assert(!result.at("ok").get<bool>());
        assert(result.at("error").at("code")=="invalid_request");
        assert(result.at("error").at("field")==field);
        assert(!result.at("error").at("retryable").get<bool>());
    };
    invalid({{"action","unknown"}},"action");
    invalid({{"action","status"},{"extra",1}},"extra");
    invalid({{"action","status"},{"contractMinor",0.0}},"contractMinor");
    invalid({{"action","status"},{"contractMinor",1}},"contractMinor");
    for (const auto& action : {"checkpoint","stop","reset"}) {
        invalid({{"action",action}},"expectedSessionID");
        for(const auto& value : std::vector<json>{0,-1,1.5,"1",nullptr})
            invalid({{"action",action},{"expectedSessionID",value}},"expectedSessionID");
    }
    auto result=Handle({{"action","start"}});
    assert(result.at("ok").get<bool>());
    const auto session=result.at("result").at("capture").at("sessionID").get<std::uint64_t>();
    assert(!Handle({{"action","start"}}).at("ok").get<bool>());
    assert(!Handle({{"action","checkpoint"},{"expectedSessionID",session+1}}).at("ok").get<bool>());
    assert(Handle({{"action","checkpoint"},{"expectedSessionID",session}}).at("ok").get<bool>());
    assert(!Handle({{"action","reset"},{"expectedSessionID",session}}).at("ok").get<bool>());
    assert(!Handle({{"action","stop"},{"expectedSessionID",session+1}}).at("ok").get<bool>());
    assert(Tracker::IsActive());
    assert(Handle({{"action","stop"},{"expectedSessionID",session}}).at("ok").get<bool>());
    assert(Handle({{"action","reset"},{"expectedSessionID",session}}).at("ok").get<bool>());
    assert(Handle({{"action","status"},{"contractMinor",0}}).at("ok").get<bool>());
    CSX::Api::failDispatch=true;
    result=Handle({{"action","start"}});
    assert(result.at("error").at("code")=="main_thread_dispatch_failed");
    assert(!Tracker::IsActive());
}
'''
    harness = r'''
int main() {
    namespace T = Diagnostics::D3DTextureLifetimeTracker;
    auto& state=T::GetState();
    D3D11_TEXTURE2D_DESC desc;
    { ID3D11Texture2D inactive; T::OnTextureCreated(&inactive,desc,0); assert(!inactive.attempts); }
    assert(T::Start());
    assert(!T::Start());
    auto session=state.sessionID.load();
    assert(!T::Reset(session));
    assert(!T::Checkpoint(session+1));
    assert(T::Checkpoint(session));
    assert(state.cohort==1);
    {
        ID3D11Texture2D texture;
        T::OnTextureCreated(&texture,desc,123);
        assert(texture.sentinel && state.createdCount==1 && state.liveTextures.size()==1);
        state.nativeOrigins.emplace(reinterpret_cast<std::uint64_t>(&texture), T::NiSourceTextureOwner{7,2,"historical"});
        T::OnTextureCreated(&texture,desc,123);
        assert(texture.attempts==1 && state.createdCount==1 && state.droppedTextureRecords==1);
    }
    assert(state.destroyedCount==1 && state.liveTextures.empty() && state.nativeOrigins.empty());
    {
        ID3D11Texture2D failed; failed.fail=true;
        T::OnTextureCreated(&failed,desc,124);
        assert(state.attachFailures==1 && state.createdCount==1 && state.liveTextures.empty());
    }
    {
        ID3D11Texture2D failed; failed.throws=true;
        T::OnTextureCreated(&failed,desc,125);
        assert(state.recordingFailures==1 && state.createdCount==1 && state.liveTextures.empty());
    }
    assert(!T::Stop(session+1));
    assert(T::IsActive());
    auto old=std::make_unique<ID3D11Texture2D>();
    T::OnTextureCreated(old.get(),desc,126);
    assert(T::Stop(session));
    assert(!T::Reset(session+1));
    assert(T::Reset(session));
    assert(T::Start());
    auto nextSession=state.sessionID.load();
    assert(nextSession!=session);
    {
        ID3D11Texture2D fresh;
        T::OnTextureCreated(&fresh,desc,127);
        old.reset();
        assert(state.createdCount==1 && state.destroyedCount==0 && state.liveTextures.size()==1);
    }
    assert(state.destroyedCount==1);
    assert(T::Stop(nextSession));
    assert(T::Reset(nextSession));
    desc.Format=DXGI_FORMAT_BC1_UNORM; desc.Width=5; desc.Height=7; desc.MipLevels=3; desc.ArraySize=2; desc.SampleDesc.Count=2;
    assert(T::EstimateBytes(desc)==192);
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.Width=4; desc.Height=2; desc.MipLevels=0; desc.ArraySize=2; desc.SampleDesc.Count=1;
    assert(T::EstimateBytes(desc)==88);
    desc.Format=DXGI_FORMAT_UNKNOWN; assert(T::EstimateBytes(desc)==0);
    desc=D3D11_TEXTURE2D_DESC{};
    assert(T::Start());
    session=state.sessionID.load();
    {
        std::vector<std::unique_ptr<ID3D11Texture2D>> textures;
        for (std::size_t i=0;i<T::kMaxTrackedTextureGroups+1;++i) {
            textures.push_back(std::make_unique<ID3D11Texture2D>());
            T::OnTextureCreated(textures.back().get(),desc,i+1);
        }
        assert(state.groups.size()==T::kMaxTrackedTextureGroups);
        assert(state.createdCount==T::kMaxTrackedTextureGroups && state.droppedTextureRecords==1);
    }
    assert(state.createdCount==state.destroyedCount && state.liveTextures.empty());
    assert(T::Stop(session)); assert(T::Reset(session)); assert(T::Start());
    session=state.sessionID.load();
    {
        std::vector<std::unique_ptr<ID3D11Texture2D>> textures;
        for (std::size_t i=0;i<T::kMaxTrackedLiveTextures+1;++i) {
            textures.push_back(std::make_unique<ID3D11Texture2D>());
            T::OnTextureCreated(textures.back().get(),desc,1);
        }
        assert(state.groups.size()==1 && state.liveTextures.size()==T::kMaxTrackedLiveTextures);
        assert(state.droppedTextureRecords==1);
    }
    assert(state.createdCount==state.destroyedCount && state.nativeOrigins.empty());
    assert(T::Stop(session)); assert(T::Reset(session));
    TestStatus();
    TestCommands();
}
'''
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required for the production lifetime fixture")
    json_include = Path(os.environ.get("CSX_JSON_INCLUDE", "/usr/include"))
    if not (json_include / "nlohmann/json.hpp").is_file():
        raise SystemExit("CSX_JSON_INCLUDE must contain nlohmann/json.hpp")
    with tempfile.TemporaryDirectory(prefix="se-texture-lifetime-test-") as temporary:
        root = Path(temporary)
        translation_unit = root / "lifetime.cpp"
        translation_unit.write_text(prelude + production + commands + harness, encoding="utf-8")
        executable = root / ("lifetime.exe" if os.name == "nt" else "lifetime")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", f"/I{json_include}", str(translation_unit), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror", "-pthread", f"-I{json_include}", str(translation_unit), "-o", str(executable)]
        subprocess.run(command, check=True, cwd=root)
        subprocess.run([str(executable)], check=True)
    print("Production D3D COM sentinel, capture ownership, rollback, capacity, status coverage and DevBench command checks passed")


if __name__ == "__main__":
    main()
