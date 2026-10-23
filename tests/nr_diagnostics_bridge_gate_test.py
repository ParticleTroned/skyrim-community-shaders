"""Prove native preprocessing removes NR capture machinery and its storage."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
empty_units = ["src/Features/Upscaling/NeuralRendering/ExposureCapture.cpp",
               "src/Features/ScreenshotNeuralDiagnostics.cpp"]
checks = {
    "src/Features/Upscaling/NeuralRendering/GuidedReconstructionPolicy.h": ["GuidedReconstructionOverride", "GuidedReconstructionExperiment"],
    "src/Features/Upscaling/NeuralRendering/ExecutionEvidenceJson.h": ["ModelReconstructionJson"],
    "src/Features/Upscaling/NeuralRendering/ConfigurationSerialization.h": ["ReconstructionComparisonRequest", "ParseReconstructionComparison"],
    "src/Features/Upscaling/NeuralRendering/PackedCropDiagnostics.h": ["PackedCropDiagnostics", "PackedCropObservation", "PackedCropRebuildTimings"],
    "src/Features/Upscaling/NeuralRendering/ModelResolution.h": ["cropDiagnostics_", "CropDiagnostics"],
    "src/Features/Upscaling/NeuralRendering/ModelResolution.cpp": ["captureTransition", "retainedRejection", "cropDiagnostics_"],
    "src/Features/Upscaling/NeuralRendering/PackedCropPolicy.h": ["retainedRejection"],
    "src/Features/Upscaling/NeuralRendering/Renderer.cpp": ["modelPreparationEvidence_", "MergeModelPreparationEvidenceLocked", "SimulateTransientStall", "prewarmReservedBytes", "stageMicroseconds", "packedCropAdmission_", "BeginPackedCropAdmissionLocked", "PackedCropCpuTimer", "cropEvent", "colorAllocation", "RecordEvaluation", "diagnosticSourceTransaction_", "ConfigurePackedCropDiagnostics", "ConfigureReconstructionOverride", "ResolveReconstructionOverride", "reconstructionHistoryReset"],
    "src/Features/Upscaling/NeuralRendering/Renderer.h": ["SimulateTransientStall", "prewarmReservedBytes", "shaderGenerationApplied", "packedCropCapacityDiagnostics", "ConfigurePackedCropDiagnostics", "ConfigureReconstructionOverride", "ResolveReconstructionOverride", "reconstructionHistoryReset"],
    "src/Features/Upscaling/NeuralRendering/Runtime.cpp": ["GetLifecycleCounts", "lifecycleCounts_"],
    "src/Features/Upscaling/NeuralRendering/Runtime.h": ["LifecycleCounts", "lifecycleCounts_"],
    "src/Features/Upscaling/NeuralRendering/PrewarmPolicy.h": ["stageMicroseconds"],
    "src/Features/Upscaling/NeuralRendering/D3D12Interop.cpp": ["SimulateBackpressureTimeout"],
    "src/Features/Upscaling/NeuralRendering/D3D12Interop.h": ["SimulateBackpressureTimeout"],
    "src/Features/Upscaling/NeuralRendering/ExecutionEvidence.h": ["MergeResourcePreparation", "ModelReconstructionEvidence", "modelReconstruction"],
    "src/Features/Upscaling/NeuralCapture.cpp": ["SetNeuralCaptureExecutionContext", "neuralCaptureMutex", "SerializeNeuralCaptureRecord", "ResolveReconstructionOverride"],
    "src/Globals.cpp": ["RecordNeuralCaptureCamera", "ExposureCapture::"],
    "src/Hooks.cpp": ["ExposureCapture::"],
    "src/FrameAnnotations.cpp": ["ExposureProducerScope"],
    "src/Features/Upscaling.h": ["foveatedMaskBinocularGapVisualization", "NeuralCaptureRecord", "neuralCaptureMutex", "neuralPassTelemetryMutex", "neuralStereoRouteSnapshotMutex", "SetNeuralCaptureExecutionContext"],
    "src/Features/Upscaling.cpp": ["foveatedMaskBinocularGapVisualization", "neuralCaptureMutex", "neuralPassTelemetryMutex", "neuralStereoRouteSnapshotMutex"],
    "src/Features/ScreenshotApi.cpp": ["RetainNeuralDiagnostics", "FinalizeNeuralDiagnostics"],
    "src/Features/ScreenshotApi.h": ["diagnosticSnapshot"],
    "src/Features/NeuralRenderingFeature.cpp": ["Json StatusJson", "Json CaptureJson", 'ImGui::TreeNode("Colour experiments and diagnostics")' ],
    "src/Features/Upscaling/NeuralRendering/ColorPipeline.h": ["measurementBatches_", "readbacks", "measurementOrder_", "captureEvidenceEnabled_"],
    "src/Features/Upscaling/NeuralRendering/ColorPipeline.cpp": ["measurementBatches_", "readbacks", "measurementOrder_", "captureEvidenceEnabled_", "ColorMeasureCS.hlsl"],
}
with tempfile.TemporaryDirectory(prefix="csx-nr-diagnostics-") as temporary:
    unit = Path(temporary) / "unit.cpp"
    for name in empty_units + list(checks):
        source = (root / name).read_text()
        source = re.sub(r"^\s*#\s*(?:include|pragma)\b[^\n]*", "", source, flags=re.M)
        unit.write_text(source)
        output = {}
        for enabled in (False, True):
            result = subprocess.run([args.compiler, "/nologo", "/EP", "/TP",
                "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED",
                str(unit)], capture_output=True, text=True, check=True)
            output[enabled] = result.stdout
        if name in empty_units:
            assert not output[False].strip(), (name, "capture implementation remains in production")
            assert output[True].strip(), (name, "development implementation missing")
        else:
            for symbol in checks[name]:
                assert symbol not in output[False], (name, symbol, "leaked into production")
                assert symbol in output[True], (name, symbol, "development positive control missing")
        if name == "src/Features/Upscaling/NeuralCapture.cpp":
            for enabled in (False, True):
                assert "args.renderingMode = GetNeuralRenderingMode()" in output[enabled]
                assert "args.modelResolutionPercent = NeuralRendering::EffectiveModelResolutionPercent" in output[enabled]
        if name == "src/Globals.cpp":
            for enabled in (False, True):
                assert "frameBufferCached.vr = *frameBufferVR" in output[enabled]
                assert "void ObserveVRFrameBufferUpload" in output[enabled]
                assert "context->Unmap(resource, subresource)" in output[enabled]
print("NR capture implementations, hooks and measurement storage are development-only; shared camera observation remains")
