from __future__ import annotations

import copy
import unittest

from tools.csx_test_build import (
    StateError,
    allocate,
    build_id,
    discover_pull_requests_from_subjects,
    output_values,
    validate_state,
)


SEED = {
    "schemaVersion": 1,
    "sequence": 203,
    "baseVersion": "3.19-VR",
    "dateUtc": None,
    "sourceSha": None,
    "includedPullRequests": [],
}


class TestBuildStateTests(unittest.TestCase):
    def test_first_allocation_is_rc204(self) -> None:
        state, changed = allocate(
            copy.deepcopy(SEED),
            base_version="3.19-VR",
            source_sha="a" * 40,
            date_utc="2026-08-22",
            pull_requests=[25, 24, 25],
        )

        self.assertTrue(changed)
        self.assertEqual(state["sequence"], 204)
        self.assertEqual(state["includedPullRequests"], [24, 25])
        self.assertEqual(build_id(state), "RC204-2026-08-22")
        self.assertEqual(
            output_values(state, True)["display_version"],
            "CSX 3.19-VR RC204 (2026-08-22)",
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
        self.assertEqual(updated["sequence"], 205)
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


if __name__ == "__main__":
    unittest.main()
