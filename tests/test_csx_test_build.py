from __future__ import annotations

import copy
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.csx_test_build import (
    SEED_BASE_VERSION,
    SEED_DATE_UTC,
    SEED_SEQUENCE,
    SEED_SOURCE_SHA,
    StateError,
    allocate,
    build_id,
    discover_pull_requests,
    discover_pull_requests_from_subjects,
    dispatch_decision,
    ensure_distribution_dispatch,
    output_values,
    validate_state,
    verify_allocation_commit,
    verify_seed_commit,
    write_state,
)


ROOT = Path(__file__).resolve().parents[1]
STATE_PATH = Path("version/test-build.json")
SEED = {
    "schemaVersion": 2,
    "sequence": SEED_SEQUENCE,
    "baseVersion": SEED_BASE_VERSION,
    "dateUtc": SEED_DATE_UTC,
    "sourceSha": SEED_SOURCE_SHA,
    "previousStateSha": None,
    "includedPullRequests": [],
}


def completed(
    args: list[str], returncode: int = 0, stdout: str = "", stderr: str = ""
) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(args, returncode, stdout, stderr)


def git(repository: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repository,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


class TestBuildStateTests(unittest.TestCase):
    def test_first_allocation_is_rc218(self) -> None:
        state, changed = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha="a" * 40,
            previous_state_sha="f" * 40,
            date_utc="2026-09-07",
            pull_requests=[25, 24, 25],
        )

        self.assertTrue(changed)
        self.assertEqual(state["sequence"], 218)
        self.assertEqual(state["previousStateSha"], "f" * 40)
        self.assertEqual(state["includedPullRequests"], [24, 25])
        self.assertEqual(build_id(state), "RC218-2026-09-07")
        values = output_values(state, True)
        self.assertEqual(values["display_version"], "CSX 3.19-VR RC218 (2026-09-07)")
        self.assertEqual(
            values["package_name"], "CSX_AIO-3.19-VR-RC218-2026-09-07.7z"
        )

    def test_same_source_is_idempotent(self) -> None:
        initial, _ = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha="b" * 40,
            previous_state_sha="f" * 40,
            date_utc="2026-08-22",
            pull_requests=[24],
        )
        repeated, changed = allocate(
            initial,
            base_version="3.19-VR",
            source_sha="b" * 40,
            previous_state_sha="e" * 40,
            date_utc="2026-08-23",
            pull_requests=[25],
        )

        self.assertFalse(changed)
        self.assertEqual(repeated, initial)

    def test_counter_remains_global_when_base_version_changes(self) -> None:
        initial, _ = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha="c" * 40,
            previous_state_sha="f" * 40,
            date_utc="2026-08-22",
        )
        updated, changed = allocate(
            initial,
            base_version="3.20-VR",
            source_sha="d" * 40,
            previous_state_sha="e" * 40,
            date_utc="2026-09-01",
        )

        self.assertTrue(changed)
        self.assertEqual(updated["sequence"], 219)
        self.assertEqual(updated["baseVersion"], "3.20-VR")

    def test_rejects_rollback_and_fabricated_seed(self) -> None:
        rollback = copy.deepcopy(SEED)
        rollback["sequence"] = 216
        with self.assertRaises(StateError):
            validate_state(rollback)

        fabricated = copy.deepcopy(SEED)
        fabricated["sourceSha"] = "0" * 40
        with self.assertRaises(StateError):
            validate_state(fabricated)

    def test_rejects_impossible_date_and_unknown_keys(self) -> None:
        with self.assertRaises(StateError):
            allocate(
                copy.deepcopy(SEED),
                base_version="3.19-VR",
                source_sha="e" * 40,
                previous_state_sha="f" * 40,
                date_utc="2026-02-30",
            )
        malformed = copy.deepcopy(SEED)
        malformed["surprise"] = True
        with self.assertRaises(StateError):
            validate_state(malformed)


