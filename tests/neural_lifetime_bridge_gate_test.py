"""Keep diagnostic journals bridge-only while preserving production fence ownership."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from extract_adaptive_balance_toggle import function

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
public_symbols = {
    "Renderer.cpp": {"GetLifetimeSnapshot"},
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
                first = text.index("void Renderer::State::LogFailureDiagnosticsLocked(")
                last = text.index("bool Renderer::State::FailLocked(", first)
                capture = text[first:last]
                assert capture.count("interop_.GetLifetimeSnapshot()") == 1
                assert "Sample(device_.Get(), 0, true)" in capture
                for control in ("WaitForIdle(", "Flush(", "BeginD3D12(", "EndD3D12("):
                    assert control not in capture, (name, enabled, "failure capture issues GPU work", control)
                if not enabled:
                    recovery_start = text.index("bool Renderer::State::ServiceStallRecoveryLocked(")
                    recovery_end = text.index("bool Renderer::State::ServiceRetainedBackendLocked(", recovery_start)
                    recovery = text[recovery_start:recovery_end]
                    assert recovery.index("recovery.ShouldPoll(now)") < recovery.index("interop_.GetLifetimeSnapshot()")
                    assert recovery.index("StallRecoveryDecision::Recover") < recovery.index("ResetLocked(false, false)")
                    prewarm_start = text.index("void Renderer::State::ServicePrewarmLocked(")
                    prewarm_end = text.index("void Renderer::State::PreparePrewarmFeatureLocked(", prewarm_start)
                    prewarm = text[prewarm_start:prewarm_end]
                    assert prewarm.count("interop_.GetLifetimeSnapshot()") == 1
                    assert prewarm.index("policy.BeginOpportunity(") < prewarm.index("interop_.GetLifetimeSnapshot()")
                    assert "fences.completed >= fences.issued" in prewarm
                    remaining = text[:first] + text[last:recovery_start] + text[recovery_end:prewarm_start] + text[prewarm_end:]
                    startup_scopes = (
                        ("bool Renderer::State::PollNativeStartupLocked(",
                         r"if \(!nativeStartup_\.active\)\s*return true;\s*const auto fences ="),
                        ("bool Renderer::State::AdmitMemoryLocked(",
                         r"if \(NeedsNativeStartupLocked\(a_args, a_resources\) && interop_\.IsInitialized\(\)\) \{\s*const auto fences ="),
                        ("bool Renderer::State::ApplyRegionBatchLocked(",
                         r"if \(startupIndex\) \{\s*const auto fences ="),
                        ("void Renderer::State::PreparePrewarmFeatureLocked(",
                         r"\} else \{\s*const auto fences ="),
                    )
                    for signature, guard in startup_scopes:
                        scope = function(text, signature).rstrip("\n")
                        assert scope.count("interop_.GetLifetimeSnapshot()") == 1, (name, signature)
                        assert re.search(guard + r"\s*interop_\.GetLifetimeSnapshot\(\);", scope), (name, signature)
                        if "PreparePrewarmFeatureLocked" in signature:
                            assert scope.index("interop_.EndD3D12(false)") < scope.index("interop_.GetLifetimeSnapshot()")
                        assert scope in remaining, (name, signature)
                        remaining = remaining.replace(scope, "", 1)
                    assert "GetLifetimeSnapshot" not in remaining, (
                        name, "production fence observation escaped failure, recovery, startup and prewarm ownership")
                failure = text[last:text.index("void Renderer::State::SucceedLocked(", last)]
                quarantine_start = text.index("void Renderer::State::QuarantineAfterUnexpectedFailureLocked(")
                quarantine = text[quarantine_start:text.index("bool Renderer::State::EnsureBackendLocked(", quarantine_start)]
                assert failure.count("LogFailureDiagnosticsLocked(") == 2
                assert quarantine.count("LogFailureDiagnosticsLocked(") == 1
                assert text.count("LogFailureDiagnosticsLocked(") == 5, (
                    name, enabled, "diagnostic call escaped the terminal failure handlers")
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
