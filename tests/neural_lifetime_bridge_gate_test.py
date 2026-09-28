"""Verify actual preprocessing removes lifetime diagnostics from normal builds."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
files = [
    "Renderer.cpp", "Renderer.h", "D3D12Interop.cpp", "D3D12Interop.h",
    "LifetimeDiagnostics.h", "LifetimeDiagnosticsJson.h",
]
identifiers = re.compile(r"\b(?:\w*Lifetime\w*|lifetime\w*|resourceSerial_?|backendSerial_)\b")
with tempfile.TemporaryDirectory(prefix="csx-nr-bridge-gate-") as temporary:
    for name in files:
        source = root / "src/Features/Upscaling/NeuralRendering" / name
        # Preprocess this unit's conditional compilation without expanding SDK includes.
        text = re.sub(r"^\s*#\s*(?:include|pragma)\b[^\n]*", "", source.read_text(), flags=re.M)
        unit = Path(temporary) / (name + ".cpp")
        unit.write_text(text)
        outputs = {}
        for enabled in (False, True):
            command = [args.compiler, "/nologo", "/EP", "/TP",
                       "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED", str(unit)]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            outputs[enabled] = identifiers.findall(result.stdout)
        assert not outputs[False], (name, "diagnostics leaked into non-bridge code", outputs[False])
        assert outputs[True], (name, "bridge-on positive control missing")
print("All six native diagnostic units are absent with DEVBENCH_BRIDGE disabled")
