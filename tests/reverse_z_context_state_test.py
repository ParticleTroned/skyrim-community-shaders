"""Exercise production Reverse Z hooks across native context-state swaps."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from grass_collision_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class ReverseZContextStateTests(unittest.TestCase):
    def test_native_swaps_restore_logical_state_and_ownership(self):
        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        guard = (ROOT / "src/Utils/DeviceContextState.h").read_text(encoding="utf-8")
        driver = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
using UINT = unsigned;
using HRESULT = int;
using ULONG = unsigned long;
constexpr HRESULT S_OK = 0, E_FAIL = -1, E_OUTOFMEMORY = -2, DXGI_ERROR_NOT_FOUND = -3, E_NOINTERFACE = -4;
#define __uuidof(...) 1
bool FAILED(HRESULT value) { return value < 0; }
constexpr unsigned D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE = 16;
struct GUID { unsigned a; unsigned short b, c; unsigned char d[8]; };
struct IUnknown {
    unsigned references = 1;
    virtual ~IUnknown() = default;
    ULONG AddRef() { return ++references; }
    ULONG Release() { const auto left = --references; if (!left) delete this; return left; }
};
namespace winrt {
template<class T> class com_ptr {
    T* value = nullptr;
public:
    com_ptr() = default;
    com_ptr(const com_ptr& other) { copy_from(other.value); }
    com_ptr(com_ptr&& other) noexcept : value(other.detach()) {}
    ~com_ptr() { if (value) value->Release(); }
    com_ptr& operator=(const com_ptr& other) { copy_from(other.value); return *this; }
    com_ptr& operator=(std::nullptr_t) { attach(nullptr); return *this; }
    T* get() const { return value; }
    T* operator->() const { return value; }
    explicit operator bool() const { return value != nullptr; }
    T** put() { attach(nullptr); return &value; }
    void** put_void() { return reinterpret_cast<void**>(put()); }
    void copy_from(T* next) { if (next) next->AddRef(); attach(next); }
    void attach(T* next) { if (value) value->Release(); value = next; }
    T* detach() { return std::exchange(value, nullptr); }
};
}
unsigned metadataAllocations = 0, liveMetadata = 0;
bool allocationFails = false;
namespace Microsoft::WRL {
enum { ClassicCom };
template<int> struct RuntimeClassFlags {};
template<class, class Base> struct RuntimeClass : Base {
    RuntimeClass() { ++liveMetadata; }
    ~RuntimeClass() override { --liveMetadata; }
};
template<class T> struct ComPtr {
    winrt::com_ptr<T> value;
    explicit operator bool() const { return static_cast<bool>(value); }
    T* Detach() { return value.detach(); }
};
template<class T> ComPtr<T> Make() {
    ComPtr<T> result;
    if (!allocationFails) { result.value.attach(new T); ++metadataAllocations; }
    return result;
}
}
namespace REL { template<class Function> using Relocation = Function*; }
struct D3D11_VIEWPORT { float TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; };
struct ID3D11DepthStencilState : IUnknown {};
struct ID3D11RasterizerState : IUnknown {};
struct ID3D11DepthStencilView : IUnknown { bool reversed = false; };
struct ID3D11RenderTargetView : IUnknown {};
struct NativePipeline {
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11DepthStencilView* target = nullptr;
    UINT stencil = 0, count = 0;
    std::array<D3D11_VIEWPORT, 16> viewports{};
};
struct ID3DDeviceContextState : IUnknown {
    NativePipeline pipeline;
    winrt::com_ptr<IUnknown> metadata;
    bool saveFails = false, readFails = false;
    HRESULT GetPrivateData(const GUID&, UINT* size, void* output) {
        if (readFails) return E_FAIL;
        if (!metadata) return DXGI_ERROR_NOT_FOUND;
        *size = sizeof(IUnknown*);
        metadata->AddRef();
        *static_cast<IUnknown**>(output) = metadata.get();
        return S_OK;
    }
    HRESULT SetPrivateDataInterface(const GUID&, IUnknown* value) {
        if (saveFails) return E_FAIL;
        metadata.copy_from(value);
        return S_OK;
    }
};
struct ID3D11DeviceContext;
using ID3D11DeviceContext1 = ID3D11DeviceContext;
void HookSwap(ID3D11DeviceContext*, ID3DDeviceContextState*, ID3DDeviceContextState**);
void HookClear(ID3D11DeviceContext*);
void NativeDepthGet(ID3D11DeviceContext*, ID3D11DepthStencilState**, UINT*);
void NativeRasterGet(ID3D11DeviceContext*, ID3D11RasterizerState**);
void NativeViewportGet(ID3D11DeviceContext*, UINT*, D3D11_VIEWPORT*);
struct ID3D11DeviceContext : IUnknown {
    NativePipeline pipeline;
    winrt::com_ptr<ID3DDeviceContextState> active;
    unsigned sets = 0, swaps = 0;
    HRESULT queryResult = S_OK;
    ID3D11DeviceContext* queryAlias = nullptr;
    ID3D11DeviceContext() { active.attach(new ID3DDeviceContextState); }
    void SwapDeviceContextState(ID3DDeviceContextState* state, ID3DDeviceContextState** previous) { HookSwap(this, state, previous); }
    void ClearState() { HookClear(this); }
    HRESULT QueryInterface(int, void** output) {
        *output = nullptr;
        if (FAILED(queryResult)) return queryResult;
        auto* value = queryAlias ? queryAlias : this;
        value->AddRef(); *output = value; return S_OK;
    }
    void OMGetDepthStencilState(ID3D11DepthStencilState** state, UINT* reference) { NativeDepthGet(this, state, reference); }
    void RSGetState(ID3D11RasterizerState** state) { NativeRasterGet(this, state); }
    void RSGetViewports(UINT* count, D3D11_VIEWPORT* values) { NativeViewportGet(this, count, values); }
    void OMGetRenderTargets(UINT, ID3D11RenderTargetView**, ID3D11DepthStencilView** depth) {
        *depth = pipeline.target;
        if (*depth) (*depth)->AddRef();
    }
};
void NativeSwap(ID3D11DeviceContext* context, ID3DDeviceContextState* state, ID3DDeviceContextState** previous) {
    if (!state) return;
    ++context->swaps;
    context->active->pipeline = context->pipeline;
    if (previous) { *previous = context->active.get(); (*previous)->AddRef(); }
    context->active.copy_from(state);
    context->pipeline = state->pipeline;
}
void NativeClear(ID3D11DeviceContext* context) { context->pipeline = {}; }
void NativeDepthSet(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT reference) {
    context->pipeline.depth = state; context->pipeline.stencil = reference; ++context->sets;
}
void NativeRasterSet(ID3D11DeviceContext* context, ID3D11RasterizerState* state) { context->pipeline.raster = state; ++context->sets; }
void NativeViewportSet(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* values) {
    context->pipeline.count = count;
    std::copy_n(values, count, context->pipeline.viewports.begin()); ++context->sets;
}
void NativeDepthGet(ID3D11DeviceContext* context, ID3D11DepthStencilState** state, UINT* reference) {
    if (state) { *state = context->pipeline.depth; if (*state) (*state)->AddRef(); }
    if (reference) *reference = context->pipeline.stencil;
}
void NativeRasterGet(ID3D11DeviceContext* context, ID3D11RasterizerState** state) {
    if (state) { *state = context->pipeline.raster; if (*state) (*state)->AddRef(); }
}
void NativeViewportGet(ID3D11DeviceContext* context, UINT* count, D3D11_VIEWPORT* values) {
    if (values) std::copy_n(context->pipeline.viewports.begin(), std::min(*count, context->pipeline.count), values);
    *count = context->pipeline.count;
}
struct ReverseZ {
    struct ContextStateHook;
    unsigned rejects = 0;
    bool runtimeHooksInstalled = false, requestedThisBoot = true, activationRejectedThisBoot = false;
    std::string failure;
    void LatchBootState() {}
    void InstallRuntimeHooks();
    bool IsReverseDepthView(ID3D11DepthStencilView* view) const { return view && view->reversed; }
    ID3D11DepthStencilState* GetReversedState(ID3D11DepthStencilState*);
    ID3D11RasterizerState* GetReversedRasterizerState(ID3D11RasterizerState*);
    void RejectRendering(const char*);
};
namespace globals {
namespace d3d { ID3D11DeviceContext* context = nullptr; }
namespace features { ReverseZ reverseZ; }
}
std::vector<unsigned> installedSlots;
namespace stl {
template<unsigned Slot, class, class Interface> void detour_vfunc(Interface*) { installedSlots.push_back(Slot); }
}
namespace logger {
template<class... Args> void info(const char*, Args&&...) {}
template<class... Args> void error(const char*, Args&&...) {}
}
struct ID3D11DeviceContext_OMSetRenderTargets {};
struct ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews {};
struct ID3D11DeviceContext_ClearDepthStencilView {};
TRACKED_GLOBALS
std::atomic_bool g_renderingRejected{false};
winrt::com_ptr<ID3D11DepthStencilState> g_rejectedDepthState;
ID3D11RenderTargetView* g_lastClearedCubeFace = nullptr;
std::unordered_map<ID3D11DepthStencilState*, ID3D11DepthStencilState*> depthMapping;
std::unordered_map<ID3D11RasterizerState*, ID3D11RasterizerState*> rasterMapping;
ID3D11DepthStencilState* ReverseZ::GetReversedState(ID3D11DepthStencilState* state) {
    return g_renderingRejected ? g_rejectedDepthState.get() : depthMapping.at(state);
}
ID3D11RasterizerState* ReverseZ::GetReversedRasterizerState(ID3D11RasterizerState* state) {
    return g_renderingRejected ? nullptr : rasterMapping.at(state);
}
void ReverseZ::RejectRendering(const char*) { ++rejects; g_renderingRejected = true; }
bool ShouldFlip();
void ReapplyMappedStates(ID3D11DeviceContext*);
PRODUCTION
void HookSwap(ID3D11DeviceContext* context, ID3DDeviceContextState* state, ID3DDeviceContextState** previous) {
    ReverseZ::ContextStateHook::thunk(context, state, previous);
}
void HookClear(ID3D11DeviceContext* context) { ID3D11DeviceContext_ClearState::thunk(context); }
namespace Util { PRODUCTION_GUARD; }
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool Close(float a, float b) { return std::abs(a-b) < 1e-6f; }
void Bind(ID3D11DeviceContext& context, ID3D11DepthStencilView* target, ID3D11DepthStencilState* depth,
    ID3D11RasterizerState* raster, UINT stencil, D3D11_VIEWPORT viewport) {
    context.pipeline.target = target; UpdateBoundDepthTarget(&context, target);
    ID3D11DeviceContext_OMSetDepthStencilState::thunk(&context, depth, stencil);
    ID3D11DeviceContext_RSSetState::thunk(&context, raster);
    ID3D11DeviceContext_RSSetViewports::thunk(&context, 1, &viewport);
}
void Check(ID3D11DeviceContext& context, ID3D11DepthStencilState* logicalDepth,
    ID3D11RasterizerState* logicalRaster, UINT stencil, D3D11_VIEWPORT viewport, bool reversed) {
    Require(g_requestedState.get() == logicalDepth && g_requestedRasterState.get() == logicalRaster, "Logical state was not restored");
    Require(g_requestedStencilRef == stencil && g_requestedViewportCount == 1, "Stencil or viewport count was not restored");
    Require(context.pipeline.depth == (reversed ? depthMapping.at(logicalDepth) : logicalDepth), "Wrong physical depth state after swap");
    Require(context.pipeline.raster == (reversed ? rasterMapping.at(logicalRaster) : logicalRaster), "Wrong physical raster state after swap");
    Require(context.pipeline.stencil == stencil, "Wrong physical stencil reference");
    const auto& physical = context.pipeline.viewports[0];
    Require(Close(physical.MinDepth, reversed ? 1-viewport.MaxDepth : viewport.MinDepth) &&
        Close(physical.MaxDepth, reversed ? 1-viewport.MinDepth : viewport.MaxDepth), "Wrong physical viewport range");
    winrt::com_ptr<ID3D11DepthStencilState> readDepth;
    winrt::com_ptr<ID3D11RasterizerState> readRaster;
    ID3D11DeviceContext_OMGetDepthStencilState::thunk(&context, readDepth.put(), nullptr);
    ID3D11DeviceContext_RSGetState::thunk(&context, readRaster.put());
    Require(readDepth.get() == logicalDepth && readRaster.get() == logicalRaster, "Native readback leaked mapped states");
    UINT count = 1; D3D11_VIEWPORT read{};
    ID3D11DeviceContext_RSGetViewports::thunk(&context, &count, &read);
    Require(Close(read.MinDepth, viewport.MinDepth) && Close(read.MaxDepth, viewport.MaxDepth), "Native readback leaked mapped viewport");
}
int main() {
    try {
        ID3D11DepthStencilState depthA, depthB, reversedA, reversedB, defaultDepth, rejected;
        ID3D11RasterizerState rasterA, rasterB, reversedRasterA, reversedRasterB;
        ID3D11DepthStencilView world, standard; world.reversed = true;
        depthMapping = {{nullptr, &defaultDepth}, {&depthA, &reversedA}, {&depthB, &reversedB}};
        rasterMapping = {{nullptr, nullptr}, {&rasterA, &reversedRasterA}, {&rasterB, &reversedRasterB}};
        g_rejectedDepthState.copy_from(&rejected);
        ID3D11DeviceContext_OMSetDepthStencilState::func = NativeDepthSet;
        ID3D11DeviceContext_RSSetState::func = NativeRasterSet;
        ID3D11DeviceContext_RSSetViewports::func = NativeViewportSet;
        ID3D11DeviceContext_OMGetDepthStencilState::func = NativeDepthGet;
        ID3D11DeviceContext_RSGetState::func = NativeRasterGet;
        ID3D11DeviceContext_RSGetViewports::func = NativeViewportGet;
        ID3D11DeviceContext_ClearState::func = NativeClear;
        ReverseZ::ContextStateHook::func = NativeSwap;
        const D3D11_VIEWPORT viewportA{3, 4, 1920, 1080, 0.2f, 0.7f};
        const D3D11_VIEWPORT viewportB{0, 0, 900, 700, 0.1f, 0.8f};
        {
            ID3D11DeviceContext context, foreign;
            globals::d3d::context = &context;
            ReverseZ baseOnly;
            context.queryResult = E_NOINTERFACE;
            baseOnly.InstallRuntimeHooks();
            Require(baseOnly.runtimeHooksInstalled && !baseOnly.activationRejectedThisBoot && installedSlots.size() == 10 &&
                std::find(installedSlots.begin(), installedSlots.end(), 131) == installedSlots.end(), "Base-only context attempted an extended-slot hook");
            installedSlots.clear();
            context.queryResult = S_OK; context.queryAlias = &foreign;
            ReverseZ distinct;
            distinct.InstallRuntimeHooks();
            Require(!distinct.runtimeHooksInstalled && distinct.activationRejectedThisBoot && installedSlots.empty(), "Distinct context alias was not rejected before installation");
            context.queryAlias = nullptr; context.queryResult = E_FAIL;
            ReverseZ failedQuery;
            failedQuery.InstallRuntimeHooks();
            Require(!failedQuery.runtimeHooksInstalled && failedQuery.activationRejectedThisBoot && installedSlots.empty(), "Failed interface query did not preserve standard rendering");
            context.queryResult = S_OK;
            globals::features::reverseZ.InstallRuntimeHooks();
            Require(installedSlots.size() == 11 && installedSlots.back() == 131, "Native context-state hook was not installed");
            globals::features::reverseZ.InstallRuntimeHooks();
            Require(installedSlots.size() == 11, "Native context-state hook was installed twice");
            g_projectionReversed = true;
            Bind(context, &world, &depthA, &rasterA, 19, viewportA);
            Check(context, &depthA, &rasterA, 19, viewportA, true);
            winrt::com_ptr<ID3DDeviceContextState> original = context.active, isolated, nested;
            isolated.attach(new ID3DDeviceContextState); nested.attach(new ID3DDeviceContextState);
            for (int iteration=0; iteration<3; ++iteration) {
                const auto before = metadataAllocations;
                {
                    Util::ScopedDeviceContextState outer(&context, isolated.get());
                    context.ClearState();
                    Bind(context, &standard, &depthB, &rasterB, 7, viewportB);
                    try {
                        Util::ScopedDeviceContextState inner(&context, nested.get());
                        context.ClearState();
                        throw std::runtime_error("abort isolated pass");
                    } catch (const std::runtime_error&) {}
                    Check(context, &depthB, &rasterB, 7, viewportB, false);
                    outer.Restore(); outer.Restore();
                }
                Check(context, &depthA, &rasterA, 19, viewportA, true);
                if (iteration) Require(metadataAllocations == before, "Warm swaps allocate metadata repeatedly");
            }
            auto sets = context.sets;
            winrt::com_ptr<ID3DDeviceContextState> same;
            context.SwapDeviceContextState(original.get(), same.put());
            Require(same.get() == original.get() && context.sets == sets, "Self swap changed or redundantly rebound native state");
            Check(context, &depthA, &rasterA, 19, viewportA, true);
            auto* unchanged = original.get();
            const auto swaps = context.swaps;
            context.SwapDeviceContextState(nullptr, &unchanged);
            Require(unchanged == original.get() && context.swaps == swaps, "Null state was not a no-op");
            context.SwapDeviceContextState(isolated.get(), nullptr);
            Check(context, &depthB, &rasterB, 7, viewportB, false);
            context.SwapDeviceContextState(original.get(), nullptr);
            Check(context, &depthA, &rasterA, 19, viewportA, true);
            {
                Util::ScopedDeviceContextState scope(&foreign, nested.get());
                foreign.ClearState();
            }
            Check(context, &depthA, &rasterA, 19, viewportA, true);
            {
                Util::ScopedDeviceContextState outer(&context, isolated.get());
                context.ClearState();
                g_projectionReversed = false;
            }
            Check(context, &depthA, &rasterA, 19, viewportA, false);
            g_projectionReversed = true; ReapplyMappedStates(&context);
            Check(context, &depthA, &rasterA, 19, viewportA, true);
            isolated->readFails = true;
            context.SwapDeviceContextState(isolated.get(), nullptr);
            Require(g_renderingRejected, "Metadata read failure was ignored");
            isolated->readFails = false;
            context.SwapDeviceContextState(original.get(), nullptr);
            Require(context.pipeline.depth == &rejected, "Failed tracking did not withhold reversed scene rendering");
            context.ClearState();
        }
        Require(liveMetadata == 0, "Retired context states leaked their metadata");
        for (bool allocationFailure : {false, true}) {
            g_renderingRejected = false;
            ID3D11DeviceContext context;
            globals::d3d::context = &context;
            Bind(context, &world, &depthA, &rasterA, 19, viewportA);
            winrt::com_ptr<ID3DDeviceContextState> previous, isolated;
            isolated.attach(new ID3DDeviceContextState);
            context.active->saveFails = !allocationFailure;
            allocationFails = allocationFailure;
            context.SwapDeviceContextState(isolated.get(), previous.put());
            allocationFails = false;
            Require(g_renderingRejected && previous, "Metadata retention failure was ignored");
            previous->saveFails = false;
            context.SwapDeviceContextState(previous.get(), nullptr);
            Require(context.pipeline.depth == &rejected, "Retention failure exposed reversed scene drawing");
            context.ClearState();
        }
        Require(liveMetadata == 0, "Failure cleanup leaked state metadata");
        globals::d3d::context = nullptr; g_rejectedDepthState = nullptr;
        std::cout << "Reverse Z context swaps passed: nested, self, null, exception, camera transition and failure ownership\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''
        start = source.index("\twinrt::com_ptr<ID3D11DepthStencilState> g_requestedState;")
        end = source.index("\n\tbool ShouldFlip()", start)
        driver = driver.replace("TRACKED_GLOBALS", source[start:end])
        names = [
            "bool ShouldFlip()", "D3D11_VIEWPORT MirrorViewportDepth(",
            "struct ID3D11DeviceContext_OMSetDepthStencilState", "struct ID3D11DeviceContext_RSSetState",
            "struct ID3D11DeviceContext_RSSetViewports", "void ReapplyMappedStates(ID3D11DeviceContext* This)\n\t{",
            "void UpdateBoundDepthTarget(", "struct ID3D11DeviceContext_OMGetDepthStencilState",
            "struct ID3D11DeviceContext_RSGetState", "struct ID3D11DeviceContext_RSGetViewports",
            "struct ID3D11DeviceContext_ClearState", "struct ContextTracking", "struct SavedContextTracking",
        ]
        extracted = [block(source, name) + (";" if name.startswith("struct ") else "") for name in names]
        extracted.append(source[source.index("\tconstexpr GUID kContextTrackingGuid"):source.index(";", source.index("\tconstexpr GUID kContextTrackingGuid"))+1])
        for name in ["ContextTracking CaptureContextTracking(", "HRESULT ReadContextTracking(",
                     "HRESULT SaveContextTracking(", "void RestoreContextTracking(", "struct ReverseZ::ContextStateHook",
                     "void ReverseZ::InstallRuntimeHooks()"]:
            extracted.append(block(source, name) + (";" if name.startswith("struct ") else ""))
        driver = driver.replace("PRODUCTION_GUARD", block(guard, "class ScopedDeviceContextState"))
        driver = driver.replace("PRODUCTION", "\n".join(extracted))
        compiler = os.environ.get("CXX") or shutil.which("cl") or shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="csx-reverse-state-") as temporary:
            folder = Path(temporary)
            path = folder / "test.cpp"
            executable = folder / ("test.exe" if os.name == "nt" else "test")
            path.write_text(driver, encoding="utf-8")
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", str(path), f"/Fe:{executable}"]
            else:
                command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(executable)]
            compiled = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
