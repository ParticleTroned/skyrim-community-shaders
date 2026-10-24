"""Extract the production DevBench adapter without repairing its C++ syntax."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root: Path, output: Path) -> None:
    header = (root / "src/Features/MeshBlending.h").read_text(encoding="utf-8-sig")
    source = (root / "src/Features/MeshBlendingDevBench.cpp").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    (output / "mesh_blending_devbench_members.h").write_text(
        function(header, "\tstruct Diagnostics") + ";\n", encoding="utf-8")
    (output / "mesh_blending_devbench_under_test.h").write_text(
        between(source, "json MeshBlending::ControlDevBench(", "\n#endif"), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    extract(arguments.source_dir, arguments.output_dir)
