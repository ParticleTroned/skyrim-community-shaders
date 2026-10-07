"""Source contracts for memory recovery; never invoke a compiler or game."""

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
NR = ROOT / "src/Features/Upscaling/NeuralRendering"
RENDERER = (NR / "Renderer.cpp").read_text()
PIPELINE = (NR / "ColorPipeline.cpp").read_text()
BRIDGE = (ROOT / "src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp").read_text()


def body(source, signature, indent="\t"):
    start = source.index(signature)
    return source[start:source.index("\n" + indent + "}", start)]


class MemoryRecoveryContracts(unittest.TestCase):
    def test_completion_follows_the_entire_transaction(self):
        for entry in ("Apply", "ApplyStereo", "ApplySequentialStereo"):
            source = body(RENDERER, f"bool Renderer::{entry}(")
            self.assertLess(source.index(f"state_->{entry}Locked("), source.index("FinishMemoryRecoveryLocked(succeeded, outcome)"))
            self.assertLess(source.index("FinishMemoryRecoveryLocked(succeeded, outcome)"), source.index("state_->FinishCapture("))
        per_slot = body(RENDERER, "void Renderer::State::SucceedLocked(")
        self.assertFalse("memoryRecovery_.Succeeded(" in per_slot)

    def test_pair_admission_is_scoped_and_fallback_rechecks(self):
        source = body(RENDERER, "bool Renderer::State::ApplySequentialStereoLocked(")
        self.assertLess(source.index("ValidateLocked("), source.index("AdmitMemoryLocked("))
        self.assertLess(source.index("AdmitMemoryLocked("), source.index("leftOutcome, true)"))
        self.assertTrue("rightOutcome, true)" in source)
        source = body(RENDERER, "bool Renderer::State::ApplyBatchLocked(")
        fallback = source[source.index("const auto fallback ="):source.index("capacityFallback_.ApplyBatch(")]
        self.assertTrue("ApplyRegionBatchLocked(args, outcome)" in fallback)
        self.assertTrue("memoryRecovery_.phase == MemoryRecoveryPhase::Retiring" in source)
        self.assertTrue("memoryRecovery_.phase == MemoryRecoveryPhase::Waiting" in source)
        self.assertFalse("memoryRecovery_.phase != MemoryRecoveryPhase::Ready" in source)
        self.assertFalse("memoryFrameKey_" in RENDERER)

    def test_retirement_keeps_hard_failure_guards(self):
        source = body(RENDERER, "bool Renderer::State::RetireMemoryPressureLocked(")
        self.assertTrue("failureLatched_ || quarantined_ || !TeardownBackendLocked(" in source)
        self.assertLess(source.index("TeardownBackendLocked("), source.index("memoryRecovery_.Retired()"))
        source = body(RENDERER, "bool Renderer::State::FailLocked(")
        self.assertLess(source.index("deviceRemoved || a_forceQuarantine"), source.index("memoryRecovery_.Suspend("))
        source = body(RENDERER, "bool Renderer::State::TeardownBackendLocked(")
        self.assertLess(source.index("interop_.WaitForIdle("), source.index("runtime.ResetFeatures()"))
        self.assertLess(source.index("runtime.ResetFeatures()"), source.index("slots_ = {}"))

    def test_colour_allocation_preserves_errors_and_one_sizing_rule(self):
        allocation = body(PIPELINE, "bool CreateTexture(", "\t\t")
        for operation in ("CreateTexture2D", "CreateShaderResourceView", "CreateUnorderedAccessView"):
            self.assertTrue(f"result = device->{operation}(" in allocation)
        self.assertTrue("return SUCCEEDED(result)" in allocation)
        ensure = body(PIPELINE, "bool Pipeline::Ensure(")
        self.assertTrue("Work::AllocationExtent(roi)" in ensure)
        self.assertTrue("work.Fits(roi, format)" in ensure)
        self.assertTrue("format, false, allocationResult)" in ensure)
        self.assertTrue("format, true, allocationResult)" in ensure)
        self.assertTrue("RendererStage::ResourceCreation, colorResult" in RENDERER)
        estimate = body(RENDERER, "std::optional<std::uint64_t> Renderer::State::EstimateAdditionalMemoryLocked(")
        self.assertTrue("replacingBackend || !slot.resourcesValid" in estimate)
        self.assertTrue("Color::Work::AllocationExtent(roi)" in estimate)
        self.assertTrue("slot.colorWork.Fits(roi, resource.output.desc.Format)" in estimate)

    def test_device_change_and_reset_invalidate_monitoring(self):
        source = body(RENDERER, "MemoryBudgetSample Renderer::State::SampleMemoryBudgetLocked(")
        self.assertLess(source.index("memorySampleDevice_.Get() != a_device"), source.index("QueryVideoMemoryInfo("))
        self.assertTrue("memoryRecovery_.ClearHealthyWindow()" in source)
        source = body(RENDERER, "bool Renderer::State::ResetLocked(")
        for expression in ("memorySampleDevice_.Reset()", "memoryAdapter_.Reset()", "memorySample_ = {}", "simulatedPressureUntilMs_ = 0"):
            self.assertTrue(expression in source)
        source = body(RENDERER, "bool Renderer::SimulateMemoryPressure(")
        self.assertTrue("a_durationMilliseconds && (state_->quarantined_ || state_->failureLatched_)" in source)

    def test_readiness_cannot_qualify_recovery_baseline_as_ready(self):
        source = BRIDGE.split('if (action == "nr_readiness") {', 1)[1].split('if (action == "foveation_configure") {', 1)[0]
        guard = source.index("if (upscaling.IsNeuralRenderingRequested())")
        self.assertLess(guard, source.index('"nr_memory_recovery_pending"'))
        self.assertLess(guard, source.index('"nr_backend_faulted"'))
        self.assertTrue("renderer.memoryRecovery.phase == NeuralRendering::MemoryRecoveryPhase::Ready" in source)

    def test_action_is_registered_and_schema_scopes_injection(self):
        descriptor = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', BRIDGE, re.S).group(1))
        schema = descriptor["inputSchema"]
        self.assertIn("nr_memory_recovery", schema["properties"]["action"]["enum"])
        self.assertEqual(schema["properties"]["durationMilliseconds"]["maximum"], 30000)
        scoped = next(rule for rule in schema["allOf"] if rule["if"].get("properties", {}).get("action", {}).get("const") == "nr_memory_recovery")
        self.assertEqual(set(scoped["then"]["propertyNames"]["enum"]), {"action", "expectedBuildId", "durationMilliseconds"})
        injection = next(rule for rule in schema["allOf"] if rule["if"] == {"required": ["durationMilliseconds"]})
        self.assertEqual(injection["then"]["properties"]["action"]["const"], "nr_memory_recovery")
        enum = BRIDGE.split('result["inputSchema"]["properties"]["action"]["enum"] = {', 1)[1].split("};", 1)[0]
        self.assertTrue('"nr_memory_recovery"' in enum)
        handler = BRIDGE.split('if (action == "nr_memory_recovery") {', 1)[1].split('if (action == "nr_reset") {', 1)[0]
        self.assertTrue("TryGetNonNegativeInteger(" in handler)
        self.assertLess(handler.index("RunWithRendererOwnership("), handler.index("SimulateMemoryPressure("))
        self.assertTrue('{ "mutationApplied", applied }' in handler)


if __name__ == "__main__":
    unittest.main()
