"""Supplement executable math/WARP tests with shared-route wiring contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NR = ROOT / "src/Features/Upscaling/NeuralRendering"
SHADERS = ROOT / "features/Neural Rendering/Shaders/Upscaling/NeuralRendering"


class Contracts(unittest.TestCase):
    def test_lighting_layout_and_latched_shared_path(self):
        import hashlib
        import json
        fixture = ROOT / "tests/neural_color/fixtures/preserve-source-1-2-0"
        manifest = json.loads((fixture / "baseline.json").read_text())
        self.assertEqual(manifest["sourceCommit"], "d69bdb7ebc899ce24971eac59b007503e0dc08bb")
        for name, digest in manifest["bytecodeSha256"].items():
            self.assertEqual(hashlib.sha256((fixture / name).read_bytes()).hexdigest(), digest)
        pipeline = (NR / "ColorPipeline.cpp").read_text()
        self.assertIn("sizeof(Constants) == 64", pipeline)
        self.assertIn("offsetof(Constants, exposure) == 32", pipeline)
        self.assertIn("offsetof(Constants, lightingPreservation) == 48", pipeline)
        constants = pipeline.split("Constants MakeConstants", 1)[1].split("bool CreateTexture", 1)[0]
        self.assertIn("const auto& config = work.configuration", constants)
        self.assertIn("ResolveReconstructionSettings(config.settings)", constants)
        self.assertIn("settings.lightingPreservation, {}", constants)
        self.assertNotIn("Registry", constants)
        self.assertIn("requested.revision != work.configuration.revision", pipeline)
        self.assertIn("readback.source = work.observation", pipeline)
        self.assertIn("float3 NRColorPadding;", (SHADERS / "ColorCommon.hlsli").read_text())
        self.assertNotIn("LightingPreservation", (SHADERS / "ColorExposureCS.hlsl").read_text())
        renderer = (NR / "Renderer.cpp").read_text()
        self.assertIn("ResolveReconstructionSettings(capture.configuration.settings).lightingPreservation", renderer)
        ui = (ROOT / "src/Features/NeuralRenderingFeature.cpp").read_text()
        self.assertIn('if (ImGui::SliderFloat("Lighting preservation"', ui)
        self.assertEqual(ui.count("config.settings.lightingPreservation = preservationPercent / 100.0f"), 1)
        descriptor = json.loads(ui.split('R"schema(', 1)[1].split(')schema"', 1)[0])
        setting = descriptor["inputSchema"]["properties"]["settings"]["properties"]["lightingPreservation"]
        self.assertEqual((setting["type"], setting["minimum"], setting["maximum"]), ("number", 0, 1))
        self.assertIn("neural_lighting", descriptor["inputSchema"]["properties"]["settings"]["properties"]["mode"]["enum"])

    def test_vr_camera_observes_validated_engine_upload(self):
        source = (ROOT / "src/Globals.cpp").read_text()
        observer = source.split("void ObserveVRFrameBufferUpload(", 1)[1].split(
            "void InstallVRFrameBufferUploadHook()", 1)[0]
        self.assertIn("if (context == d3d::context", observer)
        self.assertIn("if (source)", observer)
        self.assertLess(observer.index("game::mappedFrameBuffer = nullptr"), observer.index("if (source)"))
        self.assertIn("resource == *game::perFrame && subresource == 0", observer)
        self.assertLess(observer.index("CacheFramebuffer(source)"), observer.index("context->Unmap("))
        install = source.split("void InstallVRFrameBufferUploadHook()", 1)[1].split("struct ID3D11DeviceContext_Map", 1)[0]
        self.assertIn("if (!game::isVR || installed)", install)
        self.assertIn("SKSE::RUNTIME_VR_1_4_15", install)
        self.assertEqual(install.count("std::memcmp("), 3)
        self.assertIn("sizeof(FrameBufferVR) == 0x570", install)
        self.assertIn("write_call<6>(upload + 0x7A4, observer)", install)

    def test_exposure_observes_actual_draw_bindings(self):
        hooks = (ROOT / "src/Globals.cpp").read_text()
        self.assertEqual(hooks.count("ExposureCapture::Instance().ObserveDraw(This,"), 7)
        self.assertNotIn("ObserveDraw", (ROOT / "src/Hooks.cpp").read_text())
        engine_hook = (ROOT / "src/Hooks.cpp").read_text().split(
            "void Hooks::BSGraphics_SetDirtyStates::thunk", 1)[1].split(
            "struct ID3D11Device_CreateVertexShader", 1)[0]
        branches = engine_hook.split("globals::state->Draw();")[1:]
        self.assertEqual(len(branches), 2)
        for branch in branches:
            self.assertIn("ObserveGraphicsStateFlush(globals::d3d::context, isCompute)", branch)
        capture = (NR / "ExposureCapture.cpp").read_text()
        self.assertIn("ExposureDrawRejection", capture)
        self.assertIn("c != globals::d3d::context", capture)
        self.assertIn("ComputeStateGuard<1>", capture)
        self.assertIn("auto* shader = activeHDRProducer", capture)
        self.assertIn("ReadExposureDrawBindings(c)", capture)
        state_flush = capture.split("void ExposureCapture::ObserveGraphicsStateFlush", 1)[1].split(
            "void ExposureCapture::Observe(", 1)[0]
        self.assertIn("if (!isCompute)", state_flush)
        self.assertIn("Observe(context, std::nullopt)", state_flush)
        annotations = (ROOT / "src/FrameAnnotations.cpp").read_text()
        install = annotations.split("void OnPostPostLoad()", 1)[1]
        before_guard = install.split("if (!globals::state->frameAnnotations)", 1)[0]
        self.assertEqual(before_guard.count("RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic"), 4)

    def test_history_reset_preserves_only_current_character_source(self):
        upscaling = (ROOT / "src/Features/Upscaling.cpp").read_text()
        reset = upscaling.split("void Upscaling::RequestHistoryReset() noexcept", 1)[1].split("\n}", 1)[0]
        self.assertIn("CharacterRendering::Instance().Invalidate(globals::state ?", reset)
        self.assertIn("globals::state->frameCount", reset)
        char = (NR / "CharacterRendering.cpp").read_text()
        invalidate = char.split("void CharacterRendering::Invalidate(", 1)[1].split("void CharacterRendering::ResetShaderCache", 1)[0]
        self.assertIn("state_->capturedFrame_ != a_preserveCaptureFrame", invalidate)
        self.assertIn("a_preserveCaptureFrame == std::numeric_limits<std::uint32_t>::max()", invalidate)
        self.assertIn("state_->InvalidatePreparedMasks();", invalidate)
        self.assertIn("RetainCurrentCharacterAdmissions(state_->actorAdmissions_", invalidate)
        self.assertNotIn("state_->actorAdmissions_.clear()", invalidate)
        authoring = upscaling.split("bool Upscaling::IsCharacterNeuralRenderingRouteRequested() const", 1)[1].split("\n}", 1)[0]
        self.assertNotIn("IsNeuralRenderingInsertionTransitionBlocked", authoring)
        self.assertIn("admission.sourceWorldFrame == admission.currentFrame", authoring)
        claim = upscaling.split("bool Upscaling::TryClaimNeuralRenderingRoute(", 1)[1].split("\n}", 1)[0]
        self.assertIn("IsNeuralRenderingInsertionTransitionBlocked()", claim)
        self.assertIn("~state_->capturedEnabledCategoryMask_", char)
        self.assertIn("state_->capturedFrame_ != sourceWorldFrame", char)
        self.assertEqual(char.count("state_->RecordPreparationFailure(a_args,"), 3)

    def test_shared_order_and_transaction(self):
        source = (NR / "Renderer.cpp").read_text(encoding="utf-8-sig")
        self.assertEqual(source.count("state_->CaptureColorConfiguration("), 3)
        self.assertLess(source.index("colorPipeline_.Prepare("), source.index("Runtime::Instance().Execute("))
        self.assertLess(source.index("colorPipeline_.Reconstruct("), source.index("colorPipeline_.Commit("))
        self.assertIn("colorConfiguration_.Enabled() ||", source)
        self.assertIn("colorInputEpoch = colorConfiguration_.inputEpoch", source)
        self.assertIn("historyValid = !colorConfiguration_.experiments.transportBypass", source)
        self.assertIn("context.timingPending = false", (NR / "ColorTransport.cpp").read_text())

    def test_baseline_selection_not_duplicated(self):
        source = (SHADERS / "ColorReconstructCS.hlsl").read_text()
        self.assertNotIn("CharacterMask", source)
        self.assertIn("RegionSize) - 1", source)
        self.assertIn("DetailStrength == 0.0 && AppearanceMix == 0.0", source)
        self.assertIn("baseline.a", source)
        self.assertIn("neuralSource - originalProxy", (SHADERS / "ColorCommon.hlsli").read_text())

    def test_async_measurement_and_resource_lifetime(self):
        source = (NR / "ColorPipeline.cpp").read_text()
        self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", source)
        self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT", source)
        self.assertNotIn("->Flush(", source)
        self.assertIn("measurement.source = readback.source", source)
        renderer = (NR / "Renderer.cpp").read_text()
        self.assertIn("for (auto& slot : slots_)\n\t\t\tcolorPipeline_.Poll", renderer)
        self.assertLess(renderer.index("colorPipeline_.Poll"), renderer.index("colorPipeline_.Prepare("))
        self.assertNotIn("Poll(context, work)", source)
        self.assertIn("observation.expectedMeasurementSlotMask = measurementSlotMask", renderer)
        guard = (NR / "ComputeStateGuard.h").read_text()
        self.assertIn("SetPredication(nullptr, FALSE)", guard)
        self.assertIn("SetPredication(predicate_.Get(), predicateValue_)", guard)
        self.assertIn("for (auto& readback : readbacks) readback.Abandon()", source)

    def test_feature_and_packaging(self):
        source = (ROOT / "src/Feature.cpp").read_text()
        self.assertIn("&NeuralRenderingFeature::Instance()", source)
        self.assertTrue((ROOT / "features/Neural Rendering/CORE").exists())
        ini = ROOT / "features/Neural Rendering/Shaders/Features/NeuralRendering.ini"
        self.assertIn("Version = 1-5-0", ini.read_text())
        ui = (ROOT / "src/Features/NeuralRenderingFeature.cpp").read_text()
        self.assertIn('"communityshaders.nr_color"', ui)
        save = ui[ui.index("void NeuralRenderingFeature::SaveSettings"):ui.index("void NeuralRenderingFeature::RestoreDefaultSettings")]
        self.assertNotIn("experiments", save)


if __name__ == "__main__":
    unittest.main()
