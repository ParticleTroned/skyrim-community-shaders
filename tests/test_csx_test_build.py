from __future__ import annotations

import copy
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
    verify_test_distribution,
    write_state,
)


ROOT = Path(__file__).resolve().parents[1]
STATE_PATH = Path("version/test-build.json")
TAG_NAME = "csx-test-build-RC218-2026-09-07"
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

    def test_failed_atomic_replace_removes_temporary_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "test-build.json"
            with mock.patch.object(Path, "replace", side_effect=OSError("blocked")):
                with self.assertRaisesRegex(OSError, "blocked"):
                    write_state(state_path, SEED)
            self.assertEqual(list(Path(temporary).iterdir()), [])

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
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repository = Path(temporary.name)
        git(self.repository, "init")
        git(self.repository, "config", "user.name", "Test")
        git(self.repository, "config", "user.email", "test@example.invalid")
        (self.repository / "source.txt").write_text("baseline\n", encoding="utf-8")
        git(self.repository, "add", "source.txt")
        git(self.repository, "commit", "-m", "build: baseline")
        self.baseline_sha = git(self.repository, "rev-parse", "HEAD")
        self.seed_sha = self.commit_state(copy.deepcopy(SEED), "build: seed RC217")

    def commit_state(self, state: dict, subject: str | None = None) -> str:
        write_state(self.repository / STATE_PATH, state)
        git(self.repository, "add", STATE_PATH.as_posix())
        git(
            self.repository,
            "commit",
            "-m",
            subject or (
                "chore(build): allocate "
                f"{output_values(state, True)['display_version']} [skip ci]"
            ),
        )
        return git(self.repository, "rev-parse", "HEAD")

    def next_state(self, previous: dict, predecessor_sha: str) -> dict:
        state, _ = allocate(
            previous,
            base_version="3.19-VR",
            source_sha=git(self.repository, "rev-parse", "HEAD"),
            previous_state_sha=predecessor_sha,
            date_utc="2026-09-07",
            pull_requests=[67],
        )
        return state

    def test_seed_can_only_be_introduced_once(self) -> None:
        self.assertEqual(
            verify_seed_commit(self.repository, self.seed_sha, STATE_PATH), SEED
        )
        self.commit_state(self.next_state(SEED, self.seed_sha))
        restored_seed = self.commit_state(SEED, "chore(build): restore root")
        with self.assertRaisesRegex(StateError, "cannot be restored"):
            verify_seed_commit(self.repository, restored_seed, STATE_PATH)

    def test_deleted_seed_cannot_be_reintroduced(self) -> None:
        git(self.repository, "rm", STATE_PATH.as_posix())
        git(self.repository, "commit", "-m", "build: remove state")
        restored_seed = self.commit_state(SEED, "chore(build): restore root")
        with self.assertRaisesRegex(StateError, "cannot be restored"):
            verify_seed_commit(self.repository, restored_seed, STATE_PATH)

    def test_merge_commit_introduces_seed_on_first_parent(self) -> None:
        revised_seed = dict(SEED, schemaVersion=1)
        self.commit_state(revised_seed, "build: earlier seed schema")
        topic_sha = self.commit_state(SEED, "build: finalize seed schema")
        git(self.repository, "checkout", "-b", "default", self.baseline_sha)
        git(self.repository, "merge", "--no-ff", "-m", "Merge seed PR", topic_sha)
        merge_sha = git(self.repository, "rev-parse", "HEAD")
        latest = git(
            self.repository, "log", "--first-parent", "-1", "--format=%H",
            "--", STATE_PATH.as_posix(),
        )
        self.assertEqual(latest, merge_sha)
        self.assertEqual(
            verify_seed_commit(self.repository, latest, STATE_PATH), SEED
        )
        state = self.next_state(SEED, latest)
        allocation_sha = self.commit_state(state)
        self.assertEqual(
            verify_allocation_commit(self.repository, allocation_sha, STATE_PATH),
            state,
        )

    def test_exact_state_only_successor_is_verified(self) -> None:
        state = self.next_state(SEED, self.seed_sha)
        allocation_sha = self.commit_state(state)
        self.assertEqual(
            verify_allocation_commit(self.repository, allocation_sha, STATE_PATH),
            state,
        )
        (self.repository / "source.txt").write_text("changed\n", encoding="utf-8")
        git(self.repository, "add", "source.txt")
        git(self.repository, "commit", "-m", "test: descendant")
        descendant = git(self.repository, "rev-parse", "HEAD")
        with self.assertRaises(StateError):
            verify_allocation_commit(self.repository, descendant, STATE_PATH)

    def test_valid_looking_state_revert_is_rejected(self) -> None:
        self.commit_state(self.next_state(SEED, self.seed_sha))
        revert_sha = self.commit_state(SEED, "chore(build): restore old state")
        with self.assertRaises(StateError):
            verify_allocation_commit(self.repository, revert_sha, STATE_PATH)

    def test_corrupted_earlier_allocation_is_rejected(self) -> None:
        first = self.next_state(SEED, self.seed_sha)
        first_sha = self.commit_state(first)
        corrupted = self.next_state(first, first_sha)
        corrupted["sequence"] += 10
        corrupted_sha = self.commit_state(corrupted)
        successor = self.next_state(corrupted, corrupted_sha)
        successor_sha = self.commit_state(successor)
        with self.assertRaisesRegex(StateError, "exactly one"):
            verify_allocation_commit(self.repository, successor_sha, STATE_PATH)

    def test_complete_allocation_lineage_is_accepted(self) -> None:
        first = self.next_state(SEED, self.seed_sha)
        first_sha = self.commit_state(first)
        second = self.next_state(first, first_sha)
        second_sha = self.commit_state(second)
        third = self.next_state(second, second_sha)
        third_sha = self.commit_state(third)
        self.assertEqual(
            verify_allocation_commit(self.repository, third_sha, STATE_PATH), third
        )


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

    def test_malformed_run_status_is_rejected(self) -> None:
        with self.assertRaisesRegex(StateError, "invalid workflow-run status"):
            dispatch_decision([{}])

    def test_earlier_failures_reduce_uncertain_dispatch_budget(self) -> None:
        dispatches = 0
        inventories = 0
        runs = [
            {
                "status": "completed",
                "conclusion": "failure",
                "headSha": "a" * 40,
                "headBranch": TAG_NAME,
            }
        ] * 2

        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            nonlocal dispatches, inventories
            if args[:3] == ["gh", "run", "list"]:
                inventories += 1
                return completed(args, stdout=json.dumps(runs))
            if args[:3] == ["gh", "workflow", "run"]:
                dispatches += 1
                return completed(args, returncode=1, stderr="transport uncertain")
            raise AssertionError(args)

        with self.assertRaisesRegex(StateError, "manual diagnosis"):
            ensure_distribution_dispatch(
                repository_slug="owner/repository",
                workflow="test-build-distribution.yaml",
                allocation_sha="a" * 40,
                tag_name=TAG_NAME,
                runner=runner,
                sleeper=lambda _: None,
            )
        self.assertEqual(dispatches, 1)
        self.assertEqual(inventories, 2)

    def test_final_uncertain_attempt_is_reconciled(self) -> None:
        dispatches = 0

        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            nonlocal dispatches
            if args[:3] == ["gh", "run", "list"]:
                runs = (
                    [
                        {
                            "status": "queued",
                            "headSha": "a" * 40,
                            "headBranch": TAG_NAME,
                        }
                    ]
                    if dispatches == 3 else []
                )
                return completed(args, stdout=json.dumps(runs))
            if args[:3] == ["gh", "workflow", "run"]:
                dispatches += 1
                return completed(args, returncode=1, stderr="transport uncertain")
            raise AssertionError(args)

        self.assertEqual(
            ensure_distribution_dispatch(
                repository_slug="owner/repository",
                workflow="test-build-distribution.yaml",
                allocation_sha="a" * 40,
                tag_name=TAG_NAME,
                runner=runner,
                sleeper=lambda _: None,
            ),
            "existing",
        )
        self.assertEqual(dispatches, 3)

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
                    else [
                        {
                            "status": "queued",
                            "headSha": "a" * 40,
                            "headBranch": TAG_NAME,
                        }
                    ]
                )
                return completed(args, stdout=json.dumps(runs))
            if args[:3] == ["gh", "workflow", "run"]:
                return completed(args, returncode=1, stderr="transport uncertain")
            raise AssertionError(args)

        result = ensure_distribution_dispatch(
            repository_slug="owner/repository",
            workflow="test-build-distribution.yaml",
            allocation_sha="a" * 40,
            tag_name=TAG_NAME,
            runner=runner,
            sleeper=sleeps.append,
        )

        self.assertEqual(result, "existing")
        self.assertEqual(sleeps, [10])

    def test_noncanonical_ref_run_does_not_block_dispatch(self) -> None:
        dispatches = 0

        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            nonlocal dispatches
            if args[:3] == ["gh", "run", "list"]:
                runs = [
                    {
                        "status": "queued",
                        "headSha": "a" * 40,
                        "headBranch": "arbitrary-branch",
                    }
                ]
                return completed(args, stdout=json.dumps(runs))
            if args[:3] == ["gh", "workflow", "run"]:
                dispatches += 1
                return completed(args)
            raise AssertionError(args)

        self.assertEqual(
            ensure_distribution_dispatch(
                repository_slug="owner/repository",
                workflow="test-build-distribution.yaml",
                allocation_sha="a" * 40,
                tag_name=TAG_NAME,
                runner=runner,
            ),
            "dispatched",
        )
        self.assertEqual(dispatches, 1)

    def test_canonical_ref_with_wrong_sha_fails_closed(self) -> None:
        def runner(args: list[str], **_: object) -> subprocess.CompletedProcess[str]:
            if args[:3] == ["gh", "run", "list"]:
                runs = [
                    {
                        "status": "queued",
                        "headSha": "b" * 40,
                        "headBranch": TAG_NAME,
                    }
                ]
                return completed(args, stdout=json.dumps(runs))
            raise AssertionError(args)

        with self.assertRaisesRegex(StateError, "different allocation"):
            ensure_distribution_dispatch(
                repository_slug="owner/repository",
                workflow="test-build-distribution.yaml",
                allocation_sha="a" * 40,
                tag_name=TAG_NAME,
                runner=runner,
            )


