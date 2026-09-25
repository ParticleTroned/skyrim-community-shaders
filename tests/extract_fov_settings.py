"""Extract FOV production settings and UI paths for integration tests."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import function


def extract(root, output):
    output.mkdir(parents=True, exist_ok=True)
    sources = {
        name: (root / "src" / name).read_text(encoding="utf-8-sig")
        for name in ["Features/Upscaling.cpp", "Features/ScreenSpaceGI.cpp",
                     "Features/ScreenSpaceShadows.cpp", "Features/VR.cpp",
                     "MenuDevBenchBridge.cpp"]
    }
    bodies = []
    for name, signatures in {
        "Features/Upscaling.cpp": [
            "bool Upscaling::IsSharedFoveatedMaskActive()",
            "bool Upscaling::SetFoveatedUpscalingEnabled("],
        "Features/ScreenSpaceGI.cpp": [
            "\tbool IsRuntimeFoveatedActive(",
            "\tfloat GetUpscalingActiveSharedMaskScale()",
            "\tfloat ResolveFoveatedSharedMaskScale(",
            "\tvoid SyncResolvedSharedMaskScale(",
            "void ScreenSpaceGI::SetOCUEffectFoveationEnabled(",
            "void ScreenSpaceGI::DrawOCUEffectFoveationSettings()",
            "bool ScreenSpaceGI::IsRuntimeEnabled()",
            "void ScreenSpaceGI::SetFoveationEnabled(",
            "void ScreenSpaceGI::DrawFoveationSettings()"],
        "Features/ScreenSpaceShadows.cpp": [
            "bool ScreenSpaceShadows::IsRuntimeEnabled()",
            "void ScreenSpaceShadows::DrawFoveationSettings()"],
        "MenuDevBenchBridge.cpp": ["\tjson FovSettingsStatus()"],
    }.items():
        bodies.extend(function(sources[name], sig) for sig in signatures)
    vr = sources["Features/VR.cpp"]
    start = vr.index('\t\tdrawSection("Screen-Space Effects");')
    end = vr.index('\t\tdrawSection("Shader FOV");', start)
    bodies.append("void DrawScreenSpaceControls() {\n"
                  "auto& screenSpaceGI = globals::features::screenSpaceGI;\n"
                  "auto& screenSpaceShadows = globals::features::screenSpaceShadows;\n"
                  "const bool screenSpaceShadowsRuntimeActive = screenSpaceShadows.IsRuntimeEnabled();\n"
                  "const bool foveatedProfileActive = globals::features::upscaling.IsSharedFoveatedMaskActive();\n"
                  + vr[start:end] + "}\n")
    (output / "fov_under_test.h").write_text("\n".join(bodies), encoding="utf-8")
    defaults = []
    for feature, result_type in [("ScreenSpaceGI", "bool"), ("ScreenSpaceShadows", "unsigned")]:
        source = (root / "src/Features" / (feature + ".h")).read_text(encoding="utf-8-sig")
        expression = re.search(r"\bEnableFoveated\s*=\s*([^;]+);", source).group(1)
        defaults.append(f"{result_type} {feature}FovDefault() {{ return {expression}; }}")
    (output / "fov_defaults.h").write_text("\n".join(defaults), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
