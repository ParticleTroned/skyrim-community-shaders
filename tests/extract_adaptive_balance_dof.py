"""Extract production DOF gates, parameter handling and state restoration."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root, output):
    source = (root / "src/Features/AdaptiveBalanceDepthOfField.cpp").read_text(encoding="utf-8-sig")
    balance = (root / "src/Features/AdaptiveBrightness.cpp").read_text(encoding="utf-8-sig")
    image_space = (root / "extern/CommonLibSSE-NG/include/RE/I/ImageSpaceData.h").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    (output / "adaptive_balance_dof_engine_types.h").write_text(
        "namespace RE { struct ImageSpaceBaseData {\n"
        + function(image_space, "\t\tstruct DepthOfField")
        + "; }; }\n", encoding="utf-8")
    bodies = [
        between(source, "namespace\n{", "\tRE::Setting* FindDofSetting("),
        function(source, "\tvoid WriteSceneDepthOfField("),
        function(source, "\tvoid WriteUnderwaterDepthOfField("),
        function(source, "\tbool IsCurrentUnderwaterImageSpace("),
        function(source, "\tclass DepthOfFieldOverrideScope") + ";\n}\n",
        function(balance, "bool AdaptiveBrightness::IsRuntimeAvailable()"),
        function(balance, "bool AdaptiveBrightness::IsRuntimeEnabled()"),
        function(source, "void AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldSettings("),
        function(source, "void AdaptiveBalanceDepthOfField::SanitizeDepthOfFieldOverride("),
        function(source, "void AdaptiveBalanceDepthOfField::SanitizeSettings("),
        function(source, "bool AdaptiveBalanceDepthOfField::IsRuntimeEnabled()"),
        function(source, "bool AdaptiveBalanceDepthOfField::IsCorrectionEnabled()"),
    ]
    (output / "adaptive_balance_dof_under_test.h").write_text("\n".join(bodies), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
