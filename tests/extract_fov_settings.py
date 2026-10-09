"""Extract FOV production settings and UI paths for integration tests."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root, output):
    output.mkdir(parents=True, exist_ok=True)
    sources = {
        name: (root / "src" / name).read_text(encoding="utf-8-sig")
        for name in ["Features/Upscaling.cpp", "Features/ScreenSpaceGI.cpp",
                     "Features/ScreenSpaceShadows.cpp", "Features/VR.cpp",
                     "MenuDevBenchBridge.cpp", "State.cpp"]
    }
    bodies = []
    for name, signatures in {
        "Features/Upscaling.cpp": [
            "bool Upscaling::IsSharedFoveatedMaskActive()",
            "bool Upscaling::SetFoveatedUpscalingEnabled(",
            "float Upscaling::GetFoveatedBlendFalloff()",
            "bool Upscaling::SetFoveatedBlendCurve(",
            "void Upscaling::DrawFoveatedBlendSettings()"],
        "Features/ScreenSpaceGI.cpp": [
            "\tbool IsRuntimeFoveatedActive(",
            "\tfloat GetUpscalingActiveSharedMaskScale()",
            "\tfloat GetSharedUpscalingCenterMaskHorizontalScale()",
            "\tfloat GetSharedUpscalingCenterMaskFeather()",
            "\tstd::array<float2, 2> GetSharedUpscalingMaskOffsetsForSsgi()",
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
        "MenuDevBenchBridge.cpp": ["\tstd::string ValidateFovBlendCurve(", "\tjson FovSettingsStatus()"],
    }.items():
        bodies.extend(function(sources[name], sig) for sig in signatures)
    gi = sources["Features/ScreenSpaceGI.cpp"]
    shadows = sources["Features/ScreenSpaceShadows.cpp"]
    bodies.append(function(shadows, "\tstruct FoveatedShadowState") + ";")
    bodies.extend(function(shadows, sig) for sig in [
        "\tFoveatedShadowState ResolveFoveatedShadowState(",
        "\tFoveatedCommon::DispatchBounds BuildFoveatedBounds("])
    bodies.append("void ScreenSpaceGI::UpdateFoveatedBounds() {\n"
                  "const bool isVR = REL::Module::IsVR();\n"
                  "const float centerScale = ResolveFoveatedSharedMaskScale(settings);\n"
                  "const std::array<uint, 2> resolution{2000, 1000};\n"
                  + between(gi, "\tusing DispatchRect = CenterDispatchRect;",
                            "\tauto forEachCenterRect") + "}\n")
    gi_header = (root / "src/Features/ScreenSpaceGI.h").read_text(encoding="utf-8-sig")
    (output / "fov_ssgi_cache_types.h").write_text(
        between(gi_header, "\tstruct CenterDispatchRect", "\tstatic constexpr int kResourceProfileFullGI"),
        encoding="utf-8")

    def assignment(source, pattern):
        matches = re.findall(pattern, source)
        if len(matches) != 1:
            raise ValueError(f"Expected one production assignment: {pattern}")
        return matches[0] + "\n"

    bodies.append("float ShaderDetailFeather(const Upscaling::Profile& profile) {\n"
                  "const float centerScale = profile.sharedVisibleScale;\n"
                  "const float centerHorizontalScale = profile.centerHorizontalScale;\n"
                  "const float activeLightingMode = 1.0f;\n"
                  "struct { std::array<float, 4> VRFoveationData0{}; } data;\n"
                  + assignment(sources["State.cpp"],
                               r"data\.VRFoveationData0\s*=\s*\{\s*centerScale[^;]+;")
                  + "return data.VRFoveationData0[1]; }\n")
    bodies.append("float SsgiBufferFeather() {\n"
                  "struct { float CenterFullResMaskFeather = 0.0f; } data;\n"
                  + assignment(gi, r"data\.CenterFullResMaskFeather\s*=[^;]+;")
                  + "return data.CenterFullResMaskFeather; }\n")
    bodies.append("std::array<float, 2> ShadowBufferFeathers(const FoveatedShadowState& foveatedState) {\n"
                  "struct { float FoveatedData0[4]{}; } data, cbData;\n"
                  + assignment(shadows, r"\bdata\.FoveatedData0\[1\]\s*=[^;]+;")
                  + assignment(shadows, r"\bcbData\.FoveatedData0\[1\]\s*=[^;]+;")
                  + "return { data.FoveatedData0[1], cbData.FoveatedData0[1] }; }\n")
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
    upscaling = (root / "src/Features/Upscaling.h").read_text(encoding="utf-8-sig")
    for field, result_type in [("foveatedBlendCurveEnabled", "bool"), ("foveatedBlendFalloff", "float")]:
        expression = re.search(r"\b" + field + r"\s*=\s*([^;]+);", upscaling).group(1)
        defaults.append(f"{result_type} {field}Default() {{ return {expression}; }}")
    (output / "fov_defaults.h").write_text("\n".join(defaults), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
