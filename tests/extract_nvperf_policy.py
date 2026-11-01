"""Extract the production metric and result policies without graphics SDK headers."""

import argparse
from pathlib import Path


def segment(source: str, start: str, end: str) -> str:
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError(f"NvPerf policy boundaries changed: {start!r} / {end!r}")
    return source[source.index(start):source.index(end)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    source = (args.source_dir / "src/Diagnostics/NvPerf.cpp").read_text(encoding="utf-8")
    metric_policy = segment(source, "namespace NvPerf\n{", "\tstd::filesystem::path CsvPath()")
    result_policy = segment(source, "\t\tbool IsSummed(", "\t\tvoid LogResult(")
    header = """#pragma once
#include "Diagnostics/NvPerf.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <utility>
"""
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "nvperf_policy_extracted.h").write_text(
        header + metric_policy + "\nnamespace {\n" + result_policy + "\n}\n}\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
