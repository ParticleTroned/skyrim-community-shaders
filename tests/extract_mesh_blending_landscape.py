"""Expose production LAND capture and draw admission against a host engine boundary."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import between


def extract(root: Path, output: Path) -> None:
    source = (root / "src/Features/MeshBlendingLandscape.cpp").read_text(encoding="utf-8-sig")
    draw = (root / "src/Features/MeshBlending.cpp").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    (output / "mesh_blending_landscape_under_test.h").write_text(
        re.sub(r"^\s*#\s*include[^\n]*", "", source, flags=re.MULTILINE), encoding="utf-8")
    (output / "mesh_blending_landscape_draw_under_test.h").write_text(
        between(draw, "\tif (landscapePass || lodLandscapePass) {", "\tif ((passShaderFlags & ToMask(ShaderFlag::kProjectedUV)) != 0u)"),
        encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    extract(arguments.source_dir, arguments.output_dir)
