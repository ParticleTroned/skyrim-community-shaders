"""Preprocess the experiment boundary without building a DLL or object file."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--baseline", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
files = ["src/Features/Upscaling.h", "src/Features/Upscaling.cpp",
         "src/Features/Upscaling/NeuralCapture.cpp", "src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp"]
files += ["src/Features/Upscaling/NeuralRendering/" + name for name in (
    "CharacterSettings.h", "CharacterRendering.cpp", "RoiDescriptor.h", "ExecutionEvidenceJson.h", "CurrentContextExperiment.h",
    "ConfigurationSerialization.h")]
identifiers = re.compile(r"\b(?:\w*CurrentContext\w*|currentContextApplied)\b")
with tempfile.TemporaryDirectory(prefix="csx-nr-context-gate-") as temporary:
    unit = Path(temporary) / "unit.cpp"

    def preprocess(source, enabled):
        text = re.sub(r"^\s*#\s*(?:include|pragma)\b[^\n]*", "", source, flags=re.M)
        unit.write_text(text)
        result = subprocess.run([args.compiler, "/nologo", "/EP", "/TP",
                                 "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED",
                                 str(unit)], capture_output=True, text=True, check=True)
        return result.stdout

    for name in files:
        source = (root / name).read_text()
        off = preprocess(source, False)
        assert not identifiers.findall(off), (name, "experiment leaked into production")
        assert identifiers.findall(preprocess(source, True)), (name, "missing enabled positive control")
        if args.baseline:
            old = args.baseline / name
            before = preprocess(old.read_text() if old.is_file() else "", False)
            assert re.sub(r"\s+", " ", off).strip() == re.sub(r"\s+", " ", before).strip(), (name, "production tokens changed")
print(f"All {len(files)} experiment units disappear with the bridge disabled")
if args.baseline:
    print("Bridge-disabled preprocessed tokens match the preserved pre-3C source")
