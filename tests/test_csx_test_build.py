from __future__ import annotations

import copy
import io
import json
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest import mock

from tools.csx_test_build import (
    StateError,
    allocate,
    build_id,
    discover_pull_requests_from_subjects,
    output_values,
    resolve_source_revision,
    validate_state,
    verify_allocation_commit,
    write_state,
)


SEED = {
    "schemaVersion": 1,
    "sequence": 217,
    "baseVersion": "3.19-VR",
    "dateUtc": None,
    "sourceSha": None,
    "includedPullRequests": [],
}


class TestBuildStateTests(unittest.TestCase):
    def test_first_allocation_is_rc218(self) -> None:
        state, changed = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha="a" * 40,
            date_utc="2026-09-07",
            pull_requests=[25, 24, 25],
        )

        self.assertTrue(changed)
        self.assertEqual(state["sequence"], 218)
        self.assertEqual(state["includedPullRequests"], [24, 25])
        self.assertEqual(build_id(state), "RC218-2026-09-07")
        self.assertEqual(
            output_values(state, True)["display_version"],
            "CSX 3.19-VR RC218 (2026-09-07)",
        )

    def test_same_source_is_idempotent(self) -> None:
        initial, _ = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha="b" * 40,
            date_utc="2026-08-22",
            pull_requests=[24],
        )
        repeated, changed = allocate(
            initial,
            base_version="3.19-VR",
            source_sha="b" * 40,
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
            date_utc="2026-08-22",
        )
        updated, changed = allocate(
            initial,
            base_version="3.20-VR",
            source_sha="d" * 40,
            date_utc="2026-09-01",
        )

        self.assertTrue(changed)
        self.assertEqual(updated["sequence"], 219)
        self.assertEqual(updated["baseVersion"], "3.20-VR")

    def test_discovers_merge_and_squash_pr_subjects(self) -> None:
        subjects = [
            "Merge pull request #24 from owner/branch",
            "fix: calm shader compilation down (#25)",
            "ordinary direct commit",
        ]
        self.assertEqual(discover_pull_requests_from_subjects(subjects), {24, 25})

    def test_rejects_impossible_date(self) -> None:
        with self.assertRaises(StateError):
            allocate(
                copy.deepcopy(SEED),
                base_version="3.19-VR",
                source_sha="e" * 40,
                date_utc="2026-02-30",
            )

    def test_rejects_unknown_state_keys(self) -> None:
        malformed = copy.deepcopy(SEED)
        malformed["surprise"] = True
        with self.assertRaises(StateError):
            validate_state(malformed)

    def test_failed_replacement_removes_temporary_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            state_path = Path(temporary_directory) / "test-build.json"
            state_path.write_text("previous\n", encoding="utf-8")

            with mock.patch.object(
                Path, "replace", side_effect=OSError("replace refused")
            ):
                with self.assertRaisesRegex(OSError, "replace refused"):
                    write_state(state_path, copy.deepcopy(SEED))

            self.assertEqual(state_path.read_text(encoding="utf-8"), "previous\n")
            self.assertEqual(list(state_path.parent.iterdir()), [state_path])

    def test_cleanup_failure_reports_temporary_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            state_path = Path(temporary_directory) / "test-build.json"
            state_path.write_text("previous\n", encoding="utf-8")
            errors = io.StringIO()

            with (
                mock.patch.object(
                    Path, "replace", side_effect=OSError("replace refused")
                ),
                mock.patch.object(Path, "unlink", side_effect=OSError("cleanup refused")),
                redirect_stderr(errors),
            ):
                with self.assertRaisesRegex(OSError, "replace refused"):
                    write_state(state_path, copy.deepcopy(SEED))

            residue = [path for path in state_path.parent.iterdir() if path != state_path]
            self.assertEqual(len(residue), 1)
            self.assertIn(str(residue[0]), errors.getvalue())
            self.assertIn("cleanup refused", errors.getvalue())
            residue[0].unlink()

    def test_successful_replacement_publishes_complete_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            state_path = Path(temporary_directory) / "test-build.json"
            state_path.write_text("previous\n", encoding="utf-8")

            write_state(state_path, copy.deepcopy(SEED))

            self.assertEqual(json.loads(state_path.read_text(encoding="utf-8")), SEED)
            self.assertEqual(list(state_path.parent.iterdir()), [state_path])


