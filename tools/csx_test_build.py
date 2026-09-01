#!/usr/bin/env python3
"""Allocate, verify, and dispatch deterministic CSX test builds."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence


SCHEMA_VERSION = 2
SEED_SEQUENCE = 217
SEED_BASE_VERSION = "3.19-VR"
SEED_DATE_UTC = "2026-08-09"
SEED_SOURCE_SHA = "7b87217c62566dc45b457d60e4108c162007ef5d"
MAX_DISCOVERY_COMMITS = 500
GRAPHQL_DISCOVERY_BATCH = 40
MAX_DISTRIBUTION_ATTEMPTS = 3
BASE_VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+-(?:SE|VR)$")
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
PR_RE = re.compile(r"(?:\(#|pull request #)([0-9]+)", re.IGNORECASE)
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
TEST_PACKAGE_RE = re.compile(r"^CSX_AIO-[A-Za-z0-9.-]+\.7z$")
COMPILED_SHADER_SUFFIXES = {".cso", ".pso", ".vso"}
FORBIDDEN_PACKAGE_MARKERS = ("devbench", "shadercache", "mgo-presets")

CommandRunner = Callable[..., subprocess.CompletedProcess[str]]


class StateError(ValueError):
    """The test-build state or requested transition is invalid."""


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


def parse_state(text: str, source: str) -> dict[str, Any]:
    try:
        state = json.loads(text)
    except json.JSONDecodeError as error:
        raise StateError(f"cannot parse test-build state {source}: {error}") from error
    validate_state(state)
    return state


def load_state(path: Path) -> dict[str, Any]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise StateError(f"cannot read test-build state {path}: {error}") from error
    return parse_state(text, str(path))


def validate_state(state: Any) -> None:
    """Validate JSON field types and the seed/allocation invariants."""
    if not isinstance(state, dict):
        raise StateError("test-build state must be a JSON object")
    expected = {
        "schemaVersion",
        "sequence",
        "baseVersion",
        "dateUtc",
        "sourceSha",
        "previousStateSha",
        "includedPullRequests",
    }
    if set(state) != expected:
        raise StateError(
            "test-build state keys must be exactly: " + ", ".join(sorted(expected))
        )
    if (
        not isinstance(state["schemaVersion"], int)
        or isinstance(state["schemaVersion"], bool)
        or state["schemaVersion"] != SCHEMA_VERSION
    ):
        raise StateError(f"unsupported schemaVersion {state['schemaVersion']!r}")
    if (
        not isinstance(state["sequence"], int)
        or isinstance(state["sequence"], bool)
        or state["sequence"] < SEED_SEQUENCE
    ):
        raise StateError(f"sequence must be an integer at least {SEED_SEQUENCE}")
    if not isinstance(state["baseVersion"], str) or not BASE_VERSION_RE.fullmatch(
        state["baseVersion"]
    ):
        raise StateError("baseVersion must use <major>.<minor>-<SE|VR> form")
    if not isinstance(state["dateUtc"], str):
        raise StateError("dateUtc must be a real calendar date")
    try:
        parse_date(state["dateUtc"])
    except argparse.ArgumentTypeError as error:
        raise StateError(str(error)) from error
    if not isinstance(state["sourceSha"], str) or not SHA_RE.fullmatch(
        state["sourceSha"]
    ):
        raise StateError("sourceSha must be a 40-character lowercase SHA")
    previous_state_sha = state["previousStateSha"]
    if previous_state_sha is not None and (
        not isinstance(previous_state_sha, str)
        or not SHA_RE.fullmatch(previous_state_sha)
    ):
        raise StateError(
            "previousStateSha must be null or a 40-character lowercase SHA"
        )
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

    if previous_state_sha is None:
        expected_seed = {
            "schemaVersion": SCHEMA_VERSION,
            "sequence": SEED_SEQUENCE,
            "baseVersion": SEED_BASE_VERSION,
            "dateUtc": SEED_DATE_UTC,
            "sourceSha": SEED_SOURCE_SHA,
            "previousStateSha": None,
            "includedPullRequests": [],
        }
        if state != expected_seed:
            raise StateError("the root state must match the immutable RC217 seed")
    elif state["sequence"] <= SEED_SEQUENCE:
        raise StateError("an allocated state must advance beyond the RC217 seed")


def build_id(state: dict[str, Any]) -> str:
    return f"RC{state['sequence']}-{state['dateUtc']}"


def output_values(state: dict[str, Any], allocated: bool) -> dict[str, str]:
    identity = build_id(state)
    return {
        "allocated": str(allocated).lower(),
        "state_kind": "seed" if state["previousStateSha"] is None else "allocation",
        "sequence": str(state["sequence"]),
        "date": state["dateUtc"],
        "build_id": identity,
        "base_version": state["baseVersion"],
        "display_version": (
            f"CSX {state['baseVersion']} RC{state['sequence']} "
            f"({state['dateUtc']})"
        ),
        "artifact_name": f"CSX-{state['baseVersion']}-{identity}",
        "package_name": f"CSX_AIO-{state['baseVersion']}-{identity}.7z",
        "tag_name": f"csx-test-build-{identity}",
        "source_sha": state["sourceSha"],
        "previous_state_sha": state["previousStateSha"] or "",
        "included_prs": ",".join(
            str(number) for number in state["includedPullRequests"]
        ),
    }


def _run(
    args: Sequence[str],
    *,
    cwd: Path | None = None,
    runner: CommandRunner = subprocess.run,
) -> subprocess.CompletedProcess[str]:
    return runner(
        list(args),
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
    )


def _checked_output(
    args: Sequence[str],
    *,
    cwd: Path | None = None,
    runner: CommandRunner = subprocess.run,
    description: str,
) -> str:
    result = _run(args, cwd=cwd, runner=runner)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "command failed"
        raise StateError(f"{description}: {detail}")
    return result.stdout.strip()


def discover_pull_requests_from_subjects(subjects: Iterable[str]) -> set[int]:
    return {
        int(match.group(1))
        for subject in subjects
        for match in PR_RE.finditer(subject)
    }


def discover_pull_requests(
    old_sha: str,
    new_sha: str,
    repository_slug: str,
    *,
    cwd: Path = Path("."),
    runner: CommandRunner = subprocess.run,
) -> set[int]:
    if not SHA_RE.fullmatch(old_sha) or not SHA_RE.fullmatch(new_sha):
        raise StateError("PR discovery requires canonical source SHAs")
    if not REPOSITORY_RE.fullmatch(repository_slug):
        raise StateError("repository must use owner/name form")

    relation = _run(
        ["git", "merge-base", "--is-ancestor", old_sha, new_sha],
        cwd=cwd,
        runner=runner,
    )
    if relation.returncode != 0:
        raise StateError("the previous source is not an ancestor of the new source")

    log_output = _checked_output(
        [
            "git",
            "log",
            "--first-parent",
            "--reverse",
            "--format=%H%x00%s",
            f"{old_sha}..{new_sha}",
        ],
        cwd=cwd,
        runner=runner,
        description="cannot inspect the complete allocation range",
    )
    commits: list[tuple[str, str]] = []
    for line in log_output.splitlines():
        sha, separator, subject = line.partition("\0")
        if not separator or not SHA_RE.fullmatch(sha):
            raise StateError("git returned malformed allocation-range evidence")
        commits.append((sha, subject))
    if len(commits) > MAX_DISCOVERY_COMMITS:
        raise StateError(
            f"allocation range exceeds the {MAX_DISCOVERY_COMMITS}-commit review bound"
        )

    pull_requests: set[int] = set()
    owner, repository_name = repository_slug.split("/", maxsplit=1)
    commit_shas = [commit_sha for commit_sha, _ in commits]
    for offset in range(0, len(commit_shas), GRAPHQL_DISCOVERY_BATCH):
        batch = commit_shas[offset : offset + GRAPHQL_DISCOVERY_BATCH]
        fields = "\n".join(
            (
                f'c{index}: object(oid: "{commit_sha}") {{ '
                "... on Commit { associatedPullRequests(first: 100) { "
                "nodes { number } pageInfo { hasNextPage } } } }"
            )
            for index, commit_sha in enumerate(batch)
        )
        query = (
            "query($owner:String!,$name:String!){"
            "repository(owner:$owner,name:$name){"
            f"{fields}"
            "}}"
        )
        associated = _checked_output(
            [
                "gh",
                "api",
                "graphql",
                "-f",
                f"query={query}",
                "-F",
                f"owner={owner}",
                "-F",
                f"name={repository_name}",
            ],
            cwd=cwd,
            runner=runner,
            description="cannot resolve complete PR provenance for the allocation range",
        )
        try:
            response = json.loads(associated)
            objects = response["data"]["repository"]
        except (json.JSONDecodeError, KeyError, TypeError) as error:
            raise StateError("GitHub returned malformed PR provenance") from error
        if not isinstance(objects, dict):
            raise StateError("GitHub returned malformed PR provenance")
        for index, commit_sha in enumerate(batch):
            commit = objects.get(f"c{index}")
            try:
                connection = commit["associatedPullRequests"]
                nodes = connection["nodes"]
                has_next_page = connection["pageInfo"]["hasNextPage"]
            except (KeyError, TypeError) as error:
                raise StateError(
                    f"GitHub omitted PR provenance for commit {commit_sha}"
                ) from error
            if has_next_page:
                raise StateError(
                    f"commit {commit_sha} has more associated PRs than the review bound"
                )
            if not isinstance(nodes, list):
                raise StateError(
                    f"GitHub returned malformed PR provenance for commit {commit_sha}"
                )
            for node in nodes:
                number = node.get("number") if isinstance(node, dict) else None
                if (
                    not isinstance(number, int)
                    or isinstance(number, bool)
                    or number < 1
                ):
                    raise StateError(
                        f"GitHub returned an invalid PR number for commit {commit_sha}"
                    )
                pull_requests.add(number)
    return pull_requests


def validate_transition(
    previous: dict[str, Any],
    current: dict[str, Any],
    *,
    previous_state_sha: str,
    source_sha: str,
) -> None:
    validate_state(previous)
    validate_state(current)
    if not SHA_RE.fullmatch(previous_state_sha) or not SHA_RE.fullmatch(source_sha):
        raise StateError("state transitions require canonical commit SHAs")
    if current["previousStateSha"] != previous_state_sha:
        raise StateError("state does not name its immediate predecessor state commit")
    if current["sourceSha"] != source_sha:
        raise StateError("state source does not match the allocation commit parent")
    if current["sequence"] != previous["sequence"] + 1:
        raise StateError("state sequence must advance its predecessor by exactly one")
    if current["sourceSha"] == previous["sourceSha"]:
        raise StateError("a new allocation must represent new source")
    if current["dateUtc"] < previous["dateUtc"]:
        raise StateError("allocation date cannot precede its predecessor")


def allocate(
    state: dict[str, Any],
    *,
    base_version: str,
    source_sha: str,
    previous_state_sha: str,
    date_utc: str,
    pull_requests: Iterable[int] = (),
) -> tuple[dict[str, Any], bool]:
    validate_state(state)
    if not BASE_VERSION_RE.fullmatch(base_version):
        raise StateError("base version must use <major>.<minor>-<SE|VR> form")
    if not SHA_RE.fullmatch(source_sha):
        raise StateError("source SHA must be 40 lowercase hexadecimal characters")
    if not SHA_RE.fullmatch(previous_state_sha):
        raise StateError(
            "previous state SHA must be 40 lowercase hexadecimal characters"
        )
    try:
        parse_date(date_utc)
    except argparse.ArgumentTypeError as error:
        raise StateError(str(error)) from error
    if state["sourceSha"] == source_sha:
        return state.copy(), False

    prs = sorted(set(pull_requests))
    if any(
        not isinstance(number, int)
        or isinstance(number, bool)
        or number < 1
        for number in prs
    ):
        raise StateError("PR numbers must be positive integers")
    updated = {
        "schemaVersion": SCHEMA_VERSION,
        "sequence": state["sequence"] + 1,
        "baseVersion": base_version,
        "dateUtc": date_utc,
        "sourceSha": source_sha,
        "previousStateSha": previous_state_sha,
        "includedPullRequests": prs,
    }
    validate_transition(
        state,
        updated,
        previous_state_sha=previous_state_sha,
        source_sha=source_sha,
    )
    return updated, True


def _relative_state_path(repository: Path, state_path: Path) -> str:
    repository = repository.resolve()
    candidate = state_path if state_path.is_absolute() else repository / state_path
    try:
        relative = candidate.resolve().relative_to(repository)
    except ValueError as error:
        raise StateError("state path must remain inside the repository") from error
    if not relative.parts or ".git" in relative.parts:
        raise StateError("state path must name a repository file")
    return relative.as_posix()


def _state_at_commit(
    repository: Path,
    commit_sha: str,
    relative_state_path: str,
    *,
    runner: CommandRunner = subprocess.run,
) -> dict[str, Any]:
    text = _checked_output(
        ["git", "show", f"{commit_sha}:{relative_state_path}"],
        cwd=repository,
        runner=runner,
        description=f"cannot read state at commit {commit_sha}",
    )
    return parse_state(text, f"{commit_sha}:{relative_state_path}")


def verify_seed_commit(
    repository: Path,
    seed_sha: str,
    state_path: Path,
    *,
    runner: CommandRunner = subprocess.run,
) -> dict[str, Any]:
    repository = repository.resolve()
    relative_state_path = _relative_state_path(repository, state_path)
    seed_commit = _checked_output(
        ["git", "rev-parse", f"{seed_sha}^{{commit}}"],
        cwd=repository,
        runner=runner,
        description="cannot resolve seed commit",
    )
    if not SHA_RE.fullmatch(seed_commit) or seed_commit != seed_sha:
        raise StateError("seed SHA must name one exact canonical commit")
    ancestry = _checked_output(
        ["git", "rev-list", "--parents", "-n", "1", seed_commit],
        cwd=repository,
        runner=runner,
        description="cannot inspect seed ancestry",
    ).split()
    if len(ancestry) < 2 or ancestry[0] != seed_commit:
        raise StateError("seed state must be introduced after existing history")
    first_parent = ancestry[1]
    prior_state = _checked_output(
        [
            "git",
            "log",
            "--first-parent",
            "-1",
            "--format=%H",
            first_parent,
            "--",
            relative_state_path,
        ],
        cwd=repository,
        runner=runner,
        description="cannot inspect earlier state history",
    )
    if prior_state:
        raise StateError("root state cannot be restored over an earlier state")
    state = _state_at_commit(
        repository, seed_commit, relative_state_path, runner=runner
    )
    if state["previousStateSha"] is not None:
        raise StateError("root state must be the immutable RC217 seed")
    return state


def verify_allocation_commit(
    repository: Path,
    allocation_sha: str,
    state_path: Path,
    *,
    runner: CommandRunner = subprocess.run,
) -> dict[str, Any]:
    repository = repository.resolve()
    relative_state_path = _relative_state_path(repository, state_path)
    state = _verify_allocation_step(
        repository, allocation_sha, state_path, runner=runner
    )
    predecessor_sha = state["previousStateSha"]
    while True:
        predecessor = _state_at_commit(
            repository,
            predecessor_sha,
            relative_state_path,
            runner=runner,
        )
        if predecessor["previousStateSha"] is None:
            verify_seed_commit(
                repository, predecessor_sha, state_path, runner=runner
            )
            return state
        predecessor = _verify_allocation_step(
            repository, predecessor_sha, state_path, runner=runner
        )
        predecessor_sha = predecessor["previousStateSha"]


def _verify_allocation_step(
    repository: Path,
    allocation_sha: str,
    state_path: Path,
    *,
    runner: CommandRunner = subprocess.run,
) -> dict[str, Any]:
    repository = repository.resolve()
    relative_state_path = _relative_state_path(repository, state_path)
    allocation = _checked_output(
        ["git", "rev-parse", f"{allocation_sha}^{{commit}}"],
        cwd=repository,
        runner=runner,
        description="cannot resolve allocation commit",
    )
    if not SHA_RE.fullmatch(allocation) or allocation != allocation_sha:
        raise StateError("allocation SHA must name one exact canonical commit")

    ancestry = _checked_output(
        ["git", "rev-list", "--parents", "-n", "1", allocation],
        cwd=repository,
        runner=runner,
        description="cannot inspect allocation ancestry",
    ).split()
    if len(ancestry) != 2 or ancestry[0] != allocation:
        raise StateError("allocation must be a single-parent state commit")
    source_sha = ancestry[1]
    state = _state_at_commit(
        repository, allocation, relative_state_path, runner=runner
    )
    if state["previousStateSha"] is None:
        raise StateError("the RC217 seed is not a distributable allocation commit")
    if state["sourceSha"] != source_sha:
        raise StateError("allocation state source must be its exact parent")

    changed_paths = _checked_output(
        [
            "git",
            "diff-tree",
            "--no-commit-id",
            "--name-only",
            "-r",
            source_sha,
            allocation,
        ],
        cwd=repository,
        runner=runner,
        description="cannot inspect allocation contents",
    ).splitlines()
    if changed_paths != [relative_state_path]:
        raise StateError("allocation commit must change only the test-build state")

    previous_state_sha = _checked_output(
        [
            "git",
            "log",
            "--first-parent",
            "-1",
            "--format=%H",
            source_sha,
            "--",
            relative_state_path,
        ],
        cwd=repository,
        runner=runner,
        description="cannot locate predecessor state commit",
    )
    if not SHA_RE.fullmatch(previous_state_sha):
        raise StateError("cannot locate a canonical predecessor state commit")
    if state["previousStateSha"] != previous_state_sha:
        raise StateError("allocation does not continue the latest state in its parent")

    predecessor_relation = _run(
        ["git", "merge-base", "--is-ancestor", previous_state_sha, source_sha],
        cwd=repository,
        runner=runner,
    )
    if predecessor_relation.returncode != 0:
        raise StateError("predecessor state is not in the allocation source history")
    previous = _state_at_commit(
        repository, previous_state_sha, relative_state_path, runner=runner
    )
    validate_transition(
        previous,
        state,
        previous_state_sha=previous_state_sha,
        source_sha=source_sha,
    )

    expected_subject = (
        f"chore(build): allocate {output_values(state, True)['display_version']} "
        "[skip ci]"
    )
    subject = _checked_output(
        ["git", "show", "-s", "--format=%s", allocation],
        cwd=repository,
        runner=runner,
        description="cannot inspect allocation commit subject",
    )
    if subject != expected_subject:
        raise StateError("allocation commit subject does not match its state identity")
    return state


def dispatch_decision(
    runs: Any,
    *,
    max_attempts: int = MAX_DISTRIBUTION_ATTEMPTS,
    allocation_sha: str | None = None,
) -> str:
    if not isinstance(runs, list):
        raise StateError("GitHub run inventory must be a JSON array")
    failures = 0
    for run in runs:
        if not isinstance(run, dict):
            raise StateError("GitHub run inventory contains a non-object")
        if allocation_sha is not None and run.get("headSha") != allocation_sha:
            raise StateError("GitHub returned a run for a different allocation")
        status = run.get("status")
        conclusion = run.get("conclusion")
        if status not in {
            "queued", "in_progress", "requested", "waiting", "pending", "completed"
        }:
            raise StateError("GitHub returned an invalid workflow-run status")
        if status != "completed":
            return "existing"
        if conclusion == "success":
            return "existing"
        failures += 1
    if failures >= max_attempts:
        raise StateError(
            f"distribution failed {failures} times; manual diagnosis is required"
        )
    return "dispatch"


def canonical_distribution_runs(runs: Any, tag_name: str) -> list[dict[str, Any]]:
    if not isinstance(runs, list):
        raise StateError("GitHub run inventory must be a JSON array")
    canonical = []
    for run in runs:
        if not isinstance(run, dict):
            raise StateError("GitHub run inventory contains a non-object")
        head_branch = run.get("headBranch")
        if not isinstance(head_branch, str):
            raise StateError("GitHub run inventory lacks a canonical ref name")
        if head_branch == tag_name:
            canonical.append(run)
    return canonical


def ensure_distribution_dispatch(
    *,
    repository_slug: str,
    workflow: str,
    allocation_sha: str,
    tag_name: str,
    runner: CommandRunner = subprocess.run,
    sleeper: Callable[[float], None] = time.sleep,
    max_attempts: int = MAX_DISTRIBUTION_ATTEMPTS,
) -> str:
    if not REPOSITORY_RE.fullmatch(repository_slug):
        raise StateError("repository must use owner/name form")
    if not SHA_RE.fullmatch(allocation_sha):
        raise StateError("allocation SHA must be canonical")
    if not re.fullmatch(
        r"csx-test-build-RC[1-9][0-9]*-[0-9]{4}-[0-9]{2}-[0-9]{2}",
        tag_name,
    ):
        raise StateError("test-build tag name is invalid")

    dispatch_commands = 0
    initial_failures: int | None = None
    while True:
        inventory = _checked_output(
            [
                "gh",
                "run",
                "list",
                "--repo",
                repository_slug,
                "--workflow",
                workflow,
                "--commit",
                allocation_sha,
                "--event",
                "workflow_dispatch",
                "--limit",
                "20",
                "--json",
                "databaseId,status,conclusion,headSha,headBranch",
            ],
            runner=runner,
            description="cannot reconcile distribution workflow runs",
        )
        try:
            runs = json.loads(inventory or "[]")
        except json.JSONDecodeError as error:
            raise StateError("GitHub returned malformed workflow-run JSON") from error
        runs = canonical_distribution_runs(runs, tag_name)
        decision = dispatch_decision(
            runs,
            max_attempts=max_attempts,
            allocation_sha=allocation_sha,
        )
        if decision == "existing":
            return "existing"

        observed_failures = sum(
            run.get("status") == "completed" and run.get("conclusion") != "success"
            for run in runs
        )
        if initial_failures is None:
            initial_failures = observed_failures
        # A transport error may still have created a run that is not visible
        # yet. Charge those commands in addition to failures from earlier
        # invocations, without charging a newly visible run twice.
        if (
            max(observed_failures, initial_failures + dispatch_commands)
            >= max_attempts
        ):
            raise StateError(
                "distribution retry budget is exhausted; manual diagnosis is required"
            )
        dispatch_commands += 1
        dispatch = _run(
            [
                "gh",
                "workflow",
                "run",
                workflow,
                "--repo",
                repository_slug,
                "--ref",
                tag_name,
                "--field",
                f"allocation-sha={allocation_sha}",
            ],
            runner=runner,
        )
        if dispatch.returncode == 0:
            return "dispatched"
        sleeper(min(10 * (2 ** (dispatch_commands - 1)), 60))


def verify_test_distribution(
    dist_path: Path,
    expected_name: str,
    *,
    runner: CommandRunner = subprocess.run,
) -> int:
    if not TEST_PACKAGE_RE.fullmatch(expected_name):
        raise StateError("test build requires one canonical AIO package name")
    try:
        staged = list(dist_path.iterdir())
    except OSError as error:
        raise StateError(f"cannot inspect test distribution {dist_path}: {error}") from error
    if len(staged) != 1 or not staged[0].is_file() or staged[0].name != expected_name:
        names = ", ".join(sorted(item.name for item in staged)) or "<empty>"
        raise StateError(
            f"test distribution must contain only {expected_name}; found: {names}"
        )

    archive = staged[0]
    listing = _run(
        ["cmake", "-E", "tar", "tf", str(archive)],
        runner=runner,
    )
    if listing.returncode != 0:
        detail = (listing.stderr or listing.stdout or "unknown archive error").strip()
        raise StateError(f"cannot inspect test distribution archive: {detail}")
    members = [line.strip() for line in listing.stdout.splitlines() if line.strip()]
    if not members:
        raise StateError("test distribution archive is empty")

    for member in members:
        normalized = member.replace("\\", "/")
        parts = [part for part in normalized.split("/") if part not in {"", "."}]
        if normalized.startswith("/") or ".." in parts:
            raise StateError(f"test distribution contains an unsafe path: {member}")
        lowered = [part.lower() for part in parts]
        if any(
            marker in part
            for part in lowered
            for marker in FORBIDDEN_PACKAGE_MARKERS
        ) or Path(normalized).suffix.lower() in COMPILED_SHADER_SUFFIXES:
            raise StateError(f"test distribution contains forbidden material: {member}")
    return len(members)


def write_state(path: Path, state: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
    ) as handle:
        json.dump(state, handle, indent=4)
        handle.write("\n")
        temporary = Path(handle.name)
    try:
        temporary.replace(path)
    except OSError:
        try:
            temporary.unlink(missing_ok=True)
        except OSError as cleanup_error:
            print(
                f"error: cannot remove temporary state {temporary}: {cleanup_error}",
                file=sys.stderr,
            )
        raise


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

    read_parser = subparsers.add_parser("read", help="validate and report state")
    read_parser.add_argument("--state", type=Path, required=True)
    read_parser.add_argument("--github-output", type=Path)

    allocate_parser = subparsers.add_parser(
        "allocate", help="allocate the next identity for changed product source"
    )
    allocate_parser.add_argument("--state", type=Path, required=True)
    allocate_parser.add_argument("--base-version", required=True)
    allocate_parser.add_argument("--source-sha", required=True)
    allocate_parser.add_argument("--previous-state-sha", required=True)
    allocate_parser.add_argument(
        "--date",
        type=parse_date,
        default=dt.datetime.now(dt.timezone.utc).date().isoformat(),
    )
    allocate_parser.add_argument("--pr", type=int, action="append", default=[])
    allocate_parser.add_argument("--discover-git-range", action="store_true")
    allocate_parser.add_argument("--repository-slug")
    allocate_parser.add_argument("--github-output", type=Path)

    verify_parser = subparsers.add_parser(
        "verify-allocation", help="verify one immutable allocation commit"
    )
    verify_parser.add_argument("--repository", type=Path, default=Path("."))
    verify_parser.add_argument("--allocation-sha", required=True)
    verify_parser.add_argument(
        "--state", type=Path, default=Path("version/test-build.json")
    )
    verify_parser.add_argument("--github-output", type=Path)

    seed_parser = subparsers.add_parser(
        "verify-seed", help="verify the one-time RC217 root-state commit"
    )
    seed_parser.add_argument("--repository", type=Path, default=Path("."))
    seed_parser.add_argument("--seed-sha", required=True)
    seed_parser.add_argument(
        "--state", type=Path, default=Path("version/test-build.json")
    )

    dispatch_parser = subparsers.add_parser(
        "ensure-dispatch", help="idempotently start one allocated distribution"
    )
    dispatch_parser.add_argument("--repository-slug", required=True)
    dispatch_parser.add_argument(
        "--workflow", default="test-build-distribution.yaml"
    )
    dispatch_parser.add_argument("--allocation-sha", required=True)
    dispatch_parser.add_argument("--tag-name", required=True)

    package_parser = subparsers.add_parser(
        "verify-package", help="verify one staged test distribution"
    )
    package_parser.add_argument("--dist", type=Path, default=Path("dist"))
    package_parser.add_argument("--expected-name", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    try:
        if args.command == "ensure-dispatch":
            print(
                ensure_distribution_dispatch(
                    repository_slug=args.repository_slug,
                    workflow=args.workflow,
                    allocation_sha=args.allocation_sha,
                    tag_name=args.tag_name,
                )
            )
            return 0

        if args.command == "verify-package":
            members = verify_test_distribution(args.dist, args.expected_name)
            print(f"package_members={members}")
            return 0

        if args.command == "verify-allocation":
            state = verify_allocation_commit(
                args.repository, args.allocation_sha, args.state
            )
            values = output_values(state, allocated=False)
            values["allocation_sha"] = args.allocation_sha
            emit(values, args.github_output)
            return 0

        if args.command == "verify-seed":
            verify_seed_commit(args.repository, args.seed_sha, args.state)
            print("seed=verified")
            return 0

        state = load_state(args.state)
        if args.command == "read":
            emit(output_values(state, allocated=False), args.github_output)
            return 0

        prs = set(args.pr)
        if args.discover_git_range:
            if not args.repository_slug:
                raise StateError("--repository-slug is required for complete discovery")
            prs.update(
                discover_pull_requests(
                    state["sourceSha"],
                    args.source_sha,
                    args.repository_slug,
                )
            )
        updated, changed = allocate(
            state,
            base_version=args.base_version,
            source_sha=args.source_sha,
            previous_state_sha=args.previous_state_sha,
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
