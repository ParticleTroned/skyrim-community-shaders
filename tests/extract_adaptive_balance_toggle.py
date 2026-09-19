"""Extract production adjustment methods for a test with simulated engine state."""

import argparse
from pathlib import Path


def between(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def function(source, signature):
    begin = source.index(signature)
    brace = source.index("{", begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end] + "\n"


def extract(root, output):
    def read(name):
        return (root / "src/Features" / name).read_text(encoding="utf-8-sig")

    header = read("AdaptiveBrightness.h")
    source = read("AdaptiveBrightness.cpp")
    linear = read("LinearLighting.h")
    bloom = read("Bloom.cpp")
    water = read("WaterAppearance.cpp")
    output.mkdir(parents=True, exist_ok=True)
    (output / "linear_lighting_members.h").write_text(
        between(linear, "\tstruct Settings", "\tstruct alignas(16) PerFrameData"), encoding="utf-8")
    (output / "adaptive_balance_members.h").write_text(
        between(header, "\tenum class Profile", "\tstruct LocationOverrideTarget")
        + between(header, "\tstruct Settings", "\tstruct alignas(16) VanillaPointLightData")
        + between(header, "\tstruct EffectiveLinearLightingSettings", "\tstatic constexpr const char* kDefaultGlobalPresetName")
        + between(header, "\tmutable float smoothedWaterWindSpeed", "\n\tvirtual void DrawSettings()")
        + between(header, "\tvirtual bool SupportsPerformanceCostMeasurement()", "\n\tvirtual void LoadSettings").replace(" override", "")
        + between(header, "\tstruct ActiveProfileBlend", "\tProfile GetInteriorProfile()"), encoding="utf-8")
    bodies = [
        between(source, "AdaptiveBrightness::ProfileSettings AdaptiveBrightness::ProfileSettings::AdjustmentDefaults()", "namespace"),
        between(source, "\tconstexpr float kBrightnessMin", "\tconstexpr std::size_t kMaxOverrideHierarchyDepth"),
        function(source, "\tbool UsesClassifiedPointLightMultipliers("),
        between(source, "\tfloat SafeFinite(", "\tfloat WrapHour("),
        between(source, "\tvoid SanitizeWaterWindSettings(AdaptiveBrightness::WaterWindSettings& a_settings)\n", "\tvoid NormalizeBaseSettings("),
        function(source, "\tvoid ClampProfileSettings("),
        function(source, "bool AdaptiveBrightness::IsRuntimeAvailable()"),
        function(source, "void AdaptiveBrightness::SetEnabled("),
        function(source, "void AdaptiveBrightness::SetPerformanceCostMeasurementEnabled("),
        function(source, "bool AdaptiveBrightness::IsRuntimeEnabled()"),
        between(source, "LinearLighting::Settings AdaptiveBrightness::GetNeutralLinearLightingSettings()", "void AdaptiveBrightness::UpdateVanillaPointLightData("),
        between(bloom, "\tconstexpr float kEnhancementIntensityMax", "\tvoid DrawTooltip("),
        function(bloom, "\tvoid SanitizeProfileWithDefaults("),
        function(bloom, "Bloom::Settings Bloom::GetCommonBufferData("),
        function(bloom, "Bloom::Profile Bloom::LerpProfiles("),
        function(bloom, "void Bloom::SanitizeProfile("),
        between(water, "\tconstexpr float kWaterBrightnessMin", "\tvoid DrawTooltip("),
        function(water, "\tbool HasIdentityValues("),
        water[water.index("WaterAppearance::Settings WaterAppearance::GetCommonBufferData("):],
    ]
    (output / "adaptive_balance_under_test.h").write_text("\n".join(bodies), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