class PullRequestDiscoveryTests(unittest.TestCase):
    def test_discovers_merge_squash_and_associated_rebase_prs(self) -> None:
        first_sha = "1" * 40
        second_sha = "2" * 40
        calls: list[list[str]] = []

        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            calls.append(args)
            if args[:3] == ["git", "merge-base", "--is-ancestor"]:
                return completed(args)
            if args[:2] == ["git", "log"]:
                return completed(
                    args,
                    stdout=(
                        f"{first_sha}\0fix: merged work (#25)\n"
                        f"{second_sha}\0rebase commit without suffix\n"
                    ),
                )
            if args[:3] == ["gh", "api", "graphql"]:
                return completed(
                    args,
                    stdout=json.dumps(
                        {
                            "data": {
                                "repository": {
                                    "c0": {
                                        "associatedPullRequests": {
                                            "nodes": [{"number": 25}],
                                            "pageInfo": {"hasNextPage": False},
                                        }
                                    },
                                    "c1": {
                                        "associatedPullRequests": {
                                            "nodes": [{"number": 26}],
                                            "pageInfo": {"hasNextPage": False},
                                        }
                                    },
                                }
                            }
                        }
                    ),
                )
            raise AssertionError(args)

        result = discover_pull_requests(
            "a" * 40,
            "b" * 40,
            "owner/repository",
            runner=runner,
        )

        self.assertEqual(result, {25, 26})
        self.assertEqual(sum(call[:3] == ["gh", "api", "graphql"] for call in calls), 1)

    def test_discovery_fails_closed_on_missing_range(self) -> None:
        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            if args[:3] == ["git", "merge-base", "--is-ancestor"]:
                return completed(args)
            return completed(args, returncode=128, stderr="missing object")

        with self.assertRaisesRegex(StateError, "complete allocation range"):
            discover_pull_requests(
                "a" * 40,
                "b" * 40,
                "owner/repository",
                runner=runner,
            )

    def test_subject_parser_remains_bounded(self) -> None:
        subjects = [
            "Merge pull request #24 from owner/branch",
            "fix: calm shader compilation down (#25)",
            "ordinary direct commit",
        ]
        self.assertEqual(discover_pull_requests_from_subjects(subjects), {24, 25})


class AllocationCommitTests(unittest.TestCase):
    def test_seed_can_only_be_introduced_once(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)
            git(repository, "init")
            git(repository, "config", "user.name", "Test")
            git(repository, "config", "user.email", "test@example.invalid")
            (repository / "source.txt").write_text("baseline\n", encoding="utf-8")
            git(repository, "add", "source.txt")
            git(repository, "commit", "-m", "build: baseline")
            write_state(repository / STATE_PATH, copy.deepcopy(SEED))
            git(repository, "add", STATE_PATH.as_posix())
            git(repository, "commit", "-m", "build: seed RC217")
            seed_sha = git(repository, "rev-parse", "HEAD")
            self.assertEqual(
                verify_seed_commit(repository, seed_sha, STATE_PATH), SEED
            )

            state, _ = allocate(
                copy.deepcopy(SEED),
                base_version="3.19-VR",
                source_sha=seed_sha,
                previous_state_sha=seed_sha,
                date_utc="2026-09-07",
            )
            write_state(repository / STATE_PATH, state)
            git(repository, "add", STATE_PATH.as_posix())
            git(
                repository,
                "commit",
                "-m",
                "chore(build): allocate "
                f"{output_values(state, True)['display_version']} [skip ci]",
            )
            write_state(repository / STATE_PATH, copy.deepcopy(SEED))
            git(repository, "add", STATE_PATH.as_posix())
            git(repository, "commit", "-m", "chore(build): restore root")
            restored_seed = git(repository, "rev-parse", "HEAD")
            with self.assertRaisesRegex(StateError, "cannot be restored"):
                verify_seed_commit(repository, restored_seed, STATE_PATH)

    def test_exact_state_only_successor_is_verified(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)
            git(repository, "init")
            git(repository, "config", "user.name", "Test")
            git(repository, "config", "user.email", "test@example.invalid")
            write_state(repository / STATE_PATH, copy.deepcopy(SEED))
            git(repository, "add", STATE_PATH.as_posix())
            git(repository, "commit", "-m", "build: seed RC217")
            predecessor_sha = git(repository, "rev-parse", "HEAD")

            state, _ = allocate(
                copy.deepcopy(SEED),
                base_version="3.19-VR",
                source_sha=predecessor_sha,
                previous_state_sha=predecessor_sha,
                date_utc="2026-09-07",
                pull_requests=[67],
            )
            write_state(repository / STATE_PATH, state)
            git(repository, "add", STATE_PATH.as_posix())
            subject = (
                "chore(build): allocate "
                f"{output_values(state, True)['display_version']} [skip ci]"
            )
            git(repository, "commit", "-m", subject)
            allocation_sha = git(repository, "rev-parse", "HEAD")

            verified = verify_allocation_commit(
                repository, allocation_sha, STATE_PATH
            )
            self.assertEqual(verified, state)

            (repository / "unrelated.txt").write_text("changed\n", encoding="utf-8")
            git(repository, "add", "unrelated.txt")
            git(repository, "commit", "-m", "test: descendant")
            descendant = git(repository, "rev-parse", "HEAD")
            with self.assertRaises(StateError):
                verify_allocation_commit(repository, descendant, STATE_PATH)

    def test_valid_looking_state_revert_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)
            git(repository, "init")
            git(repository, "config", "user.name", "Test")
            git(repository, "config", "user.email", "test@example.invalid")
            write_state(repository / STATE_PATH, copy.deepcopy(SEED))
            git(repository, "add", STATE_PATH.as_posix())
            git(repository, "commit", "-m", "build: seed RC217")
            predecessor_sha = git(repository, "rev-parse", "HEAD")
            state, _ = allocate(
                copy.deepcopy(SEED),
                base_version="3.19-VR",
                source_sha=predecessor_sha,
                previous_state_sha=predecessor_sha,
                date_utc="2026-09-07",
            )
            write_state(repository / STATE_PATH, state)
            git(repository, "add", STATE_PATH.as_posix())
            git(
                repository,
                "commit",
                "-m",
                "chore(build): allocate "
                f"{output_values(state, True)['display_version']} [skip ci]",
            )

            write_state(repository / STATE_PATH, copy.deepcopy(SEED))
            git(repository, "add", STATE_PATH.as_posix())
            git(repository, "commit", "-m", "chore(build): restore old state")
            revert_sha = git(repository, "rev-parse", "HEAD")
            with self.assertRaises(StateError):
                verify_allocation_commit(repository, revert_sha, STATE_PATH)


