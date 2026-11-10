"""Verify the NR startup boundary with stereo publication and shared reservations."""

from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
NR = ROOT / "src/Features/Upscaling/NeuralRendering"
RENDERER = (NR / "Renderer.cpp").read_text()
UPSCALING = (ROOT / "src/Features/Upscaling.cpp").read_text()
BRIDGE = (ROOT / "src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp").read_text()


def function(source, signature):
    start = source.index(signature)
    indent = source[source.rfind("\n", 0, start) + 1:start]
    return source[start:source.index("\n" + indent + "}", start)]


class NativeStartupContracts(unittest.TestCase):
    def test_startup_cannot_publish_one_eye(self):
        batch = function(RENDERER, "bool Renderer::State::ApplyRegionBatchLocked(")
        self.assertIn("if (startupIndex && *startupIndex != index)", batch)
        self.assertIn("interop_.EndD3D12(!startupIndex)", batch)
        deferred = batch.index("nativeStartupDeferred_ = true")
        output = batch.index("activeStage_ = RendererStage::OutputCommit", deferred)
        self.assertIn("return false;", batch[deferred:output])
        self.assertIn("nativeStartup_.issued = fences.issued", batch[:deferred])
        sequential = function(RENDERER, "bool Renderer::State::ApplySequentialStereoLocked(")
        self.assertIn("nativeStartup_.active || NeedsNativeStartupLocked(a_args, resources)", sequential)
        self.assertIn("return ApplyBatchLocked(a_args, a_outcome, a_memoryAdmitted)", sequential)
        self.assertIn("outcome.evaluationAttemptedFeatureSlotMask != 0 && !outcome.startupDeferred", UPSCALING)
        self.assertIn("NeuralRendering::IsCompleteNeuralImageResult(summary.successfulEyeMask, summary.bypassedEyeMask, globals::game::isVR)", UPSCALING)
        self.assertIn("a_outcome.spatialHistoryReset |= slots[index]->nativeUnpublished", batch)
        startup = function(RENDERER, "bool Renderer::State::NeedsNativeStartupLocked(")
        self.assertIn("slot.nativeUnpublished", startup)

    def test_private_reuse_and_retirement_poll_before_allocating(self):
        admission = function(RENDERER, "bool Renderer::State::AdmitMemoryLocked(")
        self.assertLess(admission.index("PollNativeStartupLocked()"), admission.index("EstimateAdditionalMemoryLocked("))
        retirement = function(RENDERER, "bool Renderer::State::RetireMemoryPressureLocked(")
        self.assertLess(retirement.index("PollNativeStartupLocked()"), retirement.index("TeardownBackendLocked("))
        poll = function(RENDERER, "bool Renderer::State::PollNativeStartupLocked(")
        for forbidden in ("WaitForIdle(", "Flush(", "WaitForSingleObject(", "BeginD3D12("):
            self.assertNotIn(forbidden, poll)
        self.assertLess(poll.index("nativeStartup_.Completed(fences)"), poll.index("slot.nativeReady = true"))
        self.assertIn("RemainingReservation(memoryReservation_.Bytes(), fences, sample", poll)
        self.assertLess(poll.index("RemainingReservation("), poll.index("nativeStartup_ = {}"))
        maintenance = function(RENDERER, "void Renderer::ServiceBackendMaintenance(")
        self.assertIn("PollNativeStartupLocked()", maintenance)
        self.assertIn("NativeMemoryPolicy::kIdleReservationMs", maintenance)

    def test_render_scale_and_output_tickets_remain_reserved(self):
        output = function(RENDERER, "Util::GpuMemoryBudget::Reservation Renderer::ReserveOutputAllocation(")
        self.assertIn("const auto pending = shared.pendingBytes - reservation.Bytes()", output)
        self.assertIn("CanAllocateOutput(sample, bytes, now, pending)", output)
        estimate = function(RENDERER, "std::optional<std::uint64_t> Renderer::State::EstimateAdditionalMemoryLocked(")
        self.assertIn("!colorConfiguration_.experiments.transportBypass && (replacing || !slot.nativeReady)", estimate)
        finish = function(RENDERER, "void FinishMemoryAdmissionLocked()")
        self.assertLess(finish.index("nativeStartupMask_"), finish.index("memoryReservation_.Reset()"))
        projection = function(RENDERER, "void Renderer::State::PlanCheaperResolutionLocked(")
        self.assertIn("candidate && *candidate < *baseline", projection)
        self.assertIn("modelResolution_.PlanPackedCrops(view, true)", projection)
        self.assertIn("modelResolution_.PlanPeripheralCompression(view, compact)", projection)
        self.assertIn("ProjectModelResourcesLocked", projection)
        self.assertIn("ModelResolutionSharingLocked(view)", projection)
        model = function(RENDERER, "bool Renderer::State::ApplyModelResolutionLocked(")
        self.assertIn("ModelResolutionSharingLocked(args)", model)
        sharing = function(RENDERER, "ModelResolution::Sharing Renderer::State::ModelResolutionSharingLocked(")
        self.assertIn("!stableBackend || !ModelResolution::CanUseSharedTargets", sharing)
        for forbidden in ("SetRenderScale", "CreateTexture", "Dispatch(", "Commit("):
            self.assertNotIn(forbidden, projection)

    def test_diagnostics_explicitly_describe_uncertainty(self):
        descriptor = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', BRIDGE, re.S).group(1))
        text = json.dumps(descriptor)
        self.assertIn("nativeStartup", text)
        self.assertIn("process-wide", text)
        self.assertIn("exactNativeAllocationBytes", text)
        self.assertNotIn("kNativeFeatureReserve", RENDERER)
        header = (NR / "Renderer.h").read_text()
        block = header[:header.index("NativeStartupObservation nativeStartup")]
        self.assertGreater(block.rfind("#ifdef DEVBENCH_BRIDGE_ENABLED"), block.rfind("#endif"))


if __name__ == "__main__":
    unittest.main()
