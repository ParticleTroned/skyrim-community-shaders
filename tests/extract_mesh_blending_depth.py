"""Extract the actual draw-time Mesh Blending depth binding for host tests."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import between


def extract(root: Path, output: Path) -> None:
    header = (root / "src/State.h").read_text(encoding="utf-8-sig")
    source = (root / "src/State.cpp").read_text(encoding="utf-8-sig")
    binding = between(
        source,
        "\t\tconstexpr auto meshBlendingMask =",
        "\n\t\tif (permutationData != permutationDataPrevious)",
    )
    output.mkdir(parents=True, exist_ok=True)
    (output / "mesh_blending_depth_under_test.h").write_text(binding, encoding="utf-8")
    descriptor = between(
        header, "\tenum class ExtraShaderDescriptors :", "\n\tenum class ExtraFeatureDescriptors :"
    )
    (output / "mesh_blending_descriptors_under_test.h").write_text(descriptor, encoding="utf-8")
    hooks = (root / "src/Hooks.cpp").read_text(encoding="utf-8-sig")
    cleanup = re.search(r"CSX::MeshBlendingPolicy::ClearDrawDescriptors?\([^;]+;", hooks)
    if cleanup is None:
        raise ValueError("Mesh Blending unloaded-feature cleanup was not found")
    (output / "mesh_blending_cleanup_under_test.h").write_text(cleanup.group(), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    extract(arguments.source_dir, arguments.output_dir)
