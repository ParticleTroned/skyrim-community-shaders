"""Context geometry, evidence mapping and fail-closed orchestration without GPU calls."""

import copy
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import context_probe as probe
import test_packed_report as fixtures


class ContextGeometry(unittest.TestCase):
    def test_outward_alignment_clipping_and_raw_controls(self):
        owned = [[192, 512, 128, 128], [672, 512, 128, 128]]
        cases = probe.cases_for(owned, 1008, 1120, [0, 64, 128, 256])
        self.assertEqual(len(cases), 17)
        lookup = {case["id"]: case for case in cases}
        self.assertEqual(lookup["tight"]["rects"], owned)
        self.assertEqual(lookup["individual-raw-r1"]["rects"], [owned[1]])
        self.assertEqual(lookup["individual-h0-r1"]["rects"], [[640, 512, 192, 128]])
        self.assertEqual(lookup["individual-h256-r1"]["rects"], [[384, 256, 624, 640]])
        self.assertEqual(lookup["enclosing-raw"]["rects"], [[192, 512, 608, 128]])
        self.assertEqual(lookup["enclosing-h0"]["rects"], [[192, 512, 640, 128]])
        self.assertEqual(len({case["caseIdentitySha256"] for case in cases}), len(cases))
        for case in cases:
            for index in case["ownedIndices"]:
                evaluated = case["rects"][index] if case["id"] == "tight" else case["rects"][0]
                self.assertTrue(probe._contains(tuple(evaluated), tuple(owned[index])))
        self.assertEqual(probe.expanded([0, 0, 128, 128], 256, 1008, 1120), [0, 0, 384, 384])

    def test_unsafe_geometry_and_halos_are_rejected(self):
        for owned, halos in (([[0, 0, 64, 64]], [0]),
                             ([[0, 0, 128, 128], [64, 64, 128, 128]], [0]),
                             ([[0, 0, 64, 64], [200, 0, 64, 64]], [0]),
                             ([[0, 0, 64, 64], [128, 0, 64, 64]], [0, 0]),
                             ([[0, 0, 64, 64], [128, 0, 64, 64]], [512]),
                             ([[0, 0, 64, 64], [64, 0, 64, 64], [128, 0, 64, 64]], [0])):
            with self.subTest(owned=owned, halos=halos), self.assertRaises(ValueError):
                probe.cases_for(owned, 256, 128, halos)


