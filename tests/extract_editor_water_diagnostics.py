"""Compile the native SE light-observer contract and its production diagnostics."""

import argparse
from pathlib import Path
import re

from extract_adaptive_balance_toggle import function


def extract(root, output):
    header = (root / "src/CSEditor/LightEditor.h").read_text(encoding="utf-8-sig")
    source = (root / "src/CSEditor/LightEditor.cpp").read_text(encoding="utf-8-sig")
    light_header = (root / "src/Features/LightLimitFix.h").read_text(encoding="utf-8-sig")
    light_source = (root / "src/Features/LightLimitFix.cpp").read_text(encoding="utf-8-sig")
    declaration = re.search(r"static void ObserveWaterPass\([^;]+;", header)
    if not declaration:
        raise ValueError("LightLimitFix still calls LightEditor::ObserveWaterPass, but its declaration is missing")
    output.mkdir(parents=True, exist_ok=True)
    members = function(header, "\tstruct WaterRoutingDiagnostics") + ";\n"
    members += "static WaterRoutingDiagnostics waterRoutingDiagnostics;\n"
    members += declaration.group(0) + "\n"
    for name in ("UpdateWaterRoutingDiagnostics", "ClearWaterRoutingDiagnostics"):
        members += re.search(r"(?:static )?void " + name + r"\([^;]+;", header).group(0) + "\n"
    (output / "editor_water_diagnostics_members.h").write_text(members, encoding="utf-8")
    policies = "\n".join(function(light_header, "\tstatic inline bool " + name + "(")
                         for name in ("RequiresEngineLightPath", "UsesClusteredWaterPath"))
    (output / "editor_water_policy_under_test.h").write_text(policies, encoding="utf-8")
    methods = "\n".join(function(source, marker) for marker in (
        "void LightEditor::UpdateWaterRoutingDiagnostics(",
        "void LightEditor::ClearWaterRoutingDiagnostics(",
        "void LightEditor::ObserveWaterPass("))
    hook_call = next(line for line in light_source.splitlines() if "LightEditor::ObserveWaterPass(Pass);" in line)
    methods += "\nvoid InvokeWaterPassHook(RE::BSRenderPass* Pass) {\n" + hook_call + "\n}\n"
    (output / "editor_water_diagnostics_under_test.h").write_text(methods, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
