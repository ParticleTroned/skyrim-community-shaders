#!/usr/bin/env python3
"""Keep the checked-in feature registry consistent with package INIs and CORE markers."""

import argparse
from pathlib import Path
import re
import sys


def generate(root: Path) -> str:
    versions: dict[str, tuple[int, int, int]] = {}
    core: set[str] = set()
    for path in sorted(root.glob("features/*/Shaders/Features/*.ini")):
        match = re.search(r"(?im)^\s*version\s*=\s*(\d+)-(\d+)-(\d+)\s*$", path.read_text(encoding="utf-8-sig"))
        if not match:
            raise ValueError(f"Missing or invalid feature version: {path}")
        if path.stem in versions:
            raise ValueError(f"Duplicate feature short name: {path.stem}")
        versions[path.stem] = tuple(int(value) for value in match.groups())
        if (path.parents[2] / "CORE").is_file():
            core.add(path.stem)
    if not versions:
        raise ValueError("No feature INIs were found")
    lines = [
        "#pragma once",
        "// Generated from feature INIs and CORE markers by tools/generate-feature-versions.py.",
        "// This checked-in registry also serves builds whose include path precedes the generated directory.",
        "",
        "#include <map>",
        "#include <string_view>",
        "#include <unordered_set>",
        "",
        "namespace FeatureVersions",
        "{",
        "\tusing namespace std::literals::string_view_literals;",
        "",
        "\tinline const std::map<std::string_view, REL::Version> FEATURE_MINIMAL_VERSIONS{",
    ]
    for name, version in sorted(versions.items()):
        lines.append(f'\t\t{{ "{name}"sv, {{ {", ".join(map(str, version))} }} }},')
    lines.extend(["\t};", "", "\tinline const std::unordered_set<std::string_view> FEATURE_CORE_NAMES{"])
    lines.extend(f'\t\t"{name}"sv,' for name in sorted(core))
    lines.extend(["\t};", "}", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="Fail when the checked-in registry is stale")
    mode.add_argument("--write", action="store_true", help="Regenerate the checked-in registry")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / "include/FeatureVersions.h"
    try:
        content = generate(root)
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    if args.write:
        output.write_text(content, encoding="utf-8")
        print(f"Updated {output.relative_to(root)}")
        return 0
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        print("FeatureVersions.h is stale; run python tools/generate-feature-versions.py --write", file=sys.stderr)
        return 1
    print("Feature version registry matches package INIs and CORE markers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
