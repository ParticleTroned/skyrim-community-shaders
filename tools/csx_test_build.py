#!/usr/bin/env python3
"""Allocate and report deterministic CSX test-distribution identities."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
BASE_VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+-(?:SE|VR)$")
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
PR_RE = re.compile(r"(?:\(#|pull request #)([0-9]+)", re.IGNORECASE)


class StateError(ValueError):
    """The persisted test-build state is missing or invalid."""


def parse_date(value: str) -> str:
    try:
        parsed = dt.date.fromisoformat(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "date must be a real calendar date in YYYY-MM-DD form"
        ) from error
    if parsed.isoformat() != value:
        raise argparse.ArgumentTypeError("date must use YYYY-MM-DD form")
    return value


def load_state(path: Path) -> dict[str, Any]:
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise StateError(f"cannot read test-build state {path}: {error}") from error
    validate_state(state)
    return state


def validate_state(state: Any) -> None:
    if not isinstance(state, dict):
        raise StateError("test-build state must be a JSON object")
    expected = {
        "schemaVersion",
        "sequence",
        "baseVersion",
        "dateUtc",
        "sourceSha",
        "includedPullRequests",
    }
    if set(state) != expected:
        raise StateError(
            "test-build state keys must be exactly: " + ", ".join(sorted(expected))
        )
    if state["schemaVersion"] != SCHEMA_VERSION:
        raise StateError(f"unsupported schemaVersion {state['schemaVersion']!r}")
    if (
        not isinstance(state["sequence"], int)
        or isinstance(state["sequence"], bool)
        or state["sequence"] < 1
    ):
        raise StateError("sequence must be a positive integer")
    if not isinstance(state["baseVersion"], str) or not BASE_VERSION_RE.fullmatch(
        state["baseVersion"]
    ):
        raise StateError("baseVersion must use <major>.<minor>-<SE|VR> form")
    if state["dateUtc"] is not None:
        try:
            parse_date(state["dateUtc"])
        except argparse.ArgumentTypeError as error:
            raise StateError(str(error)) from error
    if state["sourceSha"] is not None and (
        not isinstance(state["sourceSha"], str)
        or not SHA_RE.fullmatch(state["sourceSha"])
    ):
        raise StateError("sourceSha must be null or a 40-character lowercase SHA")
    prs = state["includedPullRequests"]
    if not isinstance(prs, list) or any(
        not isinstance(number, int)
        or isinstance(number, bool)
        or number < 1
        for number in prs
    ):
        raise StateError("includedPullRequests must be an array of positive integers")
    if prs != sorted(set(prs)):
        raise StateError("includedPullRequests must be sorted and unique")


def build_id(state: dict[str, Any]) -> str:
    if state["dateUtc"] is None or state["sourceSha"] is None:
        raise StateError("the seed state has not allocated a test build yet")
    return f"RC{state['sequence']}-{state['dateUtc']}"


def output_values(state: dict[str, Any], allocated: bool) -> dict[str, str]:
    identity = build_id(state)
    return {
        "allocated": str(allocated).lower(),
        "sequence": str(state["sequence"]),
        "date": state["dateUtc"],
        "build_id": identity,
        "base_version": state["baseVersion"],
        "display_version": (
            f"CSX {state['baseVersion']} RC{state['sequence']} "
            f"({state['dateUtc']})"
        ),
        "artifact_name": f"CSX-{state['baseVersion']}-{identity}",
        "source_sha": state["sourceSha"],
        "included_prs": ",".join(str(number) for number in state["includedPullRequests"]),
    }


def discover_pull_requests(old_sha: str | None, new_sha: str) -> set[int]:
    if old_sha is None:
        return set()
    result = subprocess.run(
        ["git", "log", "--format=%s", f"{old_sha}..{new_sha}"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(
            "warning: could not inspect the previous allocation range; "
            "recording only explicitly supplied PR numbers",
            file=sys.stderr,
        )
        return set()
    return discover_pull_requests_from_subjects(result.stdout.splitlines())


def discover_pull_requests_from_subjects(subjects: Iterable[str]) -> set[int]:
    return {int(match.group(1)) for subject in subjects for match in PR_RE.finditer(subject)}


def allocate(
    state: dict[str, Any],
    *,
    base_version: str,
    source_sha: str,
    date_utc: str,
    pull_requests: Iterable[int] = (),
) -> tuple[dict[str, Any], bool]:
    validate_state(state)
    if not BASE_VERSION_RE.fullmatch(base_version):
        raise StateError("base version must use <major>.<minor>-<SE|VR> form")
    if not SHA_RE.fullmatch(source_sha):
        raise StateError("source SHA must be 40 lowercase hexadecimal characters")
    try:
        parse_date(date_utc)
    except argparse.ArgumentTypeError as error:
        raise StateError(str(error)) from error
    if state["sourceSha"] == source_sha:
        return state.copy(), False

    prs = sorted(set(pull_requests))
    if any(not isinstance(number, int) or isinstance(number, bool) or number < 1 for number in prs):
        raise StateError("PR numbers must be positive integers")
    updated = {
        "schemaVersion": SCHEMA_VERSION,
        "sequence": state["sequence"] + 1,
        "baseVersion": base_version,
        "dateUtc": date_utc,
        "sourceSha": source_sha,
        "includedPullRequests": prs,
    }
    validate_state(updated)
    return updated, True


def write_state(path: Path, state: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
    ) as handle:
        json.dump(state, handle, indent=4)
        handle.write("\n")
        temporary = Path(handle.name)
    temporary.replace(path)


def emit(values: dict[str, str], github_output: Path | None) -> None:
    lines = [f"{key}={value}" for key, value in values.items()]
    if github_output is not None:
        with github_output.open("a", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
    for line in lines:
        print(line)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    read_parser = subparsers.add_parser("read", help="validate and report allocated state")
    read_parser.add_argument("--state", type=Path, required=True)
    read_parser.add_argument("--github-output", type=Path)

    allocate_parser = subparsers.add_parser(
        "allocate", help="increment the state when the represented source SHA changed"
    )
    allocate_parser.add_argument("--state", type=Path, required=True)
    allocate_parser.add_argument("--base-version", required=True)
    allocate_parser.add_argument("--source-sha", required=True)
    allocate_parser.add_argument(
        "--date",
        type=parse_date,
        default=dt.datetime.now(dt.timezone.utc).date().isoformat(),
    )
    allocate_parser.add_argument("--pr", type=int, action="append", default=[])
    allocate_parser.add_argument("--discover-git-range", action="store_true")
    allocate_parser.add_argument("--github-output", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    try:
        state = load_state(args.state)
        if args.command == "read":
            emit(output_values(state, allocated=False), args.github_output)
            return 0

        prs = set(args.pr)
        if args.discover_git_range:
            prs.update(discover_pull_requests(state["sourceSha"], args.source_sha))
        updated, changed = allocate(
            state,
            base_version=args.base_version,
            source_sha=args.source_sha,
            date_utc=args.date,
            pull_requests=prs,
        )
        if changed:
            write_state(args.state, updated)
        emit(output_values(updated, allocated=changed), args.github_output)
        return 0
    except StateError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
