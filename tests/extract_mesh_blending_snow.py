"""Extract snow flags and final-alpha integration from the production sources."""

import argparse
from pathlib import Path
import re

from extract_adaptive_balance_toggle import between, function


def extract(root: Path, output: Path, commonlib: Path) -> None:
    implementation = (root / "src/Features/MeshBlending.cpp").read_text(encoding="utf-8-sig")
    lighting = (root / "package/Shaders/Lighting.hlsl").read_text(encoding="utf-8-sig")
    math = (root / "features/Mesh Blending/Shaders/MeshBlending/MeshBlending.hlsli").read_text(encoding="utf-8-sig")
    flags = (commonlib / "include/RE/B/BSShaderProperty.h").read_text(encoding="utf-8-sig")
    material = (commonlib / "include/RE/B/BSShaderMaterial.h").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    (output / "mesh_blending_snow_flags_under_test.h").write_text(
        "namespace EngineFlags {\n"
        + re.search(r"static constexpr auto BIT64 = [^;]+;", flags).group()
        + "\n" + function(flags, "enum class EShaderPropertyFlag : std::uint64_t") + ";\n"
        + function(material, "enum class Feature\n") + ";\n"
        + "using ShaderFlag = EShaderPropertyFlag;\nusing MaterialFeature = Feature;\n"
        + between(implementation, "\tconstexpr std::uint64_t ToMask(", "\n\tbool IsFinite(")
        + "}\n", encoding="utf-8")

    coverage = between(lighting, "#\t\t\tif defined(MESH_BLENDING_PROJECTED_SNOW_AVAILABLE)",
                       "\n\trawBaseColor = Triplanar::SampleStochasticBias")
    coverage = re.sub(r"\[(branch|flatten)\]\s*", "", coverage).replace("discard;", "return std::nullopt;")
    (output / "mesh_blending_snow_coverage_under_test.h").write_text(
        "namespace MeshBlending {\n"
        + re.search(r"static const float kMinimumBlendWidth = [^;]+;", math).group() + "\n"
        + function(math, "float ComputeProjectedSnowCoverage(") + "}\n"
        + "#define MESH_BLENDING_PROJECTED_SNOW_AVAILABLE\n"
        + "std::optional<float> ProjectedCoverage(float projWeight) {\n"
        + "float meshBlendingProjectedSnowCoverage = 1.0f;\n" + coverage
        + "\nreturn meshBlendingProjectedSnowCoverage;\n}\n"
        + "#undef MESH_BLENDING_PROJECTED_SNOW_AVAILABLE\n", encoding="utf-8")

    epilogue = lighting[lighting.index("\tpsout.Diffuse.w = alpha;"):]
    epilogue = epilogue[re.match(r"\s*psout\.Diffuse\.w = alpha;\s*#\s*endif", epilogue).end():]
    finalization = epilogue[:epilogue.index("#\tif defined(LIGHT_LIMIT_FIX) && defined(LLFDEBUG)")]
    outputs = epilogue[len(finalization):]
    if re.search(r"psout\.Diffuse\.w\s*(?:[+*/-]?=|\+\+|--)", outputs):
        raise ValueError("Diffuse alpha changes after the tested finalization and before output replication")
    if finalization.count("psout.Diffuse.w *= meshBlendingProjectedSnowCoverage;") != 1:
        raise ValueError("Snow coverage must multiply final alpha exactly once")
    for name, constructor in (("MotionVectors.zw", "float2"), ("Specular", "float4"),
                              ("Albedo", "float4"), ("Reflectance", "float4"),
                              ("NormalGlossiness", "float4"), ("Masks", "float4"), ("Masks2", "float4")):
        if not re.search(r"psout\." + re.escape(name) + r"\s*=\s*" + constructor + r"\([^;]*psout\.Diffuse\.w\);", outputs):
            raise ValueError(f"{name} must copy finalized alpha")
    finalization = re.sub(r"\[(branch|flatten)\]\s*", "", finalization)
    wrappers = []
    cases = []
    for deferred in (False, True):
        for terrain in (False, True):
            for family in ("None", "Snow", "Nif"):
                name = f"Finalize{family}{int(deferred)}{int(terrain)}"
                defines = ["DEFERRED"] if deferred else []
                if terrain:
                    defines.append("TERRAIN_BLENDING")
                if family != "None":
                    defines.append("MESH_BLENDING_PROJECTED_SNOW_AVAILABLE" if family == "Snow" else "MESH_BLENDING_AVAILABLE")
                wrappers.extend(f"#define {define}\n" for define in defines)
                wrappers.append(
                    f"float {name}(float baseAlpha, [[maybe_unused]] float blendFactorTerrain, "
                    "[[maybe_unused]] float meshBlendingProjectedSnowCoverage) {\n"
                    "ShaderOutput psout;\npsout.Diffuse.w = baseAlpha;\n" + finalization
                    + "return psout.Diffuse.w;\n}\n")
                wrappers.extend(f"#undef {define}\n" for define in defines)
                cases.append(f"{{ {name}, {str(deferred and terrain).lower()}, Family::{family} }}")
    wrappers.append("constexpr FinalAlphaCase FinalAlphaCases[] = {\n" + ",\n".join(cases) + "\n};\n")
    (output / "mesh_blending_final_alpha_under_test.h").write_text("".join(wrappers), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--commonlib-dir", type=Path)
    arguments = parser.parse_args()
    extract(arguments.source_dir, arguments.output_dir,
            arguments.commonlib_dir or arguments.source_dir / "extern/CommonLibSSE-NG")