class TestBuildGitIdentityTests(unittest.TestCase):
    def run_git(self, repository: Path, *arguments: str) -> str:
        result = subprocess.run(
            ["git", *arguments],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip()

    def make_allocation(self, repository: Path) -> tuple[Path, str, str]:
        self.run_git(repository, "init", "--initial-branch=main-VR")
        self.run_git(repository, "config", "user.name", "Test Builder")
        self.run_git(repository, "config", "user.email", "builder@example.invalid")
        (repository / "CMakePresets.json").write_text(
            json.dumps(
                {
                    "configurePresets": [
                        {
                            "name": "VR",
                            "cacheVariables": {"CSX_VERSION": "3.19-VR"},
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        state_path = repository / "version" / "test-build.json"
        write_state(state_path, copy.deepcopy(SEED))
        self.run_git(
            repository, "add", "CMakePresets.json", "version/test-build.json"
        )
        self.run_git(repository, "commit", "-m", "seed state")
        source_sha = self.run_git(repository, "rev-parse", "HEAD")

        allocated, changed = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha=source_sha,
            date_utc="2026-09-08",
            pull_requests=[23],
        )
        self.assertTrue(changed)
        write_state(state_path, allocated)
        self.run_git(repository, "add", "version/test-build.json")
        self.run_git(repository, "commit", "-m", "allocate test build")
        allocation_sha = self.run_git(repository, "rev-parse", "HEAD")
        return state_path, source_sha, allocation_sha

    def test_recovers_existing_allocation_without_new_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository = Path(temporary_directory)
            state_path, source_sha, allocation_sha = self.make_allocation(repository)
            state = json.loads(state_path.read_text(encoding="utf-8"))

            self.assertEqual(
                verify_allocation_commit(state_path, state),
                (source_sha, allocation_sha),
            )
            self.assertEqual(
                resolve_source_revision(state_path, state),
                (source_sha, allocation_sha),
            )

    def test_product_descendant_cannot_borrow_previous_allocation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository = Path(temporary_directory)
            state_path, _, allocation_sha = self.make_allocation(repository)
            (repository / "product.txt").write_text("new code\n", encoding="utf-8")
            self.run_git(repository, "add", "product.txt")
            self.run_git(repository, "commit", "-m", "new product change")
            product_sha = self.run_git(repository, "rev-parse", "HEAD")
            state = json.loads(state_path.read_text(encoding="utf-8"))

            self.assertEqual(resolve_source_revision(state_path, state), (product_sha, None))
            with self.assertRaisesRegex(StateError, "does not directly follow"):
                verify_allocation_commit(state_path, state, product_sha)
            self.assertEqual(
                verify_allocation_commit(state_path, state, allocation_sha)[1],
                allocation_sha,
            )

    def test_allocation_commit_rejects_an_extra_changed_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository = Path(temporary_directory)
            self.run_git(repository, "init", "--initial-branch=main-VR")
            self.run_git(repository, "config", "user.name", "Test Builder")
            self.run_git(
                repository, "config", "user.email", "builder@example.invalid"
            )
            (repository / "CMakePresets.json").write_text(
                json.dumps(
                    {
                        "configurePresets": [
                            {
                                "name": "VR",
                                "cacheVariables": {"CSX_VERSION": "3.19-VR"},
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            state_path = repository / "version" / "test-build.json"
            write_state(state_path, copy.deepcopy(SEED))
            self.run_git(
                repository, "add", "CMakePresets.json", "version/test-build.json"
            )
            self.run_git(repository, "commit", "-m", "seed state")
            source_sha = self.run_git(repository, "rev-parse", "HEAD")
            allocated, _ = allocate(
                copy.deepcopy(SEED),
                base_version="3.19-VR",
                source_sha=source_sha,
                date_utc="2026-09-08",
            )
            write_state(state_path, allocated)
            (repository / "extra.txt").write_text("not metadata\n", encoding="utf-8")
            self.run_git(repository, "add", "version/test-build.json", "extra.txt")
            self.run_git(repository, "commit", "-m", "mixed allocation")
            state = json.loads(state_path.read_text(encoding="utf-8"))

            with self.assertRaisesRegex(StateError, "must change only"):
                verify_allocation_commit(state_path, state)

    def test_allocation_rejects_base_version_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository = Path(temporary_directory)
            state_path, _, _ = self.make_allocation(repository)
            state = json.loads(state_path.read_text(encoding="utf-8"))
            state["baseVersion"] = "3.20-VR"
            write_state(state_path, state)
            self.run_git(repository, "add", "version/test-build.json")
            self.run_git(repository, "commit", "--amend", "--no-edit")

            with self.assertRaisesRegex(StateError, "does not match source"):
                verify_allocation_commit(state_path, state)


if __name__ == "__main__":
    unittest.main()
