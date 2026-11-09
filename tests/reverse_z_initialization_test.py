"""Execute production target-creation ordering and context-hook installation."""

import os
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class ReverseZInitializationTests(unittest.TestCase):
    def test_info_controls_and_pending_restart(self):
        from grass_draw_lifecycle_test import compile_fixture

        header = (ROOT / "src/Features/ReverseZ.h").read_text(encoding="utf-8")
        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <cassert>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
namespace globals {
struct State {
    std::unordered_map<std::string, bool> disabled;
    bool IsDeveloperMode() const { return false; }
    bool IsFeatureDisabled(const std::string& name) const { return disabled.contains(name) && disabled.at(name); }
    auto& GetDisabledFeatures() { return disabled; }
} stateStorage;
State* state = &stateStorage;
}
namespace ImGui {
unsigned controls = 0;
unsigned failures = 0;
std::optional<bool> edit;
std::string activeLabel;
void SeparatorText(const char*) {}
bool Checkbox(const char*, bool* value) {
    ++controls;
    if (!edit) return false;
    const bool changed = *value != *edit;
    *value = *edit;
    edit.reset();
    return changed;
}
void TextUnformatted(const char* value) { activeLabel = value; }
template<class... Args> void TextWrapped(const char*, Args...) { ++failures; }
}
namespace Util {
std::string tooltip;
void AddTooltip(const char* value) { tooltip = value; }
namespace Text {
unsigned restarts = 0;
void WrappedRestartNeeded(const char* value) {
    assert(std::string(value).find("Restart required") != std::string::npos);
    ++restarts;
}
}
}
struct ReverseZ {
SETTINGS;
    Settings settings;
    bool loaded = true, bootLatched = true, requestedThisBoot = false, activeThisBoot = false;
    std::mutex failureMutex;
    std::string failure;
    const char* failurePhase = "none";
    uint64_t failureGeneration = 0;
    bool IsActive() const { return activeThisBoot; }
    bool IsEnabledAtStartup() const;
    void SetEnabledAtStartup(bool);
    bool HasPendingBootChange() const;
    void RestoreDefaultSettings();
    void DrawSettings();
};
METHODS
int main() {
    static_assert(!ReverseZ::Settings{}.EnableReverseZBuffer);
    ReverseZ feature;
    const auto draw = [&](bool expectedRestart) {
        const auto controls = ImGui::controls;
        const auto restarts = Util::Text::restarts;
        feature.DrawSettings();
        assert(ImGui::controls == controls + 1);
        assert(Util::Text::restarts == restarts + unsigned(expectedRestart));
        assert(feature.HasPendingBootChange() == expectedRestart);
        assert(ImGui::activeLabel == (feature.IsActive() ? "Active: Reverse Z" : "Active: Standard Z"));
    };
    draw(false);
    assert(Util::tooltip.find("graphics memory") != std::string::npos);
    assert(Util::tooltip.find("performance") != std::string::npos);
    assert(Util::tooltip.find("restart") != std::string::npos);
    ImGui::edit = true;
    draw(true);
    assert(!feature.activeThisBoot && !feature.requestedThisBoot);
    draw(true);
    ImGui::edit = false;
    draw(false);
    feature.requestedThisBoot = feature.activeThisBoot = feature.settings.EnableReverseZBuffer = true;
    draw(false);
    ImGui::edit = false;
    draw(true);
    assert(feature.activeThisBoot && feature.requestedThisBoot);
    ImGui::edit = true;
    draw(false);
    feature.RestoreDefaultSettings();
    draw(true);
    assert(feature.activeThisBoot && feature.requestedThisBoot);
    feature.activeThisBoot = false;
    feature.failure = "Required depth hooks unavailable";
    feature.settings.EnableReverseZBuffer = true;
    const auto failures = ImGui::failures;
    draw(false);
    assert(ImGui::failures == failures + 1);
    ImGui::edit = false;
    draw(true);
    assert(ImGui::failures == failures + 2 && !feature.activeThisBoot);
    feature.failure.clear();
    feature.bootLatched = false;
    draw(false);
    feature.requestedThisBoot = false;
    feature.bootLatched = true;
    feature.settings.EnableReverseZBuffer = true;
    draw(true);
    feature.RestoreDefaultSettings();
    draw(false);
    feature.settings.EnableReverseZBuffer = true;
    globals::state->disabled["ReverseZ"] = true;
    draw(false);
    assert(!feature.IsEnabledAtStartup());
    ImGui::edit = true;
    draw(true);
    assert(feature.IsEnabledAtStartup() && !globals::state->disabled.contains("ReverseZ"));
    assert(!feature.IsActive());
    globals::state = nullptr;
    const auto controls = ImGui::controls;
    feature.DrawSettings();
    assert(ImGui::controls == controls);
}
'''
        driver = driver.replace("SETTINGS", block(header, "struct Settings"))
        driver = driver.replace("METHODS", "\n".join(block(source, signature) for signature in (
            "bool ReverseZ::HasPendingBootChange() const", "void ReverseZ::RestoreDefaultSettings()",
            "bool ReverseZ::IsEnabledAtStartup() const", "void ReverseZ::SetEnabledAtStartup(bool",
            "void ReverseZ::DrawSettings()")))
        result = compile_fixture(driver)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_startup_checkboxes_use_enabled_state(self):
        from grass_draw_lifecycle_test import compile_fixture

        menu = (ROOT / "src/Menu.cpp").read_text(encoding="utf-8")
        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
struct Feature {
    std::string name;
    bool hidden = false;
    virtual ~Feature() = default;
    virtual std::string GetShortName() { return name; }
    bool IsHiddenFromUserView() { return hidden; }
    static std::vector<Feature*> GetFeatureList();
};
namespace globals {
struct State {
    std::unordered_map<std::string, bool> disabled;
    bool IsFeatureDisabled(const std::string& name) const { return disabled.contains(name) && disabled.at(name); }
    auto& GetDisabledFeatures() { return disabled; }
} storage;
State* state = &storage;
}
struct ReverseZ : Feature {
    struct { bool EnableReverseZBuffer = false; } settings;
    bool active = false;
    bool IsEnabledAtStartup() const;
    void SetEnabledAtStartup(bool);
    std::string GetShortName() override { return "ReverseZ"; }
};
namespace globals::features { ReverseZ reverseZ; }
Feature clouds, grass, hidden;
std::vector<Feature*> Feature::GetFeatureList() { return { &grass, &globals::features::reverseZ, &::hidden, &clouds }; }
namespace ImGui {
void Text(const char*) {}
void Spacing() {}
bool CollapsingHeader(const char*) { return true; }
}
namespace Util::Widgets {
std::unordered_map<std::string, bool> shown;
std::optional<std::pair<std::string, bool>> edit;
bool Checkbox(const char* name, bool* value) {
    shown[name] = *value;
    if (!edit || edit->first != name) return false;
    *value = edit->second;
    edit.reset();
    return true;
}
}
struct Menu { void DrawDisableAtBootSettings(); } menu;
METHODS
int main() {
    clouds.name = "CloudShadows";
    grass.name = "GrassCollision";
    hidden.name = "Hidden";
    hidden.hidden = true;
    globals::state->disabled[clouds.name] = true;
    menu.DrawDisableAtBootSettings();
    assert(!Util::Widgets::shown.at(clouds.name));
    assert(Util::Widgets::shown.at(grass.name));
    assert(!Util::Widgets::shown.at("ReverseZ"));
    assert(!Util::Widgets::shown.contains(hidden.name));
    Util::Widgets::edit = { clouds.name, true };
    menu.DrawDisableAtBootSettings();
    assert(!globals::state->IsFeatureDisabled(clouds.name));
    Util::Widgets::edit = { grass.name, false };
    menu.DrawDisableAtBootSettings();
    assert(globals::state->IsFeatureDisabled(grass.name));
    auto& reverse = globals::features::reverseZ;
    Util::Widgets::edit = { "ReverseZ", true };
    menu.DrawDisableAtBootSettings();
    assert(reverse.settings.EnableReverseZBuffer && !reverse.active);
    reverse.SetEnabledAtStartup(false);
    menu.DrawDisableAtBootSettings();
    assert(!Util::Widgets::shown.at("ReverseZ"));
    reverse.settings.EnableReverseZBuffer = true;
    globals::state->disabled["ReverseZ"] = true;
    menu.DrawDisableAtBootSettings();
    assert(!Util::Widgets::shown.at("ReverseZ"));
    Util::Widgets::edit = { "ReverseZ", true };
    menu.DrawDisableAtBootSettings();
    assert(reverse.IsEnabledAtStartup() && !globals::state->disabled.contains("ReverseZ") && !reverse.active);
}
'''
        methods = block(menu, "void Menu::DrawDisableAtBootSettings()")
        methods += block(source, "bool ReverseZ::IsEnabledAtStartup() const")
        methods += block(source, "void ReverseZ::SetEnabledAtStartup(bool")
        result = compile_fixture(driver.replace("METHODS", methods))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_failure_diagnostics_schema(self):
        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        bridge = (ROOT / "src/Api/FeatureDevBenchBridge.cpp").read_text(encoding="utf-8")
        descriptor = json.loads(re.search(r'const char\* descriptor = R"\((.*?)\)";', bridge, re.S)[1])
        properties = descriptor["outputSchema"]["properties"]["result"]["properties"]
        emitted = set(re.findall(r'\{ "(\w+)",', block(source, "json ReverseZ::GetDiagnostics() const")))
        self.assertFalse(emitted - properties.keys(), f"Undocumented Reverse Z diagnostics: {emitted - properties.keys()}")
        action = bridge[bridge.index('if (action == "reverse_z")'):bridge.index('if (action == "sun_receiver")')]
        self.assertIn("return feature.GetDiagnostics();", action)
        self.assertNotIn('status["restartRequired"]', action)
        self.assertIn("feature.SetEnabledAtStartup(", action)
        self.assertIn("Reverse Z defaults off.", descriptor["description"])

    def test_activation_rejects_pre_activation_shader_publication(self):
        from grass_draw_lifecycle_test import compile_fixture, publication_generation_methods

        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        cache = (ROOT / "src/ShaderCache.cpp").read_text(encoding="utf-8")
        cache_header = (ROOT / "src/ShaderCache.h").read_text(encoding="utf-8")
        # D3D staging ends at this unlock; exercise the unchanged publication tail.
        preparation = block(source, "bool ReverseZ::PrepareDepthTargets()")
        publication = preparation[preparation.index("lock.unlock();"):]
        driver = r'''
#include "shader_publication_fixture.h"
struct ReverseZ {
    bool loaded = true, bootLatched = false, requestedThisBoot = false;
    struct { bool EnableReverseZBuffer = true; } settings;
    bool IsEnabledAtStartup() const { return settings.EnableReverseZBuffer; }
    std::atomic_bool activeThisBoot{false};
    bool depthTargetsConverted = false;
    double extraDepthBytesPerPixel = 0;
    std::string failure;
    std::atomic_uint64_t generation{0};
    void LatchBootState();
    bool HasShaderDefine(RE::BSShader::Type);
    bool PublishPreparedTargets();
} reverse;
namespace VRDepthCullingTemporal { void InvalidateDepthGeneration() {} }
using UINT = unsigned;
namespace globals::d3d {
    struct Context { void OMSetRenderTargets(UINT, void**, void*) {} } contextStorage;
    auto* context = &contextStorage;
}
REVERSE_METHODS
CACHE_METHODS
bool ReverseZ::PublishPreparedTargets() {
    std::mutex publicationMutex;
    std::unique_lock lock(publicationMutex);
    uint64_t extraBytes = 0, mainPixels = 0;
    std::array<void*, 1> colorViews{};
    void* replacementBoundDepth = nullptr;
    PUBLICATION
int main() {
    ShaderCache cache;
    globals::shaderCache = &cache;
    RE::BSShader water;
    Feature feature;
    Feature::GetFeatureList().push_back(&feature);
    static const auto before = cache.compilationSet.generation.load();
    assert(!reverse.HasShaderDefine(RE::BSShader::Type::Water) && reverse.requestedThisBoot);
    assert(cache.MakeAndAddVertexShader(water, 7, before));
    assert(cache.MakeAndAddPixelShader(water, 7, before));
    cache.shaderMap[7] = cache.hlslToShaderMap[7] = 1;
    cache.deferredEvictions.insert(7);
    cache.deferredEvictionCount = 1;
    resource.onRelease = [] {
        assert(compileDedupe.clears == 1);
        assert(globals::shaderCache->compilationSet.generation > before);
    };
    feature.onClear = [] {
        // Work queued after invalidation must already see the committed depth convention.
        assert(reverse.HasShaderDefine(RE::BSShader::Type::Water));
        assert(globals::shaderCache->vertexShaders[0].empty());
        assert(globals::shaderCache->pixelShaders[0].empty());
        const auto current = globals::shaderCache->compilationSet.generation.load();
        RE::BSShader water;
        assert(globals::shaderCache->MakeAndAddVertexShader(water, 9, current));
        assert(globals::shaderCache->MakeAndAddPixelShader(water, 9, current));
    };
    assert(reverse.PublishPreparedTargets());
    assert(reverse.HasShaderDefine(RE::BSShader::Type::Water));
    assert(reverse.depthTargetsConverted && reverse.generation == 1);
    assert(resource.releases == 2 && cache.compilationSet.generation > before);
    assert(cache.shaderMap.empty() && cache.hlslToShaderMap.empty());
    assert(cache.deferredEvictions.empty() && cache.deferredEvictionCount == 0);
    assert(feature.clears == 1 && globals::deferred->clears == 1);
    // A delayed worker must not repopulate cleared descriptor slots or replace new stages.
    assert(!cache.MakeAndAddVertexShader(water, 7, before));
    assert(!cache.MakeAndAddPixelShader(water, 7, before));
    assert(cache.vertexShaders[0].size() == 1 && cache.vertexShaders[0].contains(9));
    assert(cache.pixelShaders[0].size() == 1 && cache.pixelShaders[0].contains(9));
    auto* vertex = cache.MakeAndAddVertexShader(water, 7);
    auto* pixel = cache.MakeAndAddPixelShader(water, 7);
    assert(vertex && pixel);
    assert(!cache.MakeAndAddVertexShader(water, 7, before));
    assert(!cache.MakeAndAddPixelShader(water, 7, before));
    assert(cache.vertexShaders[0].at(7).get() == vertex && cache.pixelShaders[0].at(7).get() == pixel);
    const auto current = cache.compilationSet.generation.load();
    assert(reverse.PublishPreparedTargets() && reverse.generation == 2);
    assert(cache.compilationSet.generation == current && feature.clears == 1);
    assert(cache.vertexShaders[0].at(7).get() == vertex && cache.pixelShaders[0].at(7).get() == pixel);
    assert(testDevice.creates == 6 && resource.releases == 2);
    assert(resource.live == 4 && resource.named == 6);
    resource.onRelease = nullptr;
    cache.ReleaseResources();
    assert(resource.live == 0 && resource.releases == 6);
    for (const auto& candidate : testDevice.resources)
        assert(candidate->references == 0 && candidate->releases == 1);
}
'''
        driver = driver.replace("REVERSE_METHODS", "\n".join(block(source, signature) for signature in (
            "void ReverseZ::LatchBootState(", "bool ReverseZ::HasShaderDefine(")))
        methods = "\n".join(block(cache, signature) for signature in (
            "void ShaderCache::Clear()", "RE::BSGraphics::VertexShader* ShaderCache::MakeAndAddVertexShader(",
            "RE::BSGraphics::PixelShader* ShaderCache::MakeAndAddPixelShader("))
        methods += publication_generation_methods(cache_header)
        methods += block(cache, "void CompilationSet::BumpGeneration()").replace(
            "CompilationSet::", "ShaderCache::Queue::")
        driver = driver.replace("CACHE_METHODS", methods).replace("PUBLICATION", publication)
        result = compile_fixture(driver)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_camera_upload_and_publication_before_global_range(self):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        source = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        globals_header = (ROOT / "src/Globals.h").read_text(encoding="utf-8")
        driver = r'''
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <iostream>
#include <limits>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>
#include "Utils/DepthConvention.h"
using namespace DirectX;
struct Matrix {
    float m[4][4];
    static inline thread_local unsigned inversions = 0;
    Matrix Invert() const {
        ++inversions;
        Matrix result;
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&result),
            XMMatrixInverse(nullptr, XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(this))));
        return result;
    }
};
struct float4 { float x, y, z, w; };
namespace globals {
FRAME_BUFFER
FRAME_BUFFER_VR
namespace game { bool isVR = false; float range = 0; float* cameraNear = &range; float* cameraFar = &range; }
namespace d3d { void* context = nullptr; }
}
struct ViewData { Matrix projMat, projMatrixUnjittered, viewProjMat, viewProjMatrixUnjittered; };
namespace RE {
struct Frustum { float fNear = 1, fFar = 100000; bool bOrtho = false; };
struct NiCamera {
    Frustum frusta[2];
    struct VRData { Frustum* viewFrustumArray; } vr{frusta};
    struct Data { Frustum viewFrustum; } flat;
    const VRData& GetVRRuntimeData() const { return vr; }
    const Data& GetRuntimeData2() const { return flat; }
};
namespace BSGraphics { using ViewData = ::ViewData; }
namespace Main { NiCamera* world; NiCamera* WorldRootCamera() { return world; } }
}
namespace logger { template<class... T> void info(const char*, T...) {} }
struct CameraStateEntry { RE::NiCamera* camera; ViewData viewData; };
struct CameraStateEntryVR { RE::NiCamera* camera; std::vector<ViewData> viewData; };
struct ID3D11DepthStencilView {};
std::shared_mutex g_viewMutex;
std::unordered_set<ID3D11DepthStencilView*> g_viewIsReverse;
ViewData published[2];
bool g_projectionReversed = true, converted = true;
std::atomic<bool> g_reversalActive = false;
std::atomic<bool> g_renderingRejected = false;
constexpr uint32_t kStereoEyeCount = 2;
uint32_t publishedEyeCount = 2;
uint32_t PublishedEyeCount() { return publishedEyeCount; }
uint32_t PublishedDepthStencilTarget() { return 0; }
bool IsConvertedDepthTarget(uint32_t) { return converted; }
int missingPublishedEye = -1;
ViewData* PublishedViewData(uint32_t eye) { return static_cast<int>(eye) == missingPublishedEye ? nullptr : &published[eye]; }
void ReapplyMappedStates(void*) {}
unsigned bindingRefreshes = 0;
void RefreshBoundDepthTarget() { ++bindingRefreshes; }
struct ReverseZ {
    bool activeThisBoot = true, depthTargetsConverted = true;
    uint64_t generation = 0;
    uint64_t GetGeneration() const { return generation; }
    unsigned rejected = 0;
    void RejectRendering(const char*) { ++rejected; }
    void ReversePublishedProjection();
    void ApplyReverseProjection(void*, const RE::NiCamera*);
    bool ExpectPublishedReversal(const RE::NiCamera*, bool) const;
    bool IsReverseDepthView(ID3D11DepthStencilView*) const;
    void FixupMappedFrameBuffer(globals::FrameBuffer&);
    bool FixupMappedFrameBufferVR(globals::FrameBufferVR&);
};
CAMERA_HELPERS
CAMERA_METHODS
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
Matrix Store(FXMMATRIX value) {
    Matrix result;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&result), value);
    return result;
}
XMMATRIX Load(const Matrix& value) { return XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&value)); }
bool Equal(const Matrix& a, const Matrix& b, float tolerance = 1e-5f) {
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            if (!std::isfinite(a.m[row][col]) || std::abs(a.m[row][col] - b.m[row][col]) > tolerance) return false;
    return true;
}
void Fill(const UploadedEye& eye, float offset, float nearPlane, float farPlane) {
    const auto projection = XMMatrixPerspectiveOffCenterLH(-0.8f + offset, 1.2f + offset,
        -0.7f, 0.9f, nearPlane, farPlane);
    const auto view = XMMatrixLookToLH(XMVectorSet(50, -13, 200, 1),
        XMVectorSet(0.3f, 0.2f, 1, 0), XMVectorSet(0, 1, 0, 0));
    eye.proj = eye.projUnjittered = Store(XMMatrixTranspose(projection));
    eye.viewProj = eye.viewProjUnjittered = Store(XMMatrixTranspose(XMMatrixMultiply(view, projection)));
    // Temporal history can have a different depth range from the current eye.
    const auto previous = XMMatrixPerspectiveFovLH(1.1f, 1.3f, nearPlane * 2, farPlane / 2);
    eye.previousViewProj = Store(XMMatrixTranspose(XMMatrixMultiply(view, previous)));
}
void CheckReversal(const UploadedEye& eye, const Matrix& before, const Matrix& previous) {
    Require(AlreadyReversed(eye.proj, ZAxis::kCol2), "Eye projection was not reversed");
    auto expected = before;
    for (int col = 0; col < 4; ++col) expected.m[2][col] = before.m[3][col] - before.m[2][col];
    Require(Equal(eye.proj, expected), "Reversal changed the camera's own depth range");
    expected = previous;
    for (int col = 0; col < 4; ++col) expected.m[2][col] = previous.m[3][col] - previous.m[2][col];
    Require(Equal(eye.previousViewProj, expected), "Temporal history used the current camera's range");
    for (const auto pair : { std::pair{eye.proj, eye.projInverse},
             std::pair{eye.projUnjittered, eye.projUnjitteredInverse},
             std::pair{eye.viewProj, eye.viewProjInverse} }) {
        const auto identity = Store(XMMatrixMultiply(Load(pair.first), Load(pair.second)));
        Require(Equal(identity, Store(XMMatrixIdentity()), 1e-3f), "Reversed upload has a stale inverse");
    }
}
void CheckExactInverses(const UploadedEye& eye) {
    for (const auto pair : {std::pair{eye.proj, eye.projInverse},
             std::pair{eye.projUnjittered, eye.projUnjitteredInverse},
             std::pair{eye.viewProj, eye.viewProjInverse}}) {
        const auto expected = Store(XMMatrixInverse(nullptr, Load(pair.first)));
        Require(std::memcmp(expected.m, pair.second.m, sizeof(expected.m)) == 0,
            "Cached inverse differs from a fresh inversion of the normalized matrix");
    }
}
void CheckInverseReuse() {
    ReverseZ reverse;
    reverse.generation = 100;
    g_reversalActive = g_projectionReversed = true;
    globals::FrameBufferVR original{};
    for (uint32_t eye = 0; eye < kStereoEyeCount; ++eye)
        Fill(VRUploadedEye(original, eye), eye ? 0.03f : -0.03f, eye ? 5.0f : 1.0f, 100000);
    auto frame = original;
    auto inversions = Matrix::inversions;
    reverse.FixupMappedFrameBufferVR(frame);
    Require(Matrix::inversions == inversions + 6, "Cold stereo upload did not invert all six inputs");
    const auto expected = frame;
    inversions = Matrix::inversions;
    for (unsigned repeat = 0; repeat < 64; ++repeat) {
        frame = original;
        reverse.FixupMappedFrameBufferVR(frame);
        Require(std::memcmp(&frame, &expected, sizeof(frame)) == 0,
            "Repeated upload trusted stale inverses or skipped independent matrix normalization");
    }
    Require(Matrix::inversions == inversions, "Identical normalized stereo inputs repeated inversions");

    frame.CameraViewProjUnjittered[0] = original.CameraViewProjUnjittered[0];
    frame.CameraPreviousViewProjUnjittered[1] = original.CameraPreviousViewProjUnjittered[1];
    frame.CameraViewProjUnjittered[0].m[0][3] += 3;
    frame.CameraPreviousViewProjUnjittered[1].m[0][3] += 17;
    reverse.FixupMappedFrameBufferVR(frame);
    auto changed = expected;
    changed.CameraViewProjUnjittered[0].m[0][3] += 3;
    changed.CameraPreviousViewProjUnjittered[1].m[0][3] += 17;
    Require(Matrix::inversions == inversions && std::memcmp(&frame, &changed, sizeof(frame)) == 0,
        "Inverse reuse skipped changed history or an independently standard matrix");

    for (auto* matrix : {&frame.CameraProj[0], &frame.CameraViewProj[0], &frame.CameraProjUnjittered[0], &frame.CameraProj[1]}) {
        matrix->m[0][0] = std::nextafter(matrix->m[0][0], std::numeric_limits<float>::infinity());
        reverse.FixupMappedFrameBufferVR(frame);
        Require(Matrix::inversions == ++inversions, "A one-bit input change did not invalidate exactly its own inverse");
        for (uint32_t eye = 0; eye < kStereoEyeCount; ++eye) CheckExactInverses(VRUploadedEye(frame, eye));
    }
    auto& zero = frame.CameraProj[0].m[0][1];
    Require(zero == 0, "Signed-zero fixture is not zero");
    zero = std::bit_cast<float>(std::bit_cast<uint32_t>(zero) ^ 0x80000000u);
    reverse.FixupMappedFrameBufferVR(frame);
    Require(Matrix::inversions == ++inversions, "Signed-zero input change was treated as an exact match");
    CheckExactInverses(VRUploadedEye(frame, 0));

    // Every component belongs to the exact key, even where a denormal barely affects the result.
    UploadedEyeInverseCache::Entry entry;
    auto matrix = Store(XMMatrixIdentity());
    entry.Resolve(matrix);
    inversions = Matrix::inversions;
    for (auto& row : matrix.m) for (auto& value : row) {
        value = std::bit_cast<float>(std::bit_cast<uint32_t>(value) ^ 1u);
        entry.Resolve(matrix);
        Require(Matrix::inversions == ++inversions, "Inverse key omitted a matrix component");
    }

    const auto beforeRecreation = frame;
    ++reverse.generation;
    reverse.FixupMappedFrameBufferVR(frame);
    inversions += 6;
    Require(Matrix::inversions == inversions && std::memcmp(&frame, &beforeRecreation, sizeof(frame)) == 0,
        "Target recreation retained cached inverses or changed their results");

    {
        struct FloatingPointScope {
            unsigned saved = _mm_getcsr();
            ~FloatingPointScope() { _mm_setcsr(saved); }
        } control;
        for (const auto bit : {_MM_ROUND_DOWN, _MM_FLUSH_ZERO_MASK, _MM_DENORMALS_ZERO_MASK}) {
            _mm_setcsr(control.saved ^ bit);
            reverse.FixupMappedFrameBufferVR(frame);
            inversions += 6;
            Require(Matrix::inversions == inversions, "Floating-point control change reused a preceding inverse");
        }
        _mm_setcsr(control.saved);
        reverse.FixupMappedFrameBufferVR(frame);
        inversions += 6;
        Require(Matrix::inversions == inversions, "Restored floating-point mode reused a different mode's inverse");
        for (uint32_t eye = 0; eye < kStereoEyeCount; ++eye) CheckExactInverses(VRUploadedEye(frame, eye));
        _mm_setcsr(_mm_getcsr() ^ _MM_EXCEPT_INEXACT);
        reverse.FixupMappedFrameBufferVR(frame);
        Require(Matrix::inversions == inversions, "Sticky exception status invalidated unchanged inverse inputs");
    }

    g_projectionReversed = false;
    auto standard = original;
    reverse.FixupMappedFrameBufferVR(standard);
    Require(Matrix::inversions == inversions && std::memcmp(&standard, &original, sizeof(standard)) == 0,
        "Standard camera domain populated the inverse cache or modified its upload");
    g_projectionReversed = true;
    reverse.FixupMappedFrameBufferVR(frame);
    Require(Matrix::inversions == inversions, "Standard domain damaged the retained reversed inputs");

    globals::FrameBuffer flat{};
    Fill(FlatUploadedEye(flat), 0, 1, 100000);
    reverse.FixupMappedFrameBuffer(flat);
    inversions += 3;
    Require(Matrix::inversions == inversions, "Flat upload did not use its own inverse entries");
    const auto flatOnce = flat;
    flat.CameraProjInverse = flat.CameraViewProjInverse = flat.CameraProjUnjitteredInverse = Store(XMMatrixIdentity());
    reverse.FixupMappedFrameBuffer(flat);
    reverse.FixupMappedFrameBufferVR(frame);
    Require(Matrix::inversions == inversions && std::memcmp(&flat, &flatOnce, sizeof(flat)) == 0,
        "Flat and stereo caches interfered or a cached upload trusted supplied inverses");
    ++reverse.generation;
    reverse.FixupMappedFrameBuffer(flat);
    Require(Matrix::inversions == inversions + 3, "Flat cache ignored target recreation");

    bool threadLocalCache = false;
    const auto workerControl = _mm_getcsr();
    std::thread worker([&] {
        _mm_setcsr(workerControl);
        auto copy = frame;
        reverse.FixupMappedFrameBufferVR(copy);
        const auto cold = Matrix::inversions;
        reverse.FixupMappedFrameBufferVR(copy);
        threadLocalCache = cold == 6 && Matrix::inversions == cold && std::memcmp(&copy, &frame, sizeof(copy)) == 0;
    });
    worker.join();
    Require(threadLocalCache, "Upload threads shared inverse entries or produced different results");
    g_reversalActive = false;
}
void CheckMixedCameraMatrices() {
    ReverseZ reverse;
    std::array<UploadedEyeInverseCache, kStereoEyeCount> caches;
    RE::NiCamera world;
    world.flat.viewFrustum = {5, 100000, false};
    RE::Main::world = &world;
    // Independent homogeneous z'=w-z transform in uploaded column-vector layout.
    const auto depthReflection = XMMatrixSet(1, 0, 0, 0, 0, 1, 0, 0,
        0, 0, -1, 1, 0, 0, 0, 1);
    for (float headX : {0.0f, 5.0f}) {
        for (float yaw : {0.0f, 0.3f, 0.6f, XM_PIDIV2, XM_PI})
        for (float pitch : {-0.75f, -0.35f, 0.0f, 0.35f, 0.75f}) {
            for (unsigned mask = 0; mask < 32; ++mask) {
                globals::FrameBufferVR frame{};
                for (unsigned index = 0; index < 2; ++index) {
                    auto eye = VRUploadedEye(frame, index);
                    Fill(eye, index ? 0.03f : -0.03f, 5, 100000);
                    const auto view = XMMatrixLookToLH(XMVectorSet(headX, 0, 0, 1),
                        XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                            yaw == XM_PIDIV2 ? 0.0f : std::cos(yaw) * std::cos(pitch), 0), XMVectorSet(0, 1, 0, 0));
                    eye.viewProj = eye.viewProjUnjittered = Store(XMMatrixMultiply(Load(eye.proj), XMMatrixTranspose(view)));
                    const std::array matrices{&eye.proj, &eye.viewProj, &eye.viewProjUnjittered,
                        &eye.projUnjittered, &eye.previousViewProj};
                    std::array<Matrix, 5> expected;
                    for (unsigned matrix = 0; matrix < matrices.size(); ++matrix) {
                        expected[matrix] = Store(XMMatrixMultiply(depthReflection, Load(*matrices[matrix])));
                        if (mask & (1u << matrix)) *matrices[matrix] = expected[matrix];
                    }
                    FixupUploadedEye(eye, caches[index], reverse.GetGeneration());
                    for (unsigned matrix = 0; matrix < matrices.size(); ++matrix)
                        Require(Equal(*matrices[matrix], expected[matrix]), "Mixed upload reversed a camera matrix twice or left it standard");
                    const auto point = XMVectorSet(300, 10, 3000, 1);
                    const auto clip = XMVector4Transform(point, XMMatrixTranspose(Load(expected[1])));
                    const auto reconstructed = XMVector4Transform(clip, XMMatrixTranspose(Load(eye.viewProjInverse)));
                    const auto error = XMVectorGetX(XMVector3Length(XMVectorSubtract(point, XMVectorScale(reconstructed, 1 / XMVectorGetW(reconstructed)))));
                    Require(error < 0.1f, "Fixed water/reflection point moved with the head pose");
                    for (bool transpose : {false, true}) {
                        ViewData expectedView{expected[0], expected[3], expected[1], expected[2]};
                        if (transpose) {
                            for (auto* matrix : {&expectedView.projMat, &expectedView.projMatrixUnjittered,
                                    &expectedView.viewProjMat, &expectedView.viewProjMatrixUnjittered})
                                *matrix = Store(XMMatrixTranspose(Load(*matrix)));
                        }
                        auto mixed = expectedView;
                        unsigned bit = 1;
                        for (auto* matrix : {&mixed.projMat, &mixed.projMatrixUnjittered,
                                &mixed.viewProjMat, &mixed.viewProjMatrixUnjittered}) {
                            if (!(mask & bit))
                                *matrix = Store(transpose ? XMMatrixMultiply(Load(*matrix), XMMatrixTranspose(depthReflection)) :
                                    XMMatrixMultiply(depthReflection, Load(*matrix)));
                            bit <<= 1;
                        }
                        CameraStateEntry entry{&world, mixed};
                        reverse.ApplyReverseProjection(&entry, &world);
                        published[0] = published[1] = mixed;
                        reverse.ReversePublishedProjection();
                        for (const auto& actual : {entry.viewData, published[0], published[1]}) {
                            Require(Equal(actual.projMat, expectedView.projMat) &&
                                Equal(actual.projMatrixUnjittered, expectedView.projMatrixUnjittered) &&
                                Equal(actual.viewProjMat, expectedView.viewProjMat) &&
                                Equal(actual.viewProjMatrixUnjittered, expectedView.viewProjMatrixUnjittered),
                                "Mixed native camera matrices do not share a reversed depth convention");
                        }
                    }
                }
                const auto before = frame;
                for (unsigned index = 0; index < 2; ++index)
                    FixupUploadedEye(VRUploadedEye(frame, index), caches[index], reverse.GetGeneration());
                Require(std::memcmp(&before, &frame, sizeof(frame)) == 0, "Mixed camera upload was not idempotent");
            }
        }
    }
    g_reversalActive = false;
    bindingRefreshes = 0;
}
int main() {
    try {
        CheckMixedCameraMatrices();
        CheckInverseReuse();
        ReverseZ reverse;
        globals::FrameBuffer flat{};
        Fill(FlatUploadedEye(flat), 0, 1, 100000);
        RE::NiCamera world, menu;
        RE::Main::world = &world;
        ID3D11DepthStencilView depthView;
        g_viewIsReverse.insert(&depthView);
        Require(!reverse.IsReverseDepthView(&depthView), "Prepared menu target was classified as reversed before a world projection");
        Require(!reverse.ExpectPublishedReversal(&menu, false), "Startup menu was forced into reversed projection");
        Require(!reverse.ExpectPublishedReversal(&world, false), "Unbuilt world camera activated reverse projection");
        const auto startupFlat = flat;
        reverse.FixupMappedFrameBuffer(flat);
        Require(std::memcmp(&startupFlat, &flat, sizeof(flat)) == 0, "Startup menu upload was reversed before world activation");
        globals::FrameBufferVR startupStereo{};
        for (uint32_t eye = 0; eye < 2; ++eye) Fill(VRUploadedEye(startupStereo, eye), 0, 1, 100000);
        const auto startupStereoBefore = startupStereo;
        Require(!reverse.FixupMappedFrameBufferVR(startupStereo), "Startup stereo upload requested a redundant GPU write");
        Require(std::memcmp(&startupStereoBefore, &startupStereo, sizeof(startupStereo)) == 0,
            "Stereo menu upload was reversed before world activation");
        for (auto& data : published) {
            data.projMat = data.projMatrixUnjittered = Store(XMMatrixTranspose(Load(flat.CameraProj)));
            data.viewProjMat = data.viewProjMatrixUnjittered = data.projMat;
        }
        const auto startupPublished = published[0];
        reverse.ReversePublishedProjection();
        Require(!g_reversalActive && std::memcmp(&startupPublished, &published[0], sizeof(ViewData)) == 0,
            "Startup publication activated reversal without a world camera");
        CameraStateEntry entry{&menu, startupPublished};
        reverse.ApplyReverseProjection(&entry, &menu);
        Require(!g_reversalActive && !bindingRefreshes, "Menu-camera construction activated world reversal");
        entry.camera = &world;
        reverse.ApplyReverseProjection(&entry, &world);
        Require(g_reversalActive && bindingRefreshes == 1 && reverse.IsReverseDepthView(&depthView),
            "First world projection did not activate and refresh the current depth binding");
        Require(AlreadyReversed(entry.viewData.projMat, DetectZAxis(entry.viewData.projMat)), "World projection remained standard");
        reverse.ApplyReverseProjection(&entry, &world);
        Require(bindingRefreshes == 1, "Repeated world projection refreshed activation twice");
        g_reversalActive = false;
        globals::game::isVR = true;
        CameraStateEntryVR stereoEntry{&world, {startupPublished, startupPublished}};
        reverse.ApplyReverseProjection(&stereoEntry, &world);
        Require(g_reversalActive && bindingRefreshes == 2, "Stereo world projection did not activate reversal");
        for (const auto& eye : stereoEntry.viewData)
            Require(AlreadyReversed(eye.projMat, DetectZAxis(eye.projMat)), "Stereo world eye remained standard");
        const auto reversedStereo = stereoEntry.viewData;
        g_reversalActive = false;
        reverse.ApplyReverseProjection(&stereoEntry, &world);
        Require(g_reversalActive && bindingRefreshes == 3,
            "Already reversed world camera did not activate depth-state mapping");
        Require(std::memcmp(reversedStereo.data(), stereoEntry.viewData.data(), 2 * sizeof(ViewData)) == 0,
            "Already reversed world camera was changed during activation");
        Require(reverse.ExpectPublishedReversal(&world, false), "Active world projection was not recognized");
        Require(!reverse.ExpectPublishedReversal(&world, true), "Cubemap capture inherited the world camera's depth convention");
        const auto flatProjection = flat.CameraProj, flatPrevious = flat.CameraPreviousViewProjUnjittered;
        reverse.FixupMappedFrameBuffer(flat);
        Require(reverse.rejected == 0, "Startup upload blocked rendering because global range is unset");
        CheckReversal(FlatUploadedEye(flat), flatProjection, flatPrevious);
        const auto once = flat;
        reverse.FixupMappedFrameBuffer(flat);
        Require(std::memcmp(&once, &flat, sizeof(flat)) == 0, "Repeated flat upload reversed twice");
        globals::FrameBufferVR vr{};
        for (uint32_t eye = 0; eye < 2; ++eye) {
            Fill(VRUploadedEye(vr, eye), eye ? 0.03f : -0.03f, eye ? 5.0f : 1.0f, eye ? 50000.0f : 100000.0f);
        }
        // The native upload may already contain a reversed eye.
        UploadedEyeInverseCache rightCache;
        FixupUploadedEye(VRUploadedEye(vr, 1), rightCache, reverse.GetGeneration());
        const auto right = vr.CameraProj[1];
        const auto left = vr.CameraProj[0], previous = vr.CameraPreviousViewProjUnjittered[0];
        Require(reverse.FixupMappedFrameBufferVR(vr), "Active stereo upload did not request corrected bytes");
        Require(reverse.rejected == 0, "Stereo startup upload blocked rendering because global range is unset");
        CheckReversal(VRUploadedEye(vr, 0), left, previous);
        Require(Equal(right, vr.CameraProj[1]), "Already reversed stereo eye was reversed twice");
        const auto stereoOnce = vr;
        Require(reverse.FixupMappedFrameBufferVR(vr), "Reversed stereo upload trusted incoming inverses");
        Require(std::memcmp(&stereoOnce, &vr, sizeof(vr)) == 0, "Repeated stereo upload reversed twice");
        for (publishedEyeCount = 1; publishedEyeCount <= 2; ++publishedEyeCount) {
            for (uint32_t eye = 0; eye < publishedEyeCount; ++eye) {
                auto& data = published[eye];
                data.projMat = data.projMatrixUnjittered = Store(XMMatrixTranspose(Load(left)));
                data.viewProjMat = data.viewProjMatrixUnjittered = data.projMat;
            }
            reverse.ReversePublishedProjection();
            Require(reverse.rejected == 0 && g_reversalActive, "Startup camera publication blocked rendering");
            for (uint32_t eye = 0; eye < publishedEyeCount; ++eye) {
                const auto oncePublished = published[eye];
                Require(AlreadyReversed(oncePublished.projMat, DetectZAxis(oncePublished.projMat)),
                    "Published eye projection remained standard");
                reverse.ReversePublishedProjection();
                Require(std::memcmp(&oncePublished, &published[eye], sizeof(ViewData)) == 0,
                    "Published eye projection reversed twice");
            }
        }
        publishedEyeCount = 2;
        for (auto& data : published) {
            data.projMat = data.projMatrixUnjittered = Store(XMMatrixOrthographicLH(2, 2, 1, 100));
            data.viewProjMat = data.viewProjMatrixUnjittered = data.projMat;
        }
        const auto orthographic = published[0];
        reverse.ReversePublishedProjection();
        Require(reverse.rejected == 0 && std::memcmp(&orthographic, &published[0], sizeof(ViewData)) == 0,
            "Orthographic camera publication blocked rendering or reversed its projection");
        g_projectionReversed = false;
        Fill(FlatUploadedEye(flat), 0, 1, 100000);
        const auto standard = flat;
        reverse.FixupMappedFrameBuffer(flat);
        Require(std::memcmp(&standard, &flat, sizeof(flat)) == 0, "Standard camera domain changed uploads");
        g_projectionReversed = true;
        globals::game::isVR = true;
        const auto rejectedBefore = reverse.rejected;
        world.vr.viewFrustumArray = nullptr;
        reverse.ApplyReverseProjection(&stereoEntry, &world);
        Require(reverse.rejected == rejectedBefore + 1, "Missing stereo frusta did not reject rendering");
        world.vr.viewFrustumArray = world.frusta;
        stereoEntry.viewData.resize(1);
        reverse.ApplyReverseProjection(&stereoEntry, &world);
        Require(reverse.rejected == rejectedBefore + 2, "Incomplete stereo eyes did not reject rendering");
        stereoEntry.viewData.resize(2);
        stereoEntry.viewData.assign(2, startupPublished);
        const auto beforeInvalidStereo = stereoEntry.viewData;
        world.frusta[1].fFar = world.frusta[1].fNear;
        reverse.ApplyReverseProjection(&stereoEntry, &world);
        Require(reverse.rejected == rejectedBefore + 3, "Invalid second-eye range did not reject rendering");
        Require(std::memcmp(beforeInvalidStereo.data(), stereoEntry.viewData.data(), 2 * sizeof(ViewData)) == 0,
            "Invalid second eye partially changed the stereo camera");
        published[0] = published[1] = startupPublished;
        missingPublishedEye = 1;
        reverse.ReversePublishedProjection();
        Require(reverse.rejected == rejectedBefore + 4, "Missing published eye did not reject rendering");
        Require(std::memcmp(&startupPublished, &published[0], sizeof(ViewData)) == 0,
            "Missing second published eye partially changed the stereo camera");
        missingPublishedEye = -1;
        globals::game::isVR = false;
        world.flat.viewFrustum.fNear = 0;
        reverse.ApplyReverseProjection(&entry, &world);
        Require(reverse.rejected == rejectedBefore + 5, "Invalid flat range did not reject rendering");
        globals::game::isVR = true;
        world.frusta[1].fFar = 100000;
        for (unsigned orthographicEye = 0; orthographicEye < 2; ++orthographicEye) {
            for (bool active : {false, true}) {
                g_reversalActive = active;
                stereoEntry.viewData.assign(2, startupPublished);
                stereoEntry.viewData[orthographicEye] = orthographic;
                const auto before = stereoEntry.viewData;
                const auto rejected = reverse.rejected;
                reverse.ApplyReverseProjection(&stereoEntry, &world);
                Require(reverse.rejected == rejected + 1 && g_reversalActive == active,
                    "Mixed stereo camera kinds did not reject before activation");
                Require(std::memcmp(before.data(), stereoEntry.viewData.data(), 2 * sizeof(ViewData)) == 0,
                    "Mixed stereo camera kinds partially changed the world camera");
            }
            g_reversalActive = true;
            published[0] = published[1] = startupPublished;
            published[orthographicEye] = orthographic;
            const std::array before{published[0], published[1]};
            const auto rejected = reverse.rejected;
            reverse.ReversePublishedProjection();
            Require(reverse.rejected == rejected + 1 && std::memcmp(before.data(), published, sizeof(published)) == 0,
                "Mixed published camera kinds partially changed the stereo camera");
        }
        reverse.activeThisBoot = false;
        Fill(FlatUploadedEye(flat), 0, 1, 100000);
        const auto inactive = flat;
        reverse.FixupMappedFrameBuffer(flat);
        Require(std::memcmp(&inactive, &flat, sizeof(flat)) == 0, "Inactive feature changed uploads");
        std::cout << "Flat/stereo startup publication, upload, inverse and temporal history passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''
        helpers = [block(source, "enum class ZAxis") + ";"]
        for signature in (
            "ZAxis DetectZAxis(", "bool AlreadyReversed(",
            "void ReverseClipZ(Matrix& matrix, ZAxis axis, float", "void ReverseClipZ(Matrix& matrix, ZAxis axis)",
            "struct UploadedEye", "UploadedEye FlatUploadedEye(", "UploadedEye VRUploadedEye(",
            "struct UploadedEyeInverseCache", "void FixupUploadedEye(",
        ):
            helpers.append(block(source, signature) + (";" if signature.startswith("struct ") else ""))
        methods = [block(source, signature) for signature in (
            "void ReverseZ::ReversePublishedProjection(", "void ReverseZ::FixupMappedFrameBuffer(",
            "bool ReverseZ::FixupMappedFrameBufferVR(", "void ReverseZ::ApplyReverseProjection(",
            "bool ReverseZ::ExpectPublishedReversal(", "bool ReverseZ::IsReverseDepthView(",
        )]
        driver = driver.replace("FRAME_BUFFER_VR", block(globals_header, "struct FrameBufferVR") + ";")
        driver = driver.replace("FRAME_BUFFER", block(globals_header, "struct FrameBuffer\n") + ";")
        driver = driver.replace("CAMERA_HELPERS", "\n".join(helpers)).replace("CAMERA_METHODS", "\n".join(methods))
        with tempfile.TemporaryDirectory(prefix="csx-reverse-z-camera-") as temporary:
            directory = Path(temporary)
            cpp, exe = directory / "camera.cpp", directory / "camera.exe"
            cpp.write_text(driver, encoding="utf-8")
            compiled = subprocess.run(
                [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", f"/I{ROOT / 'src'}",
                 str(cpp), f"/Fe:{exe}"], cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cold_start_recreation_and_later_init3d(self):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        hooks = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
        globals_source = (ROOT / "src/Globals.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <array>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct RecreationMutex {
    bool owned = false;
    void lock() { Require(!owned, "Recursive target recreation"); owned = true; }
    void unlock() { owned = false; }
} g_renderTargetRecreationMutex;
struct ID3D11DeviceContext {};
struct ID3D11Device {};
struct ID3D11DeviceContext_Map {};
struct ID3D11DeviceContext_Unmap {};
struct ID3D11DeviceContext_DrawIndexed {};
struct ID3D11DeviceContext_Draw {};
namespace winrt {
template<class T> struct com_ptr {
    T* value = nullptr;
    T* get() const { return value; }
    void copy_from(T* pointer) { value = pointer; }
};
}
namespace REL {
template<class Function> using Relocation = Function*;
}
namespace Hooks {
using VRRenderTargetRecreatePreparation = void (*)(void*);
using VRRenderTargetRecreateMutationEntered = void (*)(void*);
using VRRenderTargetRecreateCheckpoint = bool (*)(void*, bool);
}
struct Fixture {
    ID3D11Device device;
    ID3D11DeviceContext context;
    bool engineReady = false, uploadReady = false, active = false;
    bool throwOnPrepare = false, callbackFailed = false, dispatchChanged = false;
    unsigned hookInstallCalls = 0, isolationRegistrations = 0;
    unsigned engineCreates = 0, preparations = 0, setups = 0;
    unsigned entered = 0, checkpoints = 0, failedCheckpoints = 0;
} *fixture;
std::set<std::pair<ID3D11DeviceContext*, unsigned>> installedSlots;
namespace Util {
void RegisterDeviceContextStateForHooks(ID3D11DeviceContext* context) {
    Require(context == &fixture->context, "Isolation registered the wrong context");
    ++fixture->isolationRegistrations;
}
}
namespace stl {
template<unsigned Slot, class> void detour_vfunc(ID3D11DeviceContext* context) {
    Require(fixture->isolationRegistrations == 1, "Isolation capability was not registered before hooks");
    ++fixture->hookInstallCalls;
    Require(Slot != 12 || !fixture->dispatchChanged,
        "Native draw dispatch changed after initial hook installation");
    installedSlots.emplace(context, Slot);
}
}
namespace globals {
namespace game {
void* renderer = nullptr;
bool isVR = false;
}
namespace d3d {
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
}
struct State {
    bool published = false;
    void InvalidateRenderTargetResourcePublication() { published = false; }
    void Setup() {
        Require(!g_renderTargetRecreationMutex.owned && fixture->active,
            "State published before converted target preparation completed");
        published = true;
        ++fixture->setups;
    }
    void SetupRenderTargetResources() { Setup(); }
} stateStorage;
State* state = &stateStorage;
void* deferred = &stateStorage;
void ReInit() {
    Require(fixture->engineReady, "Globals refreshed before engine target creation");
    game::renderer = fixture;
    d3d::device = &fixture->device;
    d3d::context = &fixture->context;
}
void InstallVRFrameBufferUploadHook() {
    Require(game::renderer && d3d::context, "Upload hook installed before renderer refresh");
    if (game::isVR) fixture->uploadReady = true;
}
INSTALL_D3D_HOOKS
namespace features {
struct ReverseZ {
    void PrepareDepthTargets() {
        Require(g_renderTargetRecreationMutex.owned, "Depth conversion escaped recreation ownership");
        Require(game::renderer && d3d::device && d3d::context,
            "Depth preparation preceded renderer cache publication");
        Require(!game::isVR || fixture->uploadReady, "VR depth preparation preceded upload observation");
        Require(installedSlots.contains({ d3d::context, 14 }) &&
            installedSlots.contains({ d3d::context, 15 }), "Depth preparation preceded D3D observation");
        if (fixture->throwOnPrepare) throw std::runtime_error("Preparation failed");
        fixture->active = true;
        ++fixture->preparations;
    }
} reverseZ;
}
}
TARGET_CREATION_HOOK
void NativeCreate() {
    Require(g_renderTargetRecreationMutex.owned && !globals::state->published,
        "Native recreation did not own unpublished resources");
    fixture->engineReady = true;
    ++fixture->engineCreates;
}
void Entered(void*) { ++fixture->entered; }
bool Checkpoint(void*, bool failed) {
    Require(g_renderTargetRecreationMutex.owned, "Ownership reconciliation escaped the recreation lock");
    if (failed) { ++fixture->failedCheckpoints; return true; }
    Require(fixture->active, "Ownership reconciliation preceded depth preparation");
    ++fixture->checkpoints;
    return !fixture->callbackFailed;
}
int main() {
    try {
        std::array<Fixture, 3> runtimes;
        for (unsigned runtime = 0; runtime < runtimes.size(); ++runtime) {
            fixture = &runtimes[runtime];
            globals::game::renderer = nullptr;
            globals::game::isVR = runtime == 2;
            globals::d3d::device = nullptr;
            globals::d3d::context = nullptr;
            globals::state->published = false;
            BSShaderRenderTargets_Create::func = NativeCreate;
            const auto previousSlots = installedSlots.size();
            Require(BSShaderRenderTargets_Create::RecreateAndSetupFull(), "Cold target creation failed");
            Require(fixture->active && globals::state->published, "Cold start lost Reverse Z shader state");
            Require(installedSlots.size() == previousSlots + 4, "Cold start missed context hooks");
            globals::ReInit();
            globals::InstallD3DHooks(globals::d3d::context);
            Require(installedSlots.size() == previousSlots + 4, "Later Init3D installed duplicate hooks");
            fixture->dispatchChanged = true;
            Require(BSShaderRenderTargets_Create::RecreateAndSetupFull(), "Warm target recreation failed");
            bool entered = false;
            Require(BSShaderRenderTargets_Create::RecreateAndSetupRenderTargetResources(
                nullptr, Entered, Checkpoint, nullptr, &entered), "Target-only recreation failed");
            Require(entered && fixture->entered == 1 && fixture->checkpoints == 1,
                "Target-only mutation ownership was lost");
            const auto setups = fixture->setups;
            fixture->throwOnPrepare = true;
            bool threw = false;
            try {
                (void)BSShaderRenderTargets_Create::RecreateAndSetupRenderTargetResources(
                    nullptr, Entered, Checkpoint, nullptr, &entered);
            } catch (const std::runtime_error&) { threw = true; }
            Require(threw && fixture->failedCheckpoints == 1 && !g_renderTargetRecreationMutex.owned,
                "Preparation failure lost reconciliation or retained the lock");
            Require(!globals::state->published && fixture->setups == setups,
                "Failed preparation published resources");
            fixture->throwOnPrepare = false;
            fixture->callbackFailed = true;
            Require(!BSShaderRenderTargets_Create::RecreateAndSetupRenderTargetResources(
                nullptr, Entered, Checkpoint, nullptr, &entered), "Failed checkpoint was accepted");
            Require(!globals::state->published && fixture->setups == setups &&
                !g_renderTargetRecreationMutex.owned, "Failed checkpoint published resources or retained the lock");
            Require(installedSlots.size() == previousSlots + 4, "Recreation reinstalled context hooks");
            Require(fixture->hookInstallCalls == 4, "Context hooks were installed more than once");
            Require(fixture->isolationRegistrations == 1, "Recreation reset the isolation capability");
        }
        std::cout << "SE/AE/VR cold creation, warm recreation, Init3D hook reuse and failure ownership passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''
        driver = driver.replace("INSTALL_D3D_HOOKS", block(globals_source, "void InstallD3DHooks("))
        driver = driver.replace("TARGET_CREATION_HOOK", block(hooks, "struct BSShaderRenderTargets_Create") + ";")
        with tempfile.TemporaryDirectory(prefix="csx-reverse-z-init-") as temporary:
            directory = Path(temporary)
            cpp, exe = directory / "initialization.cpp", directory / "initialization.exe"
            cpp.write_text(driver, encoding="utf-8")
            compiled = subprocess.run(
                [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", str(cpp), f"/Fe:{exe}"],
                cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
