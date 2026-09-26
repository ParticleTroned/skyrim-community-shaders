"""Validate CSX stable release identities for the existing release pipeline."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def git(*arguments: str) -> str:
    return subprocess.check_output(["git", *arguments], text=True).strip()


def parse_version(value: str) -> tuple[int, int, int]:
    match = VERSION.fullmatch(value)
    if not match:
        raise ValueError("Expected a canonical major.minor.patch version")
    result = tuple(map(int, match.groups()))
    if any(part > 65535 for part in result):
        raise ValueError("Version exceeds the Windows resource version range")
    return result


def core_line() -> tuple[int, int]:
    presets = json.loads(Path("CMakePresets.json").read_text(encoding="utf-8"))
    label = next(p for p in presets["configurePresets"] if p["name"] == "ALL")["cacheVariables"]["CSX_VERSION"]
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)-VR", label)
    if not match:
        raise ValueError("The universal release preset must identify the VR line")
    return int(match[1]), int(match[2])


def previous_tag(version: tuple[int, int, int]) -> str:
    tags = git("tag", "--merged", "HEAD", "--list", "csx*").splitlines()
    previous = []
    for tag in tags:
        if VERSION.fullmatch(tag.removeprefix("csx")):
            parsed = parse_version(tag[3:])
            if parsed[:2] == version[:2] and parsed < version:
                previous.append((parsed, tag))
    if not previous:
        raise ValueError("No reachable stable CSX release in the requested line")
    return max(previous)[1]


def plan(expected: str) -> dict[str, str]:
    version = parse_version(expected)
    if version[:2] != core_line():
        raise ValueError("Release version does not match the universal core line")
    if git("status", "--porcelain", "--untracked-files=normal"):
        raise ValueError("Release preparation requires a clean checkout")
    base = previous_tag(version)
    base_version = parse_version(base[3:])
    if version != (*base_version[:2], base_version[2] + 1):
        raise ValueError("Only the next patch after the reachable CSX baseline is allowed")
    tags = git("tag", "--list", "csx*").splitlines()
    for tag in tags:
        if VERSION.fullmatch(tag.removeprefix("csx")):
            candidate = parse_version(tag[3:])
            if candidate[:2] == version[:2] and candidate >= version:
                raise ValueError("Requested or newer release already exists; never replace its tag")
    return {"version": expected, "tag": f"csx{expected}", "base": base,
            "source": git("rev-parse", "HEAD")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prepare = sub.add_parser("plan")
    prepare.add_argument("--expected-version", required=True)
    prepare.add_argument("--github-output", type=Path)
    prepare.add_argument("--notes", type=Path)
    build = sub.add_parser("build-version")
    build.add_argument("--tag", default="")
    build.add_argument("--github-output", type=Path, required=True)
    audit = sub.add_parser("audit-base")
    audit.add_argument("--tag", required=True)
    args = parser.parse_args()
    if args.action == "plan":
        result = plan(args.expected_version)
        if args.notes:
            commits = git("log", "--no-merges", "--format=- %s (%h)", f"{result['base']}..HEAD")
            args.notes.write_text(
                f"## Changes since {result['base']}\n\n{commits}\n\n"
                "## Package\n\nProduction universal SE/AE/VR core, DevBench and Tracy disabled.\n"
                "The FOMOD offers VR, SE/AE, or no prebuilt cache. Both runtime\n"
                "caches include standard and Horizon Fix Water variants.\n",
                encoding="utf-8",
            )
    elif args.action == "build-version":
        value = ""
        if args.tag.startswith("csx"):
            value = args.tag[3:]
            if parse_version(value)[:2] != core_line():
                raise ValueError("Tag and universal core release lines disagree")
        result = {"release_version": value}
    else:
        print(previous_tag(parse_version(args.tag.removeprefix("csx"))))
        return
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as stream:
            for key, value in result.items():
                stream.write(f"{key}={value}\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