class TestDistributionPackageTests(unittest.TestCase):
    EXPECTED = "CSX_AIO-3.19-VR-RC218-2026-09-07.7z"

    def test_clean_archive_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            dist = Path(temporary)
            archive = dist / self.EXPECTED
            archive.write_bytes(b"archive")

            def runner(
                args: list[str], **_: object
            ) -> subprocess.CompletedProcess[str]:
                self.assertEqual(args, ["cmake", "-E", "tar", "tf", str(archive)])
                return completed(args, stdout="SKSE/Plugins/CommunityShaders.dll\n")

            self.assertEqual(
                verify_test_distribution(dist, self.EXPECTED, runner=runner), 1
            )

    def test_nested_staged_output_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            dist = Path(temporary)
            (dist / self.EXPECTED).write_bytes(b"archive")
            (dist / "supplement").mkdir()
            with self.assertRaisesRegex(StateError, "must contain only"):
                verify_test_distribution(dist, self.EXPECTED)

    def test_forbidden_archive_material_is_rejected(self) -> None:
        forbidden = (
            "SKSE/Plugins/DevBench/bridge.dll",
            "ShaderCache/Optimized.A.csxpack",
            "MGO-Presets/Performance.json",
            "Shaders/compiled/example.cso",
        )
        for member in forbidden:
            with self.subTest(member=member), tempfile.TemporaryDirectory() as temporary:
                dist = Path(temporary)
                archive = dist / self.EXPECTED
                archive.write_bytes(b"archive")

                def runner(
                    args: list[str], **_: object
                ) -> subprocess.CompletedProcess[str]:
                    return completed(args, stdout=f"{member}\n")

                with self.assertRaisesRegex(StateError, "forbidden material"):
                    verify_test_distribution(dist, self.EXPECTED, runner=runner)

    def test_unreadable_archive_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            dist = Path(temporary)
            (dist / self.EXPECTED).write_bytes(b"not an archive")

            def runner(
                args: list[str], **_: object
            ) -> subprocess.CompletedProcess[str]:
                return completed(args, returncode=1, stderr="invalid archive")

            with self.assertRaisesRegex(StateError, "invalid archive"):
                verify_test_distribution(dist, self.EXPECTED, runner=runner)


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
    def test_provenance_discovery_receives_actions_authentication(self) -> None:
        workflow = (
            ROOT / ".github" / "workflows" / "test-build-allocate.yaml"
        ).read_text(encoding="utf-8")
        allocation_step = workflow.split(
            "- name: Allocate the next test-build identity", maxsplit=1
        )[1].split("- name:", maxsplit=1)[0]
        self.assertIn("GH_TOKEN: ${{ github.token }}", allocation_step)
        self.assertIn("git log --first-parent -1", workflow)
        self.assertNotIn("merged-pr", workflow)
        self.assertNotIn("PR_ARGUMENTS", workflow)

    def test_test_tags_do_not_use_the_stable_release_version_gate(self) -> None:
        workflow = (
            ROOT / ".github" / "workflows" / "_shared-build.yaml"
        ).read_text(encoding="utf-8")
        tag_step = workflow.split(
            "- name: Fail if CMake version does not match tag", maxsplit=1
        )[1].split("- name:", maxsplit=1)[0]
        self.assertIn("github.ref_type == 'tag'", tag_step)
        self.assertIn("inputs.test-build-id == ''", tag_step)

    def test_distribution_is_bound_to_exact_allocation(self) -> None:
        workflow = (
            ROOT / ".github" / "workflows" / "test-build-distribution.yaml"
        ).read_text(encoding="utf-8")
        self.assertNotIn("\n    push:", workflow)
        self.assertIn("ref: ${{ inputs.allocation-sha }}", workflow)
        self.assertIn("verify-allocation", workflow)
        self.assertIn("expected-package-name:", workflow)
        self.assertIn('"$DISPATCH_REF" != "refs/tags/$TAG_NAME"', workflow)
        self.assertIn('"$DISPATCH_SHA" != "$ALLOCATION_SHA"', workflow)

    def test_test_package_upload_uses_the_verified_file_only(self) -> None:
        workflow = (
            ROOT / ".github" / "workflows" / "_shared-build.yaml"
        ).read_text(encoding="utf-8")
        self.assertIn("csx_test_build.py verify-package", workflow)
        self.assertIn("path: dist/${{ inputs.expected-package-name }}", workflow)
        self.assertIn("if-no-files-found: error", workflow)

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
