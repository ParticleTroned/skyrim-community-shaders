"""Keep diagnostics gated while production face protection remains available."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
units = [
    "src/Features/Upscaling.h",
    "src/Features/Upscaling.cpp",
    "src/Features/Upscaling/NeuralCapture.cpp",
    "src/Utils/CharacterCategoryAuthoring.cpp",
    *[
        f"src/Features/Upscaling/NeuralRendering/{name}"
        for name in (
            "ReconstructionTuning.h", "ReconstructionTuningJson.h", "Renderer.h",
            "ExecutionEvidence.h", "ExecutionEvidenceJson.h", "ModelResolution.h",
            "ModelResolution.cpp", "ModelResolutionPolicy.h",
            "FaceDetailMapping.h", "FaceDetailPolicy.h", "PeripheralMapping.h",
            "PeripheralCompressionPolicy.h", "CharacterRendering.h",
            "CharacterRendering.cpp", "Renderer.cpp",
        )
    ],
    *[
        f"features/Neural Rendering/Shaders/Upscaling/NeuralRendering/{name}"
        for name in (
            "ModelResolutionCommon.hlsli", "ModelResolutionPrepareCS.hlsl",
            "ModelResolutionReconstructCS.hlsl",
            "PeripheralCompression.hlsli",
        )
    ],
]
symbols = re.compile(
    r"\b(?:ReconstructionTuning\w*|ReconstructionPreparation|reconstructionTuning|neuralReconstructionTuning|"
    r"experimentalPreparation\w*|experimentalReconstructionShaders_|experimentalShadowTransfer|experimentalPerChannelClamping|"
    r"experimentalDepthSampleMotion|experimentalPadding|Experimental\w+|"
    r"EnsureExperimentalShader|SelectExperimentalReconstruction|DrawNeuralReconstructionExperiments)\b"
)
production_symbols = {
    "Upscaling.h": "neuralRenderingFaceDetailProtectionPercent",
    "Upscaling.cpp": 'DrawNeuralIntegerSetting("Face detail protection"',
    "NeuralCapture.cpp": "args.faceDetailProtectionPercent",
    "CharacterCategoryAuthoring.cpp": "ObserveFaceDetail",
    "Renderer.h": "faceDetailProtectionPercent",
    "Renderer.cpp": "mapping.faceDetail = {}",
    "ModelResolution.cpp": "ApplyFaceDetailProtection",
    "FaceDetailMapping.h": "struct FaceDetailMapping",
    "FaceDetailPolicy.h": "ApplyFaceDetailProtection",
    "PeripheralMapping.h": "FaceDetailMapping faceDetail",
    "CharacterRendering.h": "ObserveFaceDetail",
    "CharacterRendering.cpp": "GetFaceDetailRegions",
    "PeripheralCompression.hlsli": "float FaceDetailForward",
}
with tempfile.TemporaryDirectory(prefix="nr-reconstruction-gate-") as directory:
    for index, name in enumerate(units):
        source = (root / name).read_text(encoding="utf-8")
        source = re.sub(r"^\s*#\s*(?:include|pragma)\b[^\n]*", "", source, flags=re.M)
        unit = Path(directory) / f"unit-{index}.cpp"
        unit.write_text(source, encoding="utf-8")
        outputs = {}
        for bridge in (False, True):
            command = [args.compiler, "/nologo", "/EP", "/TP", "/DNR_PERIPHERAL_COMPRESSION",
                       "/DNR_FACE_DETAIL_PROTECTION",
                       "/DNR_CELL_CENTRE_MOTION", "/DNR_SHADOW_TRANSFER_TUNING", "/DNR_WHOLE_PIXEL_REJECTION",
                       "/DDEVBENCH_BRIDGE_ENABLED" if bridge else "/UDEVBENCH_BRIDGE_ENABLED", str(unit)]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            found = set(symbols.findall(result.stdout))
            if not bridge:
                assert not found, (name, found)
                required = production_symbols.get(Path(name).name)
                if required:
                    assert required in result.stdout, (name, "missing production face protection", required)
            outputs[bridge] = re.sub(r"^#.*$", "", result.stdout, flags=re.M).strip()
        if "DEVBENCH_BRIDGE_ENABLED" in source:
            assert outputs[False] != outputs[True], (name, "missing bridge positive control")
print(f"PASS: {len(units)} native/shader units retain production face protection and exclude diagnostic reconstruction experiments")