class ContextEvidence(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.PackedReportTests()
        self.fixture.setUp()
        self.root = self.fixture.root
        self.source = self.root / "source/manifest.json"
        self.manifest, self.content_hash = probe._manifest(self.source)
        self.plan = {"schema": "csx-nr-context-probe-v1", "status": "prepared", "ownedRects": self.fixture.rects,
                     "repeats": 2, "samples": 2, "warmup": 1, "secondsPerCase": 10, "halos": [],
                     "sourceManifest": {"path": str(self.source), "sha256": probe.digest(self.source)},
                     "replayExecutable": {"path": str(self.fixture.exe), "sha256": probe.digest(self.fixture.exe)},
                     "runtime": {"path": "never-loaded", "sha256": self.fixture.runtime}, "cases": []}
        cases = [("full", "enclosing", [[0, 0, 8, 4]], [0, 1]),
                 ("tight", "separate", self.fixture.rects, [0, 1]),
                 ("individual-raw-r0", "enclosing", [self.fixture.rects[0]], [0]),
                 ("individual-raw-r1", "enclosing", [self.fixture.rects[1]], [1])]
        for identity, kind, rects, indices in cases:
            case = {"id": identity, "kind": kind, "rects": rects, "ownedIndices": indices,
                    "caseIdentitySha256": "b" * 64, "resultDirectory": "context-runs/" + identity,
                    "manifest": str(self.source)}
            self.plan["cases"].append(case)
            for repeat in range(2):
                self.fixture.make_result(case, repeat)
        probe.write(self.root / "plan.json", self.plan)
        probe.write(self.root / "run.json", {"status": "complete"})

    def tearDown(self):
        self.fixture.tearDown()

    def summarize(self):
        # The existing parser fixture uses tiny textures solely to test evidence.
        with patch.object(probe, "_admit_plan", return_value=(self.manifest, self.content_hash)):
            return probe.summarize(self.plan, self.root)

    def test_individual_subset_maps_to_global_owned_index_and_costs_stay_separate(self):
        report = self.summarize()
        self.assertEqual(report["status"], "complete", report)
        last = report["cases"][-1]
        self.assertEqual({d["ownedIndex"] for d in last["comparisonsToFull"]}, {1})
        self.assertTrue(last["exactRgbToFull"])
        self.assertTrue(last["exactRgbToTight"])
        self.assertTrue(report["fullReferenceRepeatability"]["exactRgb"])
        self.assertEqual(report["cases"][0]["nativeGpuMicroseconds"]["mean"], 20)
        self.assertEqual(report["cases"][1]["nativeGpuMicroseconds"]["mean"], 40)
        group = report["individualCostSums"][0]
        self.assertIn("not_one_batched_measurement", group["semantics"])
        self.assertEqual(group["repeats"][0]["sumOfMeansMicroseconds"], 40)
        self.assertFalse(report["productionQualified"])

    def test_failed_case_preserves_other_timings_and_missing_reference_is_unknown(self):
        bad = self.root / "context-runs/individual-raw-r0/repeat-1/results.json"
        raw = probe.read(bad)
        raw["status"] = "failed"
        probe.write(bad, raw)
        (self.root / "context-runs/full/repeat-0/results.json").unlink()
        probe.write(self.root / "run.json", {"status": "failed", "reason": "injected provider failure"})
        report = self.summarize()
        self.assertEqual(report["status"], "failed", report)
        self.assertEqual(report["executionReason"], "injected provider failure")
        self.assertTrue(report["errors"])
        self.assertEqual(report["cases"][-1]["nativeGpuMicroseconds"]["mean"], 20)
        self.assertIsNone(report["cases"][-1]["exactRgbToFull"])
        self.assertFalse(report["individualCostSums"][0]["repeats"][1]["status"] == "complete")

    def test_sample_count_drift_does_not_become_a_pass(self):
        self.plan["samples"] = 3
        report = self.summarize()
        self.assertEqual(report["status"], "partial", report)
        self.assertTrue(any("sample count" in reason for reason in report["errors"]))


class ContextPlan(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.temporary = tempfile.TemporaryDirectory(prefix="nr-context-plan-")
        self.root = Path(self.temporary.name)
        self.source = self.root / "manifest.json"
        self.executable = self.root / "replay.exe"
        self.runtime = self.root / "runtime.dll"
        self.executable.write_bytes(b"not an executable")
        self.runtime.write_bytes(b"not a native library")
        self.manifest = {"runtime": {"sha256": probe.digest(self.runtime).upper()},
                         "frames": [{"eyes": [{"color": {"width": 256, "height": 128}}]}]}
        probe.write(self.source, self.manifest)
        self.args = SimpleNamespace(manifest=self.source, replay=self.executable, runtime=self.runtime,
                                    output=self.root / "new", roi=[[0, 0, 64, 64], [128, 0, 64, 64]],
                                    halos="0", repeats=1, samples=2, warmup=1, seconds=10)
        self.parsed = patch.object(probe, "_manifest", return_value=(self.manifest, "c" * 64))
        self.parsed.start()
        self.plan = probe.prepare_plan(self.args)

    def tearDown(self):
        self.parsed.stop()
        self.temporary.cleanup()

    def test_preparation_is_immutable_and_plan_geometry_is_readmitted(self):
        with self.assertRaisesRegex(ValueError, "output exists"):
            probe.prepare_plan(self.args)
        probe._admit_plan(self.plan, check_tools=True)
        changed = copy.deepcopy(self.plan)
        changed["cases"][0]["ownedIndices"] = [1]
        with self.assertRaisesRegex(ValueError, "geometry changed"):
            probe._admit_plan(changed)
        changed = copy.deepcopy(self.plan)
        changed["toolSources"].pop("context_probe.py")
        with self.assertRaisesRegex(ValueError, "identity set"):
            probe._admit_plan(changed)

    def test_native_failure_stops_next_job_and_journals_terminal_reason(self):
        with patch.object(probe, "require_idle_game"), \
                patch.object(probe, "invoke", side_effect=RuntimeError("injected native failure")) as invoke, \
                patch.object(probe, "summarize", return_value={"status": "failed"}):
            with self.assertRaisesRegex(RuntimeError, "injected native failure"):
                probe.execute(self.plan, self.args.output)
        self.assertEqual(invoke.call_count, 1)
        journal = probe.read(self.args.output / "run.json")
        self.assertEqual(journal["status"], "failed")
        self.assertEqual(len(journal["jobs"]), 1)
        self.assertEqual(journal["jobs"][0]["status"], "failed")
        self.assertEqual(journal["reason"], "injected native failure")


if __name__ == "__main__":
    unittest.main()
