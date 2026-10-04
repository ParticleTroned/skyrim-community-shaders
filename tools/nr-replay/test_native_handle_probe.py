"""CPU-only campaign and exact-output guards for the native handle experiment."""

import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import native_handle_probe as probe


class NativeHandleProbeTests(unittest.TestCase):
    def samples(self, gpu=100, pixels=b"\x10\x20\x30\xff"):
        return [{"crops": {(0, 0): pixels, (0, 1): pixels, (1, 0): pixels, (1, 1): pixels},
                 "nativeGpuMicroseconds": gpu - 10, "batchGpuMicroseconds": gpu} for _ in range(2)]

    def test_calls_and_owned_geometry_remain_fixed(self):
        rects = [[0, 0, 128, 128], [256, 128, 128, 128]]
        cases = probe.cases(rects, 384, 256)
        self.assertEqual([c["nativeHandlePolicy"] for c in cases], ["independent", "per-eye", "independent"])
        self.assertTrue(all(c["rects"] == rects and c["kind"] == "separate" for c in cases))
        with self.assertRaises(ValueError):
            probe.cases([[0, 0, 64, 128], rects[1]], 384, 256)
        with self.assertRaises(ValueError):
            probe.cases([rects[0], rects[0]], 384, 256)

    def test_primary_cost_includes_barriers_and_requires_exact_rgba(self):
        result = probe.assess_bracket(self.samples(), self.samples(80), self.samples())
        self.assertTrue(result["timingQualified"])
        self.assertAlmostEqual(result["candidateBatchMeanDeltaPercent"], -20)
        candidate = self.samples(80, b"\x10\x20\x30\xfe")
        result = probe.assess_bracket(self.samples(), candidate, self.samples())
        self.assertFalse(result["strictRgbaEquivalent"])
        self.assertIsNone(result["candidateBatchMeanDeltaPercent"])

    def test_drift_and_baseline_output_instability_reject_speed_claim(self):
        for after in (self.samples(120), self.samples(100, b"\x11\x20\x30\xff")):
            result = probe.assess_bracket(self.samples(), self.samples(80), after)
            self.assertFalse(result["timingQualified"])
            self.assertIsNone(result["candidateBatchMeanDeltaPercent"])
            self.assertEqual(result["records"]["candidate"]["batchGpuMicroseconds"]["mean"], 80)

    def test_missing_outputs_or_samples_cannot_pass(self):
        bad = self.samples()
        del bad[1]["crops"][(1, 1)]
        with self.assertRaises(ValueError):
            probe.assess_bracket(self.samples(), bad, self.samples())
        with self.assertRaises(ValueError):
            probe.assess_bracket(self.samples(), [], self.samples())

    def test_journal_binds_order_plan_outputs_and_result_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = {"cases": probe.cases([[0, 0, 128, 128], [256, 128, 128, 128]], 384, 256), "repeats": 2}
            (root / "plan.json").write_text(json.dumps(plan))
            loaded = {(case["id"], repeat): {"resultSha256": f"{index:064x}", "steady": self.samples()}
                      for index, (case, repeat) in enumerate(probe.ordered_jobs(plan))}
            journal = {"schema": "csx-nr-native-handle-run-v1", "status": "complete",
                       "planSha256": probe.digest(root / "plan.json"), "jobs": [
                           {"case": case["id"], "repeat": repeat, "status": "complete",
                            "output": str(root / case["resultDirectory"] / f"repeat-{repeat}"),
                            "resultsSha256": loaded[(case["id"], repeat)]["resultSha256"]}
                           for case, repeat in probe.ordered_jobs(plan)]}
            probe.validate_journal(plan, root, journal, loaded)
            mutations = (
                lambda j: j.update(schema="other"),
                lambda j: j.update(status="running"),
                lambda j: j.update(planSha256="f" * 64),
                lambda j: j["jobs"].pop(),
                lambda j: j["jobs"].append(j["jobs"][0]),
                lambda j: j["jobs"].reverse(),
                lambda j: j["jobs"][0].update(case="per-eye"),
                lambda j: j["jobs"][0].update(repeat=True),
                lambda j: j["jobs"][0].update(status="running"),
                lambda j: j["jobs"][0].update(output=str(root / "another-output")),
                lambda j: j["jobs"][0].update(resultsSha256="f" * 64),
            )
            for mutation in mutations:
                bad = copy.deepcopy(journal)
                mutation(bad)
                with self.assertRaises(ValueError):
                    probe.validate_journal(plan, root, bad, loaded)
            with mock.patch.object(probe, "admit", return_value=({}, "")), \
                    mock.patch.object(probe, "admitted_repeat", side_effect=lambda p, c, r, repeat, m, h: loaded[(c["id"], repeat)]):
                (root / "run.json").write_text(json.dumps(journal))
                self.assertTrue(probe.summarize(plan, root)["timingQualified"])
                journal["jobs"].reverse()
                (root / "run.json").write_text(json.dumps(journal))
                rejected = probe.summarize(plan, root)
                self.assertEqual(rejected["status"], "failed")
                self.assertFalse(rejected["timingQualified"])
                self.assertEqual(rejected["brackets"], [])

    def test_failure_stops_dispatch_and_retains_failed_journal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = {"cases": probe.cases([[0, 0, 128, 128], [256, 128, 128, 128]], 384, 256),
                    "repeats": 2, "samples": 8, "warmup": 3, "secondsPerCase": 120,
                    "replayExecutable": {"path": "replay.exe"}, "sourceManifest": {"path": "manifest.json"},
                    "runtime": {"path": "runtime.dll"}, "alternateOutputSentinel": True}
            (root / "plan.json").write_text(json.dumps(plan))
            with mock.patch.object(probe, "admit", return_value=({}, "")), \
                    mock.patch.object(probe, "require_idle_game") as idle, \
                    mock.patch.object(probe, "invoke", side_effect=RuntimeError("native rejected")) as run, \
                    mock.patch.object(probe, "summarize", return_value={"status": "failed"}), \
                    mock.patch.object(probe, "admitted_repeat") as admit_result:
                with self.assertRaisesRegex(RuntimeError, "native rejected"):
                    probe.execute(plan, root)
                idle.assert_called_once()
                run.assert_called_once()
                admit_result.assert_not_called()
                arguments = run.call_args.args[0]
                self.assertEqual(arguments[arguments.index("--native-handle-policy") + 1], "independent")
                self.assertIn("--alternate-output-sentinel", arguments)
            journal = json.loads((root / "run.json").read_text())
            self.assertEqual(journal["status"], "failed")
            self.assertEqual(len(journal["jobs"]), 1)
            self.assertEqual(journal["jobs"][0]["status"], "failed")
            saved = copy.deepcopy(journal)
            with mock.patch.object(probe, "admit", return_value=({}, "")):
                with self.assertRaisesRegex(ValueError, "existing run"):
                    probe.execute(plan, root)
            self.assertEqual(json.loads((root / "run.json").read_text()), saved)


if __name__ == "__main__":
    unittest.main()
