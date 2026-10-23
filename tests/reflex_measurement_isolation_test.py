"""Source boundary checks; these do not replace compiler/forced-header checks."""

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def without_devbench(source):
    output, stack = [], []
    for line in source.splitlines():
        directive = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        visible = all(active for _, active in stack)
        if not directive:
            if visible:
                output.append(line)
            continue
        kind, expression = directive.groups()
        expression = expression.strip()
        if kind in ("if", "ifdef", "ifndef"):
            managed = expression == "DEVBENCH_BRIDGE_ENABLED" or expression in ("defined(DEVBENCH_BRIDGE_ENABLED)", "defined DEVBENCH_BRIDGE_ENABLED")
            if managed:
                stack.append((True, kind == "ifndef"))
            else:
                if visible:
                    output.append(line)
                stack.append((False, True))
        elif kind == "else":
            managed, active = stack[-1]
            if managed:
                stack[-1] = (True, not active)
            elif visible:
                output.append(line)
        elif kind == "endif":
            managed, _ = stack.pop()
            if not managed and all(active for _, active in stack):
                output.append(line)
        elif kind == "elif":
            if stack[-1][0]:
                raise AssertionError("extend source checker for managed elif")
            if visible:
                output.append(line)
    if stack:
        raise AssertionError("unbalanced preprocessor source")
    return "\n".join(output)


class SourceBoundaryTests(unittest.TestCase):
    def test_diagnostics_disappear_without_devbench(self):
        for path in (ROOT / "src/Diagnostics").glob("ReflexMeasurement*.*"):
            stripped = without_devbench(path.read_text(encoding="utf-8"))
            stripped = re.sub(r'^\s*#.*$', '', stripped, flags=re.MULTILINE)
            self.assertFalse(stripped.strip(), path.name)
        for name in ("src/Features/Upscaling/ReflexBackend.cpp", "src/Features/Upscaling/ReflexBackend.h",
                     "src/Features/Upscaling/Streamline.cpp", "src/XSEPlugin.cpp"):
            stripped = without_devbench((ROOT / name).read_text(encoding="utf-8"))
            for symbol in ("ReflexMeasurement", "diagnosticSubmission", "DiagnosticBinding", "getSleepStatus", "ReadSleepStatus"):
                self.assertNotIn(symbol, stripped, name)

    def test_registered_schema_matches_dispatch(self):
        source = (ROOT / "src/Diagnostics/ReflexMeasurementBridge.cpp").read_text(encoding="utf-8")
        descriptor = json.loads(re.search(r'descriptor\s*=\s*R"\((.*?)\)";', source, re.DOTALL)[1])
        actions = set(descriptor["inputSchema"]["properties"]["action"]["enum"])
        dispatched = set(re.findall(r'action == "([a-z_]+)"', source))
        self.assertEqual(actions, dispatched)
        self.assertIn('RegisterTool("communityshaders.reflex_measurement"', source)
        self.assertIn("acknowledge_export", actions)
        self.assertIn("recover_and_stop", actions)

    def test_entry_guards_and_production_marker_policy(self):
        source = (ROOT / "src/Features/Upscaling/Streamline.cpp").read_text(encoding="utf-8")
        update = source[source.index("void Streamline::UpdateReflex()") :]
        self.assertLess(update.index("UpdateScope"), update.index("if (!initialized ||"))
        for path in (ROOT / "src/Diagnostics").glob("ReflexMeasurement*.*"):
            text = path.read_text(encoding="utf-8")
            for forbidden in ("slReflexGetState(", "NvAPI_D3D_GetLatency(", "NvAPI_D3D_SetLatencyMarker(", "logger::"):
                self.assertNotIn(forbidden, text, path.name)
        bridge = (ROOT / "src/Diagnostics/ReflexMeasurementBridge.cpp").read_text(encoding="utf-8")
        for forbidden in (".BindDevice(", ".Shutdown(", ".Disable(", ".ReadSleepStatus("):
            self.assertNotIn(forbidden, bridge)
        self.assertIn("s.acquiring.load(std::memory_order_acquire)", bridge)


if __name__ == "__main__":
    unittest.main()
