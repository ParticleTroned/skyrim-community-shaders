"""Boundary contracts supplement executed native and WARP lifecycle tests."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
NR = ROOT / "src/Features/Upscaling/NeuralRendering"


def read(path):
    return path.read_text(encoding="utf-8")


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n\t}", start)]


class CacheContracts(unittest.TestCase):
    def test_cache_request_cannot_teardown_or_reset_policy(self):
        source = read(NR / "Renderer.cpp")
        request = body(source, "void Renderer::ResetShaderCache()")
        self.assertIn("shaderInvalidation_.Request()", request)
        invalidate = body(source, "void Renderer::State::ApplyShaderInvalidationLocked()")
        for forbidden in ("ResetLocked(", "TeardownBackendLocked(", "WaitForIdle(", "memoryRecovery_", "failureLatched_", "quarantined_", "memoryReservation_", "slots_ ="):
            self.assertNotIn(forbidden, request + invalidate)
        for owner in ("colorPipeline_", "modelResolution_"):
            self.assertIn(owner + ".InvalidateShaders()", invalidate)
        self.assertIn("slot.historyValid = false", invalidate)
        service = body(source, "void Renderer::ServiceBackendMaintenance(")
        self.assertLess(service.index("RendererOwnership"), service.index("ApplyShaderInvalidationLocked"))
        self.assertIn("ServiceStallRecoveryLocked", service)
        self.assertIn("ServiceRetainedBackendLocked", service)

    def test_narrow_owners_retain_work_and_constants(self):
        for file, signature in (("ColorPipeline.cpp", "void Pipeline::InvalidateShaders()"),
                                ("ModelResolution.cpp", "void ModelResolution::InvalidateShaders()"),
                                ("ExposureCapture.cpp", "void ExposureCapture::InvalidateShaders()")):
            source = body(read(NR / file), signature)
            self.assertNotIn("constants_.Reset()", source)
            self.assertNotIn("slots_ =", source)
            self.assertNotIn("epoch", source)
        actor = body(read(NR / "CharacterRendering.cpp"), "void CharacterRendering::ResetShaderCache()")
        self.assertNotIn("ResetEarlyMaskBounds()", actor)
        self.assertNotIn("constants_.Reset()", actor)
        self.assertIn("InvalidateCaptureMetadata()", actor)

    def test_reference_shader_requests_keep_ordinary_invalidation_independent(self):
        source = read(NR / "Renderer.cpp")
        request = body(source, "void Renderer::ResetReferenceShaders()")
        self.assertIn("referenceShaderInvalidation_.Request()", request)
        self.assertNotIn("shaderInvalidation_.Request()", request)
        invalidate = body(source, "void Renderer::State::ApplyShaderInvalidationLocked()")
        reference = invalidate[invalidate.index("if (generation == shaderInvalidation_.Applied()) {"):invalidate.index("#else")]
        self.assertIn("colorPipeline_.InvalidateShaders()", reference)
        self.assertIn("referenceShaderInvalidation_.Applied(referenceGeneration)", reference)
        for forbidden in ("slot.historyValid", "modelResolution_", "CharacterRendering::", "shaderInvalidation_.Applied(generation)"):
            self.assertNotIn(forbidden, reference)
        self.assertIn("slot.historyValid = false", invalidate[invalidate.index("#else"):])
        status = body(source, "bool Renderer::HasPendingShaderInvalidation() const noexcept")
        self.assertIn("shaderInvalidation_.Pending() || state_->referenceShaderInvalidation_.Pending()", status)

    def test_native_only_prewarm_and_safe_adoption(self):
        source = read(NR / "Renderer.cpp")
        prewarm = body(source, "void Renderer::State::ServicePrewarmLocked(") + body(source, "void Renderer::State::PreparePrewarmFeatureLocked(")
        for forbidden in (".Execute(", "SucceedLocked(", "CompleteActiveFrame(", "lastSuccessfulFrame =", "lastUsedMs =", "PrepareActorProtection("):
            self.assertNotIn(forbidden, prewarm)
        self.assertIn("PrepareFeature(commands", prewarm)
        self.assertIn("completed >= fences.issued", prewarm)
        self.assertLess(prewarm.index("if (fences.deviceRemoved)"), prewarm.index("ResetFeature(slot)"))
        self.assertIn("FailLocked(RendererStage::DeviceRemoved, DXGI_ERROR_DEVICE_REMOVED", prewarm)
        self.assertIn("preview.Admit(", prewarm)
        self.assertIn("policy.failed", prewarm)
        self.assertIn("runtimeReady_ && !policy.backendOwned", prewarm)
        admission = body(source, "bool Renderer::State::AdmitMemoryLocked(")
        self.assertIn("prewarm_.failed && prewarm_.backendOwned", admission)
        retire = body(source, "bool Renderer::State::RetireSlotLocked(")
        self.assertIn("prewarm_.unusedMask", retire)
        self.assertLess(retire.index("WaitForIdle("), retire.index("ResetFeature("))
        adopt = body(source, "bool Renderer::State::EnsureSlotLocked(")
        self.assertIn("HasCompatibleFeature", adopt)
        self.assertIn("prewarm_.Adopt(a_slot, prepared)", adopt)

    def test_registered_control_and_no_history_key_change(self):
        source = read(ROOT / "src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        schema = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', source, re.S).group(1))
        self.assertIn("nr_shader_invalidate", schema["inputSchema"]["properties"]["action"]["enum"])
        prewarm = schema["inputSchema"]["properties"]["prewarmEnabled"]
        self.assertEqual(prewarm["type"], "boolean")
        self.assertIs(prewarm["default"], True)
        renderer = schema["outputSchema"]["properties"]["neuralRendering"]["properties"]["renderer"]["properties"]
        self.assertIn("shaderGenerationApplied", renderer)
        self.assertEqual(renderer["prewarm"]["properties"]["providerAllocationBytes"]["type"], "null")
        upscaling = read(ROOT / "src/Features/Upscaling.cpp")
        key = body(upscaling, "uint64_t BuildNeuralRenderingSettingsKey(")
        self.assertNotIn("neuralRenderingPrewarmEnabled", key)
        service = upscaling.split("void Upscaling::ServiceNeuralRenderingPrewarm()", 1)[1].split("void Upscaling::Reset()", 1)[0]
        self.assertIn("Util::IsMenuOpen<RE::MainMenu>", service)
        self.assertIn("Util::IsMenuOpen<RE::LoadingMenu>", service)
        self.assertIn("resource_grid_not_authoritative", service)
        self.assertIn("!enabledChanged && !renderer.HasPrewarmWork()", service)
        self.assertIn("!enabledChanged && menu == neuralPrewarmWasMenu", service)
        retirement = body(read(NR / "Renderer.cpp"), "void Renderer::State::ServicePrewarmLocked(")
        self.assertLess(retirement.index("policy.CancelRequest()"), retirement.index("const bool retiring"))
        self.assertIn("SuppressRetryAfterRetirement(now)", retirement)
        self.assertRegex(service, r"if \(renderer.ServicePrewarm\(request, reason, opportunity\)\)\s+neuralPrewarmWasEnabled = enabled")
        owner = body(read(NR / "Renderer.cpp"), "bool Renderer::ServicePrewarm(")
        self.assertIn("if (!lock.owns_lock())\n\t\t\treturn false;", owner)
        self.assertNotIn("PrepareFullResolutionNeuralInputs", service)
        self.assertIn("PrewarmConfigurationBlocker(settings, globals::game::isVR)", service)
        self.assertNotIn("IsDeveloperMode", service)
        self.assertNotIn("GetLogLevel", service)
        status = schema["outputSchema"]["properties"]["neuralRendering"]["properties"]["prewarmConfiguration"]
        self.assertEqual(status["properties"]["enabled"]["type"], "boolean")
        settings = read(ROOT / "src/Features/Upscaling.h")
        for field in ("neuralRenderingPrewarmEnabled", "neuralRenderingParallelEyes"):
            self.assertIn("bool " + field + " = true;", settings)
        self.assertIn("bool neuralCharacterPackedCrops = false;", settings)
        self.assertIs(schema["inputSchema"]["properties"]["characterPackedCrops"]["default"], False)

    def test_projection_uses_locked_effective_policy_without_live_progress(self):
        source = read(NR / "Renderer.cpp")
        owner = body(source, "bool Renderer::ServicePrewarm(")
        self.assertLess(owner.index("state_->mutex_"), owner.index("ServicePrewarmLocked("))
        projection = body(source, "std::optional<PrewarmKey> Renderer::State::ProjectPrewarmLocked(")
        self.assertIn("auto resolution = memoryResolution_", projection)
        self.assertIn("resolution.SetEnabled(request.pressureResolutionEnabled)", projection)
        self.assertIn("resolution.EffectivePercent(request.modelResolutionPercent)", projection)
        for guard in ("MemoryRecoveryPhase::Ready", "memoryConservation_.active",
                      "memoryRetirement_.backendRetained", "resolutionRestoration_.active",
                      "peripheralExperiment_.Status()", "CompactInputsEnabled()"):
            self.assertIn(guard, projection)
        for forbidden in ("PrepareMemoryResolutionLocked(", "Resolve(", "ObserveSuccess(",
                          "resolutionLatches_", "CaptureColorConfiguration("):
            self.assertNotIn(forbidden, projection)
        geometry = body(read(NR / "ModelResolution.cpp"),
                        "std::optional<PrewarmKey> ModelResolution::ProjectPrewarm(")
        self.assertIn("PlanPeripheralCompression(args, false)", geometry)
        self.assertIn("Project(input, projected)", geometry)
        for forbidden in ("PreparePeripheralCompression(", "Prepare(", ".Execute("):
            self.assertNotIn(forbidden, geometry)
        service = body(source, "void Renderer::State::ServicePrewarmLocked(")
        self.assertLess(service.index("ProjectPrewarmLocked("), service.index("const bool changed"))
        self.assertIn("Color::Work::AllocationExtent(", service)
        self.assertIn("PrewarmTransportBytes(", service)
        self.assertIn("sample.priorityWork", service)


if __name__ == "__main__":
    unittest.main()
