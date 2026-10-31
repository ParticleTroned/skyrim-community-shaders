"""Extract production overlay admission with pinned engine enum boundaries."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root: Path, commonlib: Path, output: Path) -> None:
    header = (root / "src/Features/MeshBlending.h").read_text(encoding="utf-8-sig")
    source = (root / "src/Features/MeshBlending.cpp").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    for name, relative, signatures in (
        ("shader", "B/BSShaderProperty.h", ("\t\tenum class EShaderPropertyFlag :",)),
        ("material", "B/BSShaderMaterial.h", ("\t\tenum class Feature", "\t\tenum class Type")),
        ("alpha", "N/NiAlphaProperty.h", ("\t\tenum class AlphaFunction",)),
    ):
        text = (commonlib / "include/RE" / relative).read_text(encoding="utf-8-sig")
        (output / f"mesh_blending_{name}_enums.h").write_text(
            "\n".join(function(text, signature) + ";" for signature in signatures), encoding="utf-8")
    (output / "mesh_blending_admission_members.h").write_text(
        "\n".join(function(header, signature) + ";" for signature in (
            "\tenum class DetectionMode", "\tstruct Settings", "\tstruct SourceState", "\tstruct Diagnostics")),
        encoding="utf-8")
    helpers = between(source, "\tconstexpr std::uint64_t ToMask(", "\tbool IsValidUtf8(")
    methods = [function(source, signature) for signature in (
        "bool MeshBlending::BuildOverlaySourceState(",
        "bool MeshBlending::BuildSourceState(",
        "bool MeshBlending::ResolveStaticOwner(",
        "std::string MeshBlending::BuildModelPath(",
        "std::string MeshBlending::BuildNodePath(",
        "MeshBlending::Classification MeshBlending::ClassifyOnCacheMiss(",
    )]
    (output / "mesh_blending_admission_under_test.h").write_text(
        helpers + "\n" + "\n".join(methods), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--commonlib-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    extract(arguments.source_dir, arguments.commonlib_dir or arguments.source_dir / "extern/CommonLibSSE-NG", arguments.output_dir)
