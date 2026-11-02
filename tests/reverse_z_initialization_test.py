"""Execute the native mono camera and target-creation hooks from production source."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from grass_collision_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class ReverseZInitializationTests(unittest.TestCase):
    def run_driver(self, driver):
        compiler = os.environ.get("CXX") or shutil.which("cl") or shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="csx-se-reverse-z-") as temporary:
            directory = Path(temporary)
            source = directory / "test.cpp"
            executable = directory / ("test.exe" if os.name == "nt" else "test")
            source.write_text(driver, encoding="utf-8")
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", f"/I{ROOT / 'src'}", str(source), f"/Fe:{executable}"]
            else:
                command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"), str(source), "-o", str(executable)]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_target_creation_prepares_depth_before_resources(self):
        hooks = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
        globals_source = (ROOT / "src/Globals.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <array>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct ID3D11DeviceContext {};
struct ID3D11Device {};
struct ID3D11DeviceContext_Map {};
struct ID3D11DeviceContext_Unmap {};
struct ID3D11DeviceContext_DrawIndexed {};
struct ID3D11DeviceContext_Draw {};
struct ID3D11DeviceContext_DrawIndexedInstanced {};
struct ID3D11DeviceContext_DrawInstanced {};
struct ID3D11DeviceContext_DrawAuto {};
struct ID3D11DeviceContext_DrawIndexedInstancedIndirect {};
struct ID3D11DeviceContext_DrawInstancedIndirect {};
namespace winrt {
template<class T> struct com_ptr {
    T* value = nullptr;
    T* get() const { return value; }
    void copy_from(T* pointer) { value = pointer; }
};
}
namespace REL { template<class Function> using Relocation = Function*; }
struct Fixture {
    ID3D11Device device;
    ID3D11DeviceContext context;
    bool engineReady = false, active = false, throwOnPrepare = false;
    unsigned hookCalls = 0, creates = 0, preparations = 0, setups = 0;
} *fixture;
std::set<std::pair<ID3D11DeviceContext*, unsigned>> slots;
namespace stl {
template<unsigned Slot, class> void detour_vfunc(ID3D11DeviceContext* context) {
    ++fixture->hookCalls;
    Require(slots.emplace(context, Slot).second, "An immediate-context hook was installed twice");
}
}
namespace globals {
namespace game { void* renderer = nullptr; }
namespace d3d { ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr; }
struct State {
    void Setup() {
        Require(fixture->active && fixture->preparations == fixture->creates, "Resources published before depth conversion");
        ++fixture->setups;
    }
    void SetupRenderTargetResources() { Setup(); }
} stateStorage;
State* state = &stateStorage;
void* deferred = &stateStorage;
void ReInit() {
    Require(fixture->engineReady, "Renderer refreshed before engine creation");
    game::renderer = fixture;
    d3d::device = &fixture->device;
    d3d::context = &fixture->context;
}
INSTALL_D3D_HOOKS
namespace features {
struct ReverseZ {
    void PrepareDepthTargets() {
        Require(game::renderer && d3d::device && d3d::context, "Depth conversion preceded renderer refresh");
        Require(slots.contains({d3d::context, 14}) && slots.contains({d3d::context, 15}), "Depth conversion preceded framebuffer observation");
        if (fixture->throwOnPrepare) throw std::runtime_error("Allocation failed");
        fixture->active = true;
        ++fixture->preparations;
    }
} reverseZ;
}
}
TARGET_CREATION_HOOK
void NativeCreate() { fixture->engineReady = true; ++fixture->creates; }
int main() {
    try {
        std::array<Fixture, 2> runtimes;
        for (auto& runtime : runtimes) {
            fixture = &runtime;
            globals::game::renderer = nullptr;
            globals::d3d::device = nullptr;
            globals::d3d::context = nullptr;
            BSShaderRenderTargets_Create::func = NativeCreate;
            Require(BSShaderRenderTargets_Create::RecreateAndSetupFull(), "Cold target creation failed");
            const auto calls = fixture->hookCalls;
            Require(calls == 9, "Native D3D observation is incomplete");
            globals::InstallD3DHooks(globals::d3d::context);
            Require(BSShaderRenderTargets_Create::RecreateAndSetupFull(), "Warm target recreation failed");
            Require(BSShaderRenderTargets_Create::RecreateAndSetupRenderTargetResources(), "Target-only recreation failed");
            Require(fixture->hookCalls == calls, "Later Init3D or recreation installed duplicate hooks");
            const auto setups = fixture->setups;
            fixture->throwOnPrepare = true;
            bool threw = false;
            try { (void)BSShaderRenderTargets_Create::RecreateAndSetupRenderTargetResources(); }
            catch (const std::runtime_error&) { threw = true; }
            Require(threw && fixture->setups == setups, "Failed preparation published feature resources");
        }
        globals::InstallD3DHooks(nullptr);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''
        driver = driver.replace("INSTALL_D3D_HOOKS", block(globals_source, "void InstallD3DHooks("))
        driver = driver.replace("TARGET_CREATION_HOOK", block(hooks, "struct BSShaderRenderTargets_Create") + ";")
        self.run_driver(driver)

    def test_world_projection_activation_and_native_upload(self):
        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/Globals.h").read_text(encoding="utf-8")
        driver = r'''
#include "Utils/DepthConvention.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_set>
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Matrix {
    float m[4][4]{};
    Matrix Invert() const {
        double a[4][8]{};
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { a[r][c] = m[r][c]; a[r][c + 4] = r == c; }
        for (int p = 0; p < 4; ++p) {
            int pivot = p;
            for (int r = p + 1; r < 4; ++r) if (std::abs(a[r][p]) > std::abs(a[pivot][p])) pivot = r;
            Require(std::abs(a[pivot][p]) > 1e-15, "Singular test camera");
            for (int c = 0; c < 8; ++c) std::swap(a[p][c], a[pivot][c]);
            const double scale = a[p][p];
            for (int c = 0; c < 8; ++c) a[p][c] /= scale;
            for (int r = 0; r < 4; ++r) if (r != p) {
                const double factor = a[r][p];
                for (int c = 0; c < 8; ++c) a[r][c] -= factor * a[p][c];
            }
        }
        Matrix result;
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) result.m[r][c] = static_cast<float>(a[r][c + 4]);
        return result;
    }
};
struct float4 { float x, y, z, w; };
namespace globals { FRAME_BUFFER namespace d3d { void* context = nullptr; } }
struct ViewData { Matrix projMat, projMatrixUnjittered, viewProjMat, viewProjMatrixUnjittered; };
namespace RE {
struct Frustum { float fNear = 1, fFar = 100000; bool bOrtho = false; };
struct NiCamera { struct Data { Frustum viewFrustum; } flat; const Data& GetRuntimeData2() const { return flat; } };
namespace Main { NiCamera* world; NiCamera* WorldRootCamera() { return world; } }
}
namespace logger { template<class... T> void info(const char*, T...) {} }
struct CameraStateEntry { RE::NiCamera* camera; ViewData camViewData; };
struct ID3D11DepthStencilView {};
std::shared_mutex g_viewMutex;
std::unordered_set<ID3D11DepthStencilView*> g_viewIsReverse;
ViewData published;
bool g_projectionReversed = true, converted = true;
std::atomic<bool> g_reversalActive = false;
uint32_t PublishedDepthStencilTarget() { return 0; }
bool IsConvertedDepthTarget(uint32_t) { return converted; }
ViewData* PublishedViewData() { return &published; }
void ReapplyMappedStates(void*) {}
unsigned bindingRefreshes = 0;
void RefreshBoundDepthTarget() { ++bindingRefreshes; }
struct ReverseZ {
    bool activeThisBoot = true, depthTargetsConverted = true;
    unsigned rejected = 0;
    void RejectRendering(const char*) { ++rejected; }
    void ReversePublishedProjection();
    void ApplyReverseProjection(void*, const RE::NiCamera*);
    bool ExpectPublishedReversal(const RE::NiCamera*, bool) const;
    bool IsReverseDepthView(ID3D11DepthStencilView*) const;
    void FixupMappedFrameBuffer(globals::FrameBuffer&);
};
CAMERA_HELPERS
CAMERA_METHODS
Matrix Projection(float nearPlane = 1, float farPlane = 100000) {
    Matrix m;
    m.m[0][0] = 1.3f; m.m[1][1] = 1.7f; m.m[0][2] = .05f;
    m.m[2][2] = farPlane / (farPlane - nearPlane); m.m[3][2] = 1;
    m.m[2][3] = -nearPlane * farPlane / (farPlane - nearPlane);
    return m;
}
Matrix Transpose(Matrix value) {
    for (int r = 0; r < 4; ++r) for (int c = r + 1; c < 4; ++c) std::swap(value.m[r][c], value.m[c][r]);
    return value;
}
void CheckInverse(const Matrix& value, const Matrix& inverse) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        float product = 0;
        for (int k = 0; k < 4; ++k) product += value.m[r][k] * inverse.m[k][c];
        Require(std::abs(product - (r == c ? 1.f : 0.f)) < 1e-4f, "Upload has stale inverse");
    }
}
int main() {
    try {
        ReverseZ reverse;
        globals::FrameBuffer frame{};
        auto camera = FlatUploadedCamera(frame);
        camera.proj = camera.viewProj = camera.viewProjUnjittered = camera.projUnjittered = Projection();
        camera.previousViewProj = Projection(2, 50000);
        const auto startup = frame;
        RE::NiCamera world, menu;
        RE::Main::world = &world;
        ID3D11DepthStencilView view;
        g_viewIsReverse.insert(&view);
        Require(!reverse.IsReverseDepthView(&view), "Unactivated menu target uses reversed state");
        Require(!reverse.ExpectPublishedReversal(&world, false), "Unbuilt world activated reversal");
        reverse.FixupMappedFrameBuffer(frame);
        Require(std::memcmp(&startup, &frame, sizeof(frame)) == 0, "Startup menu upload changed");
        published.projMat = published.projMatrixUnjittered = published.viewProjMat = published.viewProjMatrixUnjittered = Transpose(Projection());
        const auto before = published;
        reverse.ReversePublishedProjection();
        Require(std::memcmp(&before, &published, sizeof(published)) == 0, "Menu publication activated world reversal");
        CameraStateEntry entry{&menu, before};
        reverse.ApplyReverseProjection(&entry, &menu);
        Require(!g_reversalActive, "Menu camera activated world reversal");
        reverse.ApplyReverseProjection(&entry, &world);
        Require(g_reversalActive && bindingRefreshes == 1 && reverse.IsReverseDepthView(&view), "World did not refresh the bound depth target");
        Require(AlreadyReversed(entry.camViewData.projMat, DetectZAxis(entry.camViewData.projMat)), "World camera remained standard");
        reverse.ApplyReverseProjection(&entry, &world);
        Require(bindingRefreshes == 1, "World activation repeated");
        reverse.FixupMappedFrameBuffer(frame);
        Require(!UploadedProjectionIsStandard(camera.proj), "Native upload remained standard");
        CheckInverse(camera.proj, camera.projInverse);
        CheckInverse(camera.viewProj, camera.viewProjInverse);
        CheckInverse(camera.projUnjittered, camera.projUnjitteredInverse);
        for (int c = 0; c < 4; ++c)
            Require(camera.previousViewProj.m[2][c] == startup.CameraPreviousViewProjUnjittered.m[3][c] - startup.CameraPreviousViewProjUnjittered.m[2][c], "History adopted the current camera range");
        const auto once = frame;
        reverse.FixupMappedFrameBuffer(frame);
        Require(std::memcmp(&once, &frame, sizeof(frame)) == 0, "Native upload reversed twice");
        reverse.ReversePublishedProjection();
        Require(AlreadyReversed(published.projMat, DetectZAxis(published.projMat)), "Published projection remained standard");
        Require(!reverse.ExpectPublishedReversal(&menu, true), "Cubemap camera forced to scene depth");
        menu.flat.viewFrustum.bOrtho = true;
        Require(!reverse.ExpectPublishedReversal(&menu, false), "Orthographic camera forced to scene depth");
        g_projectionReversed = false;
        frame = startup;
        reverse.FixupMappedFrameBuffer(frame);
        Require(std::memcmp(&startup, &frame, sizeof(frame)) == 0, "Standard camera domain changed upload");
        world.flat.viewFrustum.fNear = 0;
        reverse.ApplyReverseProjection(&entry, &world);
        Require(reverse.rejected == 1, "Invalid camera range was accepted");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''
        helpers = [block(source, "enum class ZAxis") + ";"]
        for signature in ("ZAxis DetectZAxis(", "bool AlreadyReversed(",
                          "void ReverseClipZ(Matrix& matrix, ZAxis axis, float", "void ReverseClipZ(Matrix& matrix, ZAxis axis)",
                          "struct UploadedCamera", "UploadedCamera FlatUploadedCamera(",
                          "bool UploadedProjectionIsStandard(", "void FixupUploadedCamera("):
            helpers.append(block(source, signature) + (";" if signature.startswith("struct ") else ""))
        methods = [block(source, signature) for signature in (
            "void ReverseZ::ReversePublishedProjection(", "void ReverseZ::FixupMappedFrameBuffer(",
            "void ReverseZ::ApplyReverseProjection(", "bool ReverseZ::ExpectPublishedReversal(", "bool ReverseZ::IsReverseDepthView(")]
        driver = driver.replace("FRAME_BUFFER", block(header, "struct FrameBuffer\n") + ";")
        driver = driver.replace("CAMERA_HELPERS", "\n".join(helpers)).replace("CAMERA_METHODS", "\n".join(methods))
        self.run_driver(driver)


if __name__ == "__main__":
    unittest.main()
