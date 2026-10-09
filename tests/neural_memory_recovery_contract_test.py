"""Source contracts for memory recovery; never invoke a compiler or game."""

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
NR = ROOT / "src/Features/Upscaling/NeuralRendering"
RENDERER = (NR / "Renderer.cpp").read_text()
PIPELINE = (NR / "ColorPipeline.cpp").read_text()
STREAMLINE = (ROOT / "src/Features/Upscaling/Streamline.cpp").read_text()
UPSCALING = (ROOT / "src/Features/Upscaling.cpp").read_text()
BRIDGE = (ROOT / "src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp").read_text()


def read_descriptor():
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate NR descriptor key: {key}")
            result[key] = value
        return result
    return json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', BRIDGE, re.S).group(1),
                      object_pairs_hook=unique)


def body(source, signature, indent="\t"):
    start = source.index(signature)
    return source[start:source.index("\n" + indent + "}", start)]


class MemoryRecoveryContracts(unittest.TestCase):
    def test_conservation_retires_before_allocating_and_preserves_stereo(self):
        source = body(RENDERER, "bool Renderer::State::AdmitMemoryLocked(")
        self.assertLess(source.index("memoryConservation_.Update("), source.index("ReclaimMemoryLocked("))
        self.assertLess(source.index("ReclaimMemoryLocked("), source.index("memoryRecovery_.Admit("))
        after_reclaim = source.split("if (reclaimed)", 1)[1]
        self.assertLess(after_reclaim.index("EstimateAdditionalMemoryLocked("), after_reclaim.index("memoryRecovery_.Admit("))
        self.assertIn("SampleMemoryBudgetLocked(first.device, nowMs, true)", after_reclaim)
        reclaim = body(RENDERER, "bool Renderer::State::ReclaimMemoryLocked(")
        self.assertIn("FeatureSlotBit(slotIndex) | FeatureSlotBit(slotIndex ^ 1u)", reclaim)
        self.assertIn("MemoryConservationPolicy::IsInactive(slot.lastUsedMs, nowMs)", reclaim)
        self.assertIn("IsSequentialFrame(slot.lastSuccessfulFrame, args.front().frameId)", reclaim)
        self.assertIn("memoryConservation_.CanTrimColorBuffers(slotIndex)", reclaim)
        self.assertIn("memoryConservation_.RecordColorTrim(index, trimmedBytes[index])", reclaim)
        slot = RENDERER.split("struct Slot\n", 1)[1].split("struct ValidatedResources", 1)[0]
        self.assertNotIn("colorTrimEpoch", slot)
        self.assertLess(reclaim.index("interop_.WaitForIdle("), reclaim.index("slot.colorWork.baseline = {}"))
        self.assertNotIn("historyValid = false", reclaim)
        retire = body(RENDERER, "bool Renderer::State::RetireSlotLocked(")
        self.assertLess(retire.index("interop_.WaitForIdle("), retire.index("colorPipeline_.Poll("))
        self.assertLess(retire.index("colorPipeline_.Poll("), retire.index("ResetFeature("))
        self.assertLess(retire.index("SetActiveFeatureSlotLocked(a_slot)"), retire.index("interop_.WaitForIdle("))
        self.assertLess(retire.index("DropMeasurement()"), retire.index("slot = {}"))
        self.assertIn("#ifdef DEVBENCH_BRIDGE_ENABLED\n\t\tfor (const auto& readback : slot.colorWork.readbacks)", retire)
        self.assertLess(retire.index("ResetFeature("), retire.index("slot = {}"))
        ensure = body(RENDERER, "bool Renderer::State::EnsureSlotLocked(")
        self.assertIn("RetireSlotLocked(a_slot, a_evidence)", ensure)

    def test_conservation_output_is_typed_and_unknown_bytes_are_explicit(self):
        descriptor = read_descriptor()
        output = descriptor["outputSchema"]["properties"]["neuralRendering"]["properties"]["renderer"]["properties"]["memoryConservation"]
        self.assertEqual(output["properties"]["active"]["type"], "boolean")
        self.assertEqual(output["properties"]["reclaimedLogicalBytes"]["type"], ["integer", "null"])
        self.assertEqual(set(output["required"]), set(output["properties"]))
        serialization = body(BRIDGE, "json NeuralMemoryConservationJson(")
        self.assertIn("policy.reclaimedLogicalBytesKnown ? json(policy.reclaimedLogicalBytes) : json(nullptr)", serialization)
        fields = set(re.findall(r'\{ "(\w+)",', serialization))
        self.assertEqual(fields, set(output["properties"]))

    def test_recovery_output_matches_typed_serializer(self):
        descriptor = read_descriptor()
        output = descriptor["outputSchema"]["properties"]["neuralRendering"]["properties"]["renderer"]["properties"]["memoryRecovery"]
        properties = output["properties"]
        self.assertEqual(set(properties["phase"]["enum"]), {"ready", "retiring", "waiting_for_headroom", "rebuilding", "probation"})
        self.assertEqual(set(output["required"]), set(properties))
        serialization = body(BRIDGE, "json NeuralMemoryRecoveryJson(")
        fields = set(re.findall(r'\{ "(\w+)",', serialization))
        self.assertEqual(fields, set(properties))
        for field in ("pendingAllocationBytes", "admissionDemandBytes", "observedRecoveryDemandBytes",
                      "rapidRelapses", "rebuildAttempts", "demandProbes", "headroomHealthyMilliseconds",
                      "activeHealthyMilliseconds", "minimumOffMilliseconds", "stableHeadroomMilliseconds",
                      "probationMilliseconds", "retryResetActiveMilliseconds"):
            self.assertEqual(properties[field]["type"], "integer")
            self.assertEqual(properties[field]["minimum"], 0)
        self.assertEqual(properties["failedAdmissionBarrier"]["type"], "boolean")
        self.assertEqual(properties["nativeAllocationBytes"]["type"], "null")
        self.assertIn("Policy::kProbationMs", serialization)
        self.assertIn("Policy::kRetryResetMs", serialization)
        self.assertNotIn("Repeated pressure increases retry delays up to ten seconds", descriptor["description"])
        self.assertIn("menus and observation gaps do not count", descriptor["description"])

    def test_pressure_resolution_setting_is_mutable_and_distinct_from_execution(self):
        descriptor = read_descriptor()
        schema = descriptor["inputSchema"]
        setting = schema["properties"]["pressureResolutionEnabled"]
        self.assertEqual(setting["type"], "boolean")
        self.assertFalse(setting["default"])
        configure = next(rule for rule in schema["allOf"] if rule["if"].get("properties", {}).get("action", {}).get("const") == "nr_configure")
        self.assertIn("pressureResolutionEnabled", configure["then"]["propertyNames"]["enum"])
        handler = BRIDGE.split('if (action == "nr_configure") {', 1)[1]
        predicate = handler.split("const bool settingsChanged =", 1)[1].split("if (!settingsChanged)", 1)[0]
        self.assertIn("previousSettings.neuralRenderingPressureResolutionEnabled != requestedSettings.neuralRenderingPressureResolutionEnabled", predicate)
        self.assertIn('parseBoolean("pressureResolutionEnabled", a_request.pressureResolutionEnabled)', BRIDGE)
        self.assertIn("requestedSettings.neuralRenderingPressureResolutionEnabled = *request.pressureResolutionEnabled", handler)
        model = descriptor["outputSchema"]["properties"]["neuralRendering"]["properties"]["modelResolution"]["properties"]
        self.assertEqual(model["pressureResolutionEnabled"]["type"], "boolean")
        self.assertEqual(model["effectivePercent"]["type"], "integer")
        self.assertEqual(model["effectivePercent"]["minimum"], 30)
        self.assertEqual(model["effectivePercent"]["maximum"], 100)
        self.assertIn('{ "pressureResolutionEnabled", a_upscaling.settings.neuralRenderingPressureResolutionEnabled }', BRIDGE)
        self.assertIn('{ "effectivePercent", snapshot.effectiveModelResolutionPercent }', BRIDGE)
        self.assertIn('snapshot.outputCommitted ? json(snapshot.modelResolutionPercent) : json(nullptr)', BRIDGE)

    def test_restoration_preflights_before_retirement_and_retries_working_output(self):
        admission = body(RENDERER, "bool Renderer::State::AdmitMemoryLocked(")
        preview = admission.index("auto preview = memoryRecovery_")
        self.assertLess(admission.index("EstimateAdditionalMemoryLocked("), preview)
        self.assertLess(admission.index("SampleMemoryBudgetLocked("), preview)
        self.assertLess(preview, admission.index("memoryConservation_.Update("))
        self.assertLess(preview, admission.index("ReclaimMemoryLocked("))
        self.assertLess(preview, admission.index("memoryRecovery_.InvalidateCapacityLearning("))
        preflight = admission[preview:admission.index("memoryConservation_.Update(")]
        self.assertIn("!sample.IsFresh(nowMs)", preflight)
        self.assertIn("preview.Admit(sample, *additionalBytes, nowMs, pendingBytes)", preflight)
        self.assertIn("resolutionRestoration_.rejected = true", preflight)
        self.assertNotIn("memoryRecovery_.Admit(", preflight)
        self.assertNotIn("Suspend(", preflight)
        self.assertNotIn("TeardownBackendLocked(", preflight)
        for entry in ("Apply", "ApplyStereo", "ApplySequentialStereo"):
            transaction = body(RENDERER, f"bool Renderer::{entry}(")
            self.assertEqual(transaction.count("RetryWorkingResolutionLocked("), 1)
            self.assertEqual(transaction.count("FinishMemoryRecoveryLocked("), 1)
            self.assertEqual(transaction.count("FinishMemoryResolutionLocked("), 1)
            self.assertLess(transaction.index("RetryWorkingResolutionLocked("), transaction.index("FinishMemoryRecoveryLocked("))
            preparation = body(RENDERER, f"bool Renderer::State::{entry}Locked(")
            self.assertLess(preparation.index("!resolutionRestoration_.active"), preparation.index("ReleaseUnscaledSlots("))
        retry = body(RENDERER, "bool Renderer::State::RetryWorkingResolutionLocked(")
        self.assertIn("memoryResolution_.RejectRestoration(workingPercent)", retry)
        self.assertIn("arg.modelResolutionPercent = workingPercent", retry)
        self.assertNotIn("TeardownBackendLocked(", retry)
        self.assertNotIn("memoryRecovery_.InvalidateCapacityLearning(", retry)
        completed = body(RENDERER, "void Renderer::State::FinishMemoryResolutionLocked(")
        self.assertIn("successfulResolutionPercents_[arg.featureSlot] = arg.modelResolutionPercent", completed)
        prepare = body(RENDERER, "void Renderer::State::PrepareMemoryResolutionLocked(")
        self.assertIn("arg.featureSlot >= Runtime::kFeatureSlotCount", prepare)
        self.assertIn("workingPercent = std::min(workingPercent, previous)", prepare)
        self.assertIn("memoryResolution_.ClearHealthyWindow()", prepare)
        self.assertNotIn("arg.reset", prepare)
        finish = body(RENDERER, "void Renderer::State::FinishMemoryRecoveryLocked(")
        proposal = finish[finish.index("const auto previousCeiling"):finish.index("} else {")]
        self.assertIn("restorationPreviousPercent_ = previousCeiling", proposal)
        self.assertNotIn("ResolutionChangedLocked(", proposal)

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
        self.assertLess(source.index("TeardownBackendLocked("), source.index("memoryRecovery_.Retired(now)"))
        source = body(RENDERER, "bool Renderer::State::FailLocked(")
        self.assertLess(source.index("deviceRemoved || a_forceQuarantine"), source.index("memoryRecovery_.Suspend("))
        source = body(RENDERER, "bool Renderer::State::TeardownBackendLocked(")
        self.assertLess(source.index("interop_.WaitForIdle("), source.index("runtime.ResetFeatures()"))
        self.assertLess(source.index("runtime.ResetFeatures()"), source.index("slots_ = {}"))

    def test_warm_retirement_status_matches_descriptor(self):
        nr = read_descriptor()["outputSchema"]["properties"]["neuralRendering"]["properties"]
        renderer = nr["renderer"]["properties"]
        output = renderer["memoryRetirement"]
        fields = set(re.findall(r'\{ "(\w+)",', body(BRIDGE, "json NeuralMemoryRetirementJson(")))
        self.assertEqual(fields, set(output["properties"]))
        self.assertEqual(set(output["required"]), fields)
        self.assertEqual(renderer["memoryRecovery"]["properties"]["startupKind"]["enum"], ["cold", "warm"])
        self.assertIn("modelResolution", nr)
        self.assertIn("characterRendering", nr)

    def test_warm_retirement_preserves_drains_and_healthy_ownership(self):
        teardown = body(RENDERER, "bool Renderer::State::TeardownBackendLocked(")
        for guard in ("!a_resetShader", "!a_destruction", "!failureLatched_", "!quarantined_",
                      "runtimeReady_", "interop_.IsInitialized()", "RuntimeStatus::Initialized",
                      "SUCCEEDED(device_->GetDeviceRemovedReason())"):
            self.assertIn(guard, teardown)
        self.assertLess(teardown.index("interop_.WaitForIdle("), teardown.index("runtime.ReleasePassResources()"))
        self.assertLess(teardown.index("runtime.ReleasePassResources()"), teardown.index("slots_ = {}"))
        self.assertIn("if (!retainBackend && runtimeTouched_ && !runtime.Shutdown())", teardown)
        self.assertIn("if (!retainBackend && interop_.IsInitialized() && !interop_.Shutdown(a_evidence))", teardown)
        self.assertIn("memoryRecovery_.SetStartupKind(retainBackend ? MemoryStartupKind::Warm : MemoryStartupKind::Cold)", teardown)
        self.assertIn("colorPipeline_.Reset()", teardown)
        self.assertIn("modelResolution_.Reset()", teardown)
        self.assertIn("alreadyRetained = retainBackend && memoryRetirement_.backendRetained", teardown)
        self.assertIn("!alreadyRetained && interop_.IsInitialized() && !interop_.WaitForIdle", teardown)
        self.assertIn("!alreadyRetained && runtimeReady_", teardown)
        admission = body(RENDERER, "bool Renderer::State::AdmitMemoryLocked(")
        self.assertLess(admission.index("memoryRecovery_.Admit("), admission.index("if (admitted && memoryRetirement_.backendRetained)"))
        self.assertIn("memoryRetirement_.Released()", admission)
        service = body(RENDERER, "bool Renderer::State::ServiceRetainedBackendLocked(")
        self.assertLess(service.index("now < nextRetainedBackendCheckMs_"), service.index("SampleMemoryBudgetLocked("))
        self.assertLess(service.index("reason == MemoryRetirementReason::None"), service.index("TeardownBackendLocked("))
        self.assertIn("memoryRecovery_.ResourcesRetired(", service)
        self.assertIn("FinishMemoryAdmissionLocked()", service)
        self.assertIn("a_requested && memoryRecovery_.phase == MemoryRecoveryPhase::Waiting", service)
        self.assertIn("if (!recovering)", service)
        self.assertIn("preview.phase = MemoryRecoveryPhase::Ready", service)
        self.assertIn("SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0)", service)
        self.assertNotIn("memoryResolution_.OnRetired", service)
        self.assertNotIn("memoryRecovery_.Suspend", service)
        maintenance = body(RENDERER, "void Renderer::ServiceRetainedBackend(")
        self.assertLess(maintenance.index("retainedBackendPending_.load"), maintenance.index("std::unique_lock"))
        self.assertIn("std::try_to_lock", maintenance)
        self.assertLess(maintenance.index("retainedBackendPending_.load"), maintenance.index("Util::RendererOwnership owner"))
        self.assertLess(maintenance.index("Util::RendererOwnership owner"), maintenance.index("std::unique_lock"))
        self.assertLess(maintenance.index("if (!owner)"), maintenance.index("state_->ServiceRetainedBackendLocked("))
        frame = body(UPSCALING, "void Upscaling::Reset()", "")
        self.assertLess(frame.index("ServiceRetainedBackend(settings.neuralRenderingEnabled)"), frame.index("!globals::game::isVR"))
        reset = body(RENDERER, "bool Renderer::Reset(")
        self.assertIn("state_->ResetLocked(false, false, a_policy)", reset)

    def test_dlss_warning_is_gated_and_defers_gpu_retirement(self):
        warning = STREAMLINE.split("if (evalResult == sl::Result::eWarnOutOfVRAM) {", 1)[1].split("if (!evaluationSucceeded)", 1)[0]
        self.assertLess(warning.index("IsNeuralRenderingRequested()"), warning.index("NotifyDlssMemoryPressure()"))
        self.assertLess(warning.index("NotifyDlssMemoryPressure()"), warning.index("ShouldLog("))
        source = body(RENDERER, "bool Renderer::NotifyDlssMemoryPressure(")
        self.assertLess(source.index("state_->failureLatched_ || state_->quarantined_"), source.index("ReportDlssWarning("))
        self.assertNotIn("TeardownBackendLocked(", source)
        self.assertNotIn("RetireMemoryPressureLocked(", source)
        self.assertIn("state_->snapshot_.memoryRecovery = state_->memoryRecovery_", source)
        ui = UPSCALING.split('Neural Rendering cannot recover safely in this session.', 1)[1].split('if ((status.failureLatched || status.quarantined)', 1)[0]
        self.assertIn("else if (status.failureLatched)", ui)
        self.assertIn("else if (IsNeuralRenderingRequested()", ui)
        self.assertIn("IsNeuralRenderingRequested() && status.memoryConservation.active", ui)
        self.assertIn("MemoryRecoveryPhase::Retiring", ui)
        self.assertIn("MemoryRecoveryPhase::Waiting", ui)

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
        self.assertLess(source.index("memorySampleDevice_.Get() != a_device"), source.index("Util::GpuMemoryBudget::Get().Sample("))
        self.assertTrue("memoryRecovery_.ClearHealthyWindow()" in source)
        source = body(RENDERER, "bool Renderer::State::ResetLocked(")
        for expression in ("memoryRecovery_.ResourcesRetired(now)", "memoryReservation_.Reset()", "memorySample_ = {}", "simulatedPressureUntilMs_ = 0"):
            self.assertTrue(expression in source)
        source = body(RENDERER, "bool Renderer::SimulateMemoryPressure(")
        self.assertTrue("a_durationMilliseconds && (state_->quarantined_ || state_->failureLatched_)" in source)

    def test_reset_preserves_safe_pressure_retirement_and_disabled_hot_path(self):
        source = body(RENDERER, "bool Renderer::State::ResetLocked(")
        self.assertLess(source.index("TeardownBackendLocked("), source.index("memoryResolution_.OnRetired("))
        self.assertIn("pressureRetirement = memoryRecovery_.phase == MemoryRecoveryPhase::Retiring", source)
        self.assertIn("if (pressureRetirement) {", source)
        self.assertIn("else if (memoryRecovery_.phase == MemoryRecoveryPhase::Waiting)", source)
        self.assertIn("memoryResolution_.MarkResourcesRetired(now)", source)
        finish = body(RENDERER, "void Renderer::State::FinishMemoryRecoveryLocked(")
        self.assertIn("memoryResolution_.enabled && memoryResolution_.ObserveSuccess", finish)

    def test_faulted_backend_does_not_reserve_future_streaming_work(self):
        finish = body(RENDERER, "void FinishMemoryAdmissionLocked()", "\t\t")
        self.assertIn("if (failureLatched_ || quarantined_)", finish)
        guard = finish.split("if (failureLatched_ || quarantined_)", 1)[1].split("const auto phase", 1)[0]
        self.assertIn("SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0)", guard)
        self.assertIn("return;", guard)
        for signature in ("bool Renderer::State::FailLocked(", "void Renderer::State::QuarantineAfterUnexpectedFailureLocked("):
            failure = body(RENDERER, signature)
            self.assertIn("SetOwnerWork(Util::GpuMemoryBudget::Owner::NeuralRendering, false, 0)", failure)

    def test_readiness_cannot_qualify_recovery_baseline_as_ready(self):
        source = BRIDGE.split('if (action == "nr_readiness") {', 1)[1].split('if (action == "foveation_configure") {', 1)[0]
        guard = source.index("if (upscaling.IsNeuralRenderingRequested())")
        self.assertLess(guard, source.index('"nr_memory_recovery_pending"'))
        self.assertLess(guard, source.index('"nr_backend_faulted"'))
        self.assertTrue("renderer.memoryRecovery.phase == NeuralRendering::MemoryRecoveryPhase::Ready" in source)

    def test_action_is_registered_and_schema_scopes_injection(self):
        descriptor = read_descriptor()
        schema = descriptor["inputSchema"]
        self.assertIn("nr_memory_recovery", schema["properties"]["action"]["enum"])
        self.assertEqual(schema["properties"]["durationMilliseconds"]["maximum"], 30000)
        scoped = next(rule for rule in schema["allOf"] if rule["if"].get("properties", {}).get("action", {}).get("const") == "nr_memory_recovery")
        self.assertEqual(set(scoped["then"]["propertyNames"]["enum"]), {"action", "expectedBuildId", "durationMilliseconds", "simulateDlssWarning", "conservationOnly"})
        injection = next(rule for rule in schema["allOf"] if rule["if"] == {"required": ["durationMilliseconds"]})
        self.assertEqual(injection["then"]["properties"]["action"]["const"], "nr_memory_recovery")
        enum = BRIDGE.split('result["inputSchema"]["properties"]["action"]["enum"] = {', 1)[1].split("};", 1)[0]
        self.assertTrue('"nr_memory_recovery"' in enum)
        handler = BRIDGE.split('if (action == "nr_memory_recovery") {', 1)[1].split('if (action == "nr_reset") {', 1)[0]
        self.assertTrue("TryGetNonNegativeInteger(" in handler)
        self.assertLess(handler.index("RunWithRendererOwnership("), handler.index("SimulateMemoryPressure("))
        self.assertTrue('{ "mutationApplied", applied }' in handler)
        self.assertEqual(schema["properties"]["conservationOnly"]["const"], True)
        conservation = next(rule for rule in schema["allOf"] if rule["if"] == {"required": ["conservationOnly"]})
        self.assertEqual(conservation["then"]["required"], ["durationMilliseconds"])
        self.assertIn('"conservationOnly must be true and requires durationMilliseconds"', handler)
        self.assertIn("SimulateMemoryPressure(durationMs, conservationOnly)", handler)
        self.assertEqual(schema["properties"]["simulateDlssWarning"]["const"], True)
        self.assertEqual(injection["then"]["not"], {"required": ["simulateDlssWarning"]})
        warning = handler.split('if (a_args.contains("simulateDlssWarning"))', 1)[1].split('if (!a_args.contains("durationMilliseconds"))', 1)[0]
        self.assertIn('!warning.is_boolean() || !warning.get<bool>() || a_args.contains("durationMilliseconds")', warning)
        self.assertLess(warning.index("RunWithRendererOwnership("), warning.index("NotifyDlssMemoryPressure()"))
        self.assertLess(warning.index("IsNeuralRenderingRequested()"), warning.index("NotifyDlssMemoryPressure()"))


if __name__ == "__main__":
    unittest.main()
