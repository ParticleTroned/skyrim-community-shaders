"""Exercise production grass setup, culling recovery and native wind restoration."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


def run_fixture(source: str, devbench: bool) -> subprocess.CompletedProcess:
    compiler = os.environ.get("CXX") or shutil.which("cl")
    if not compiler:
        raise RuntimeError("Run in the MSVC developer environment")
    runtime = (ROOT / "src/Features/GrassOptimizations/GrassRuntime.h").read_text(encoding="utf-8")
    hooks = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
    methods = "namespace GrassRuntime {\n" + block(runtime, "inline void PrepareGeometryWithoutAdvancingWind(") + "\n}\n"
    methods += block(hooks, "struct BSGrassShader_SetupGeometry") + ";\n"
    methods += "\n".join(block(source, signature) for signature in (
        "void GrassBucketRenderer::PrepareGeometry(", "void GrassBucketRenderer::DrawGroup(",
        "void GrassBucketRenderer::Impl::PrepareCulling(", "bool GrassBucketRenderer::Impl::DrawBucket(",
        "void GrassBucketRenderer::Impl::CopyGeometryConstants(", "void GrassBucketRenderer::Impl::RestoreCurrentGeometry(",
        "bool GrassBucketRenderer::Impl::HasBatchDrawState("))
    driver = r'''
#include "grass_draw_lifecycle_fixture.h"
PRODUCTION_METHODS
void Fixture::NativeSetup(RE::BSShader*, RE::BSRenderPass* pass, uint32_t flags) {
    auto& f = *active;
    if (f.inOriginalSetup && f.replacePropertyOnSetup) {
        pass->shaderProperty = f.replacementProperty.get();
        f.shape.geometry.shaderProperty = f.replacementProperty;
    }
    auto& data = GrassRuntime::GetPropertyData(static_cast<RE::BSGrassShaderProperty*>(pass->shaderProperty), globals::game::isVR);
    const float previous = flags & 0x10 ? 10.f : std::bit_cast<float>(data.windTimer);
    if (!(flags & 0x10)) data.windTimer = std::bit_cast<uint32_t>(10.f);
    const auto wind = GrassPolicy::WindVectorOffset(globals::game::isVR);
    f.constants[wind + 3] = 10.f;
    f.constants[wind + 7] = previous;
    ++f.nativeSetupCalls;
    const bool available = f.inOriginalSetup ? f.initialSnapshotAvailable : f.recoveredSnapshotAvailable;
    f.vertex.constantBuffers[2].data = available ? f.constants.data() : nullptr;
}
void Fixture::Setup(uint32_t flags) {
    BSGrassShader_SetupGeometry::func.target = NativeSetup;
    inOriginalSetup = true;
    BSGrassShader_SetupGeometry::thunk(&shader, &pass, flags);
    inOriginalSetup = false;
}
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        for (bool vr : {false, true}) {
            globals::game::isVR = vr;
            {
                Fixture f;
                f.Setup();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 0,
                    "Late graphics readiness must batch at the actual draw; setup must not cache native fallback");
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 0,
                    "A completed batch must suppress repeated native groups without resubmission");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw(false);
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "A mismatching native draw must never submit an early batch");
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "Native rendering already submitted must prevent later whole-bucket duplication");
            }
            {
                Fixture f;
                f.Setup();
                f.Draw();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "A failed actual draw must retain native rendering for the remainder of the pass");
            }
            {
                Fixture f;
                f.renderer.impl->frameSettings.Enabled = false;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Disabled optimization must retain native rendering");
            }
            {
                Fixture f;
                f.renderer.impl->nativeFallbackDepth = 1;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Expanded native fallback must not recursively attempt batching");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->throwOnPrepare = true;
                f.Setup();
                f.Draw();
                f.Draw();
                Require(f.renderer.impl->failed && f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "A preparation exception must retain native draws without submitting or retrying");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.Setup();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 0,
                    "A missing initial CPU snapshot must recover through native preparation");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = f.recoveredSnapshotAvailable = false;
                f.Setup();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Unrecoverable geometry constants must retain native rendering");
                Require(f.property->windTimer == std::bit_cast<uint32_t>(10.f),
                    "Unrecoverable constants must preserve the real native wind update");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                const auto culls = f.renderer.impl->cullingAttempts;
                f.pass.passEnum = 2;
                f.Setup();
                Require(!f.renderer.impl->geometryCPUValid && f.renderer.impl->cullingPrepared,
                    "The later pass must start with cached culling and no CPU snapshot");
                const auto calls = f.nativeSetupCalls;
                f.Draw();
                Require(f.renderer.impl->submissions == 2 && GrassBucketRenderer::nativeCalls == 0 &&
                    f.renderer.impl->cullingAttempts == culls && f.nativeSetupCalls == calls + 1,
                    "A later pass must recover its constants without repeating frame culling");
                const auto wind = GrassPolicy::WindVectorOffset(vr);
                Require(f.property->windTimer == std::bit_cast<uint32_t>(10.f) && f.constants[wind + 7] == 10.f,
                    "Later-pass recovery must restore that pass's original native wind");
            }

            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->allCulled = true;
                f.Setup();
                f.Draw();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 0,
                    "An entirely culled bucket must suppress all native groups without emitting draws");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->geometryCPUValid = true;
                auto& bucket = f.renderer.impl->buckets.front();
                bucket.plan.prepared = true;
                bucket.plan.geometry.front() = 1;
                Outcome outcome = Outcome::Native;
                Require(!f.renderer.impl->DrawBucket(bucket, outcome) && outcome == Outcome::Native,
                    "Changed geometry must retain native rendering");
            }
            for (uint32_t flags : {0u, 0x10u}) {
                for (float previous : {0.f, 9.f}) {
                    for (bool failPreparation : {false, true}) {
                        Fixture f;
                        f.initialSnapshotAvailable = false;
                        f.property->windTimer = std::bit_cast<uint32_t>(previous);
                        f.renderer.impl->ready = true;
                        f.renderer.impl->throwOnPrepare = failPreparation;
                        f.Setup(flags);
                        Require(f.renderer.impl->currentNativeWindTimer == std::bit_cast<uint32_t>(previous),
                            "The real hook must capture native property history before original setup");
                        Require(!f.renderer.impl->geometryCPUValid,
                            "The recovery case must begin without a CPU snapshot");
                        f.Draw();
                        const auto expectedHistory = std::bit_cast<uint32_t>(flags & 0x10 ? previous : 10.f);
                        const auto wind = GrassPolicy::WindVectorOffset(vr);
                        Require(f.property->windTimer == expectedHistory,
                            "Preparation must preserve the real native property's wind history");
                        Require(f.constants[wind + 7] == (flags & 0x10 ? 10.f : previous),
                            "Restoring native constants must retain the original previous wind on success and failure");
                        Require(f.renderer.impl->submissions == (failPreparation ? 0u : 1u),
                            "Recovered constants must batch unless preparation fails");
                        if (!failPreparation) {
                            const auto calls = f.nativeSetupCalls;
                            f.Draw();
                            Require(f.nativeSetupCalls == calls && f.renderer.impl->submissions == 1,
                                "Repeated groups must not advance wind or resubmit the batch");
                        }
                    }
                }
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.Setup();
                f.renderer.impl->currentNativeWindTimer.reset();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Absent native history must fail closed without unsafe restoration");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.renderer.impl->ready = true;
                f.renderer.impl->throwOnUpload = true;
                f.Setup();
                f.Draw();
                const auto wind = GrassPolicy::WindVectorOffset(vr);
                Require(f.renderer.impl->failed && GrassBucketRenderer::nativeCalls == 1 &&
                    f.property->windTimer == std::bit_cast<uint32_t>(10.f) && f.constants[wind + 7] == 9.f,
                    "An upload failure must preserve native wind and original constants before fallback");
            }
            for (unsigned unavailable : {0u, 1u, 2u}) {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                f.pass.passEnum = 2;
                f.Setup();
                const auto calls = f.nativeSetupCalls;
                if (unavailable == 0) f.renderer.impl->failed = true;
                if (unavailable == 1) f.renderer.impl->shaderAllowed = false;
                if (unavailable == 2) f.renderer.impl->depthAllowed = false;
                f.Draw();
                Require(f.nativeSetupCalls == calls && f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 1,
                    "Later-pass recovery must respect latched failure, shader eligibility and the main depth target");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.replacePropertyOnSetup = true;
                f.Setup();
                const auto calls = f.nativeSetupCalls;
                f.Draw();
                const auto wind = GrassPolicy::WindVectorOffset(vr);
                Require(!f.renderer.impl->currentNativeWindTimer && f.nativeSetupCalls == calls &&
                    f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1 &&
                    f.replacementProperty->windTimer == std::bit_cast<uint32_t>(10.f) && f.constants[wind + 7] == 3.f,
                    "Replacing the native property must invalidate its captured wind without changing the replacement's constants");
            }
            {
                Fixture f;
                f.renderer.impl->geometryCPUValid = true;
                const auto saved = globals::game::currentVertexShader;
                globals::game::currentVertexShader = nullptr;
                f.renderer.impl->CopyGeometryConstants();
                globals::game::currentVertexShader = saved;
                Require(!f.renderer.impl->geometryCPUValid,
                    "An unavailable shader handle must invalidate stale CPU constants");
            }
        }
        std::cout << "Production grass snapshot recovery, wind restoration, draw ordering and fallback passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_METHODS", methods)
    with tempfile.TemporaryDirectory(prefix="grass-draw-lifecycle-") as temporary:
        directory = Path(temporary)
        cpp = directory / "driver.cpp"
        executable = directory / "driver.exe"
        cpp.write_text(driver, encoding="utf-8")
        command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                   "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tests"),
                   str(cpp), "/Fe:" + str(executable)]
        if devbench:
            command.append("/DDEVBENCH_BRIDGE_ENABLED")
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True)


class GrassDrawLifecycleTests(unittest.TestCase):
    def test_production_setup_and_draw(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        for devbench in (False, True):
            with self.subTest(devbench=devbench):
                result = run_fixture(source, devbench)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