class DispatchTests(unittest.TestCase):
    def test_active_or_successful_run_is_idempotent(self) -> None:
        self.assertEqual(dispatch_decision([{"status": "queued"}]), "existing")
        self.assertEqual(
            dispatch_decision([{"status": "completed", "conclusion": "success"}]),
            "existing",
        )

    def test_three_failed_runs_stop_automatic_retry(self) -> None:
        runs = [
            {"status": "completed", "conclusion": "failure"},
            {"status": "completed", "conclusion": "cancelled"},
            {"status": "completed", "conclusion": "timed_out"},
        ]
        with self.assertRaisesRegex(StateError, "manual diagnosis"):
            dispatch_decision(runs)

    def test_uncertain_dispatch_is_reconciled_before_retry(self) -> None:
        calls = 0
        sleeps: list[float] = []

        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            nonlocal calls
            if args[:3] == ["gh", "run", "list"]:
                calls += 1
                runs = (
                    []
                    if calls == 1
                    else [{"status": "queued", "headSha": "a" * 40}]
                )
                return completed(args, stdout=json.dumps(runs))
            if args[:3] == ["gh", "workflow", "run"]:
                return completed(args, returncode=1, stderr="transport uncertain")
            raise AssertionError(args)

        result = ensure_distribution_dispatch(
            repository_slug="owner/repository",
            workflow="test-build-distribution.yaml",
            allocation_sha="a" * 40,
            tag_name="csx-test-build-RC218-2026-09-07",
            runner=runner,
            sleeper=sleeps.append,
        )

        self.assertEqual(result, "existing")
        self.assertEqual(sleeps, [10])


class CMakeIdentityTests(unittest.TestCase):
    def run_cmake_parser(self, value: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / "parse.cmake"
            module = (ROOT / "cmake" / "TestBuildVersion.cmake").as_posix()
            script.write_text(
                f'include("{module}")\n'
                'csx_parse_test_build("${CSX_TEST_BUILD}" number date)\n'
                'message(STATUS "number=${number};date=${date}")\n',
                encoding="utf-8",
            )
            return subprocess.run(
                ["cmake", f"-DCSX_TEST_BUILD={value}", "-P", str(script)],
                check=False,
                capture_output=True,
                text=True,
            )

    def test_real_leap_date_is_accepted(self) -> None:
        result = self.run_cmake_parser("RC218-2028-02-29")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("number=RC218;date=2028-02-29", result.stdout + result.stderr)

    def test_impossible_and_false_like_dates_are_rejected(self) -> None:
        for value in ("RC218-2026-02-30", "RC218-0000-01-01", "OFF", "0"):
            with self.subTest(value=value):
                self.assertNotEqual(self.run_cmake_parser(value).returncode, 0)

    def test_empty_value_selects_stable_identity(self) -> None:
        result = self.run_cmake_parser("")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("number=;date=", result.stdout + result.stderr)


class WorkflowContractTests(unittest.TestCase):
    def test_distribution_is_bound_to_exact_allocation(self) -> None:
        workflow = (
            ROOT / ".github" / "workflows" / "test-build-distribution.yaml"
        ).read_text(encoding="utf-8")
        self.assertNotIn("\n    push:", workflow)
        self.assertIn("ref: ${{ inputs.allocation-sha }}", workflow)
        self.assertIn("verify-allocation", workflow)
        self.assertIn("expected-package-name:", workflow)

    def test_quiet_cancellation_cannot_interrupt_publication(self) -> None:
        workflow = (
            ROOT / ".github" / "workflows" / "test-build-allocate.yaml"
        ).read_text(encoding="utf-8")
        self.assertIn("group: csx-test-build-quiet-period", workflow)
        self.assertIn("group: csx-test-build-publisher", workflow)
        self.assertIn("cancel-in-progress: false", workflow)
        self.assertIn("git push --atomic", workflow)
        self.assertIn("ensure-dispatch", workflow)


if __name__ == "__main__":
    unittest.main()
