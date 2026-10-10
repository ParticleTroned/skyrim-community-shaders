"""Keep diagnostic journals bridge-only while preserving production fence ownership."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
public_symbols = {
    "Renderer.cpp": set(),
    "Renderer.h": set(),
    "D3D12Interop.cpp": {"GetLifetimeSnapshot", "LifetimeFenceSnapshot"},
    "D3D12Interop.h": {"GetLifetimeSnapshot", "LifetimeFenceSnapshot"},
    "LifetimeDiagnostics.h": set(),
    "LifetimeDiagnosticsJson.h": set(),
    "SubmissionFenceSnapshot.h": {"LifetimeFenceSnapshot"},
}
diagnostic_units = {
    "Renderer.cpp", "Renderer.h", "D3D12Interop.cpp", "D3D12Interop.h",
    "LifetimeDiagnostics.h", "LifetimeDiagnosticsJson.h",
}
fence_symbols = {"GetLifetimeSnapshot", "LifetimeFenceSnapshot"}
identifiers = re.compile(
    r"\b(?:\w*Lifetime\w*|lifetime\w*|resourceSerial_?|backendSerial_|GetResourceLeases|InteropResourceLeases?)\b")
with tempfile.TemporaryDirectory(prefix="csx-nr-bridge-gate-") as temporary:
    for name, expected_public in public_symbols.items():
        source = root / "src/Features/Upscaling/NeuralRendering" / name
        # Preprocess this unit's conditional compilation without expanding SDK includes.
        text = re.sub(r"^\s*#\s*(?:include|pragma)\b[^\n]*", "", source.read_text(), flags=re.M)
        unit = Path(temporary) / (name + ".cpp")
        unit.write_text(text)
        outputs = {}
        compiled = {}
        for enabled in (False, True):
            command = [args.compiler, "/nologo", "/EP", "/TP",
                       "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED", str(unit)]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            compiled[enabled] = result.stdout
            outputs[enabled] = identifiers.findall(result.stdout)
        assert set(outputs[False]) == expected_public, (
            name, "non-bridge code differs from the exact production fence contract", outputs[False])
        assert expected_public <= set(outputs[True]), (
            name, "production fence symbols differ between bridge modes", outputs[True])
        diagnostics = set(outputs[True]) - fence_symbols
        assert bool(diagnostics) == (name in diagnostic_units), (
            name, "bridge diagnostic positive control differs", diagnostics)
        for enabled, text in compiled.items():
            if name == "Renderer.cpp":
                for control in ("interop_.WaitForIdle(", "interop_.BeginD3D12(",
                                "interop_.EndD3D12(", "interop_.AbortD3D12("):
                    assert control in text, (name, enabled, "missing production fence control", control)
            elif name == "D3D12Interop.cpp":
                retirement = text[text.index("bool D3D12Interop::WaitForIdleLocked()"):
                                  text.index("bool D3D12Interop::WaitForIdle(")]
                assert retirement.index("queue12_->Signal(") < retirement.index("context11_->Signal(")
                assert retirement.index("context11_->Signal(") < retirement.index("resourceLeases_.clear();")
                for completion in ("WaitForFenceLocked(idleValue", "acknowledgementValue, kTeardownTimeoutMilliseconds"):
                    assert completion in retirement, (name, enabled, "missing API completion wait", completion)
print("Seven native units preserve production fences and gate diagnostic journals behind DEVBENCH_BRIDGE")
