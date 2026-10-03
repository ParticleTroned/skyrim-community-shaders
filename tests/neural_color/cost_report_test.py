"""Synthetic cost-admission regressions; no GPU performance claims."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/nr-color"))
import cost_report as cost
from transaction_evidence_test import fixture


def sequence(start=0, timing=4, count=32):
    children = []
    for index in range(start, start + count):
        acquisition, delayed = fixture()
        for envelope in (acquisition["executionEvidence"], delayed["executionEvidence"]):
            envelope.update(frame=index, sourceWorldFrame=index, sourceTransactionId=index + 100,
                            publicationSequence=index + 1)
            for context in envelope["sourceContexts"]:
                context["sourceTransactionId"] = index + 100
            batch = envelope["executions"][0]
            batch.update(frame=index, sourceWorldFrame=index, finished=True, succeeded=True)
            batch["source"]["sourceTransactionId"] = index + 100
            batch["timing"] = {
                "aggregateEvaluationGpu": {"state": "complete", "microseconds": timing * 1000, "clock": "d3d12_nr_queue"},
                "wholeNrLegacy": {"gpu": {"state": "complete", "inclusiveMs": timing + 1, "clock": "d3d11_context"}}}
            for region in batch["regions"]:
                region["source"]["sourceTransactionId"] = index + 100
        for key in ("frame", "sourceWorldFrame", "sourceTransactionId", "publicationSequence"):
            if key in acquisition:
                acquisition[key] = acquisition["executionEvidence"][key]
        for eye in ("left", "right"):
            acquisition[eye].update(frame=index, sourceWorldFrame=index)
        acquisition["configuration"] = {"upscaling": {"neuralCharacterRegionLimit": 2}, "color": {}}
        children.append({"ordinal": index-start+1, "state": "completed", "actual": {"acquisition": {"nrEvidence": acquisition,
                            "cameraEvidence": {"available": True, "sourceWorldFrame": index, "position": [0, 0, 0]}},
                            "captureDiagnostics": delayed}})
    return {"state": "final", "terminalOutcome": "completed", "continuity": {"complete": True, "requested": count, "acquired": count},
            "counts": {"requested": count, "scheduled": count, "acquired": count, "written": count,
                       "failed": 0, "dropped": 0, "cancelled": 0, "inFlight": 0}, "effective": {"frameCount": count},
            "producer": {"buildId": "synthetic_fixture"}, "children": children}


def triple():
    return [cost.summarize(sequence(start, timing)) for start, timing in ((0, 4), (40, 7), (80, 4))]


class CostReportTest(unittest.TestCase):
    def test_profiler_ready_and_native_complete_are_both_measured(self):
        self.assertEqual(cost.duration({"state": "ready", "clock": "d3d11_context", "inclusiveMs": 2},
                                       "inclusiveMs", "d3d11_context"), 2)
        self.assertIsNone(cost.duration({"state": "ready", "clock": "cpu_qpc", "inclusiveMs": 2},
                                       "inclusiveMs", "d3d11_context"))

    def test_complete_bracket_does_not_authorize_production_profile(self):
        result = cost.bracket(*triple())
        self.assertTrue(result["comparable"])
        self.assertEqual(result["metrics"]["nativeMs"]["meanDeltaMs"], 3)
        self.assertFalse(result["automaticProfileAdoption"])
        self.assertTrue(result["profileRejectionReasons"])

    def test_incomplete_sequence_and_duplicate_source_fail_closed(self):
        for mutate in (lambda d: d["continuity"].update(complete=False),
                       lambda d: d["children"].append(copy.deepcopy(d["children"][0]))):
            d = sequence()
            mutate(d)
            with self.assertRaises(ValueError):
                cost.summarize(d)

    def test_wrong_join_is_not_a_measurement(self):
        d = sequence()
        d["children"][0]["actual"]["captureDiagnostics"]["executionEvidence"]["sourceWorldFrame"] += 1
        with self.assertRaises(ValueError):
            cost.summarize(d)

    def test_truncated_children_cannot_claim_complete_sampling(self):
        d = sequence()
        d["children"].pop()
        with self.assertRaisesRegex(ValueError, "partial sequence membership"):
            cost.summarize(d)

    def test_failed_or_reordered_children_are_rejected(self):
        for mutate in (lambda d: d["children"][0].update(state="failed"),
                       lambda d: d["children"].reverse()):
            d = sequence()
            mutate(d)
            with self.assertRaisesRegex(ValueError, "incomplete child order/state"):
                cost.summarize(d)

    def test_capacity_unavailable_is_retained_and_disqualifies_window(self):
        d = sequence()
        d["children"][0]["actual"]["captureDiagnostics"]["executionEvidence"] = {
            "available": False, "reason": "capture_retention_capacity_exhausted"}
        r = cost.summarize(d)
        self.assertEqual(len(r["records"]), 32)
        self.assertEqual(r["statistics"]["nativeMs"]["n"], 31)
        self.assertIn("capture_retention_capacity_exhausted", r["reasons"])
        self.assertIsNone(r["records"][0]["calls"])

    def test_unavailable_native_timing_is_never_zero(self):
        d = sequence()
        for child in d["children"]:
            actual = child["actual"]
            for e in (actual["acquisition"]["nrEvidence"]["executionEvidence"], actual["captureDiagnostics"]["executionEvidence"]):
                e["executions"][0]["timing"]["aggregateEvaluationGpu"].update(state="pending", microseconds=None)
        result = cost.summarize(d)
        self.assertEqual(result["statistics"]["nativeMs"]["n"], 0)
        self.assertIsNone(result["statistics"]["nativeMs"]["minimum"])
        self.assertIn("nativeMs_incomplete", result["reasons"])

    def test_baseline_drift_is_a_rejection_not_a_warning(self):
        a, b, _ = triple()
        result = cost.bracket(a, b, cost.summarize(sequence(80, 8)))
        self.assertFalse(result["comparable"])
        self.assertIsNone(result["metrics"]["nativeMs"]["meanDeltaMs"])
        self.assertIn("nativeMs_baseline_unstable", result["reasons"])

    def test_producer_settings_and_camera_changes_reject_pair(self):
        for field in ("producer", "configurations", "cameras"):
            runs = triple()
            runs[1][field] = {"changed": True}
            self.assertIn("unmatched_" + field, cost.bracket(*runs)["reasons"])

    def test_reused_or_reordered_windows_rejected(self):
        a, b, c = triple()
        self.assertIn("reused_observations_across_bracket", cost.bracket(a, b, a)["reasons"])
        self.assertIn("baseline_bracket_not_chronological", cost.bracket(c, b, a)["reasons"])

    def test_new_publication_does_not_make_an_old_transaction_unique(self):
        a, b, c = triple()
        c["records"][0]["identity"] = (a["records"][0]["identity"][0], a["records"][0]["identity"][1],
                                        999, 80, 80, a["records"][0]["identity"][5])
        self.assertIn("reused_observations_across_bracket", cost.bracket(a, b, c)["reasons"])

    def test_final_geometry_and_capacity_are_in_the_key(self):
        raw = sequence()["children"][0]["actual"]["captureDiagnostics"]["executionEvidence"]["executions"][0]
        key = cost.digest(cost.final_plan(raw))
        for field in ("width", "height"):
            d = copy.deepcopy(raw)
            d["regions"][0]["nrInput"]["capacityGrid"][field] += 1
            self.assertNotEqual(key, cost.digest(cost.final_plan(d)))
        d = copy.deepcopy(raw)
        d["regions"][0]["effectiveReset"] = True
        self.assertNotEqual(key, cost.digest(cost.final_plan(d)))

    def test_semantic_support_motion_is_not_a_new_native_footprint(self):
        raw = sequence()["children"][0]["actual"]["captureDiagnostics"]["executionEvidence"]["executions"][0]
        raw["regions"][0]["roi"] = {"inferenceContext": {"width": 8, "height": 8}, "samplingSupport": {"width": 2}}
        key = cost.digest(cost.final_plan(raw))
        raw["regions"][0]["roi"]["samplingSupport"]["width"] = 4
        self.assertEqual(key, cost.digest(cost.final_plan(raw)))
        raw["regions"][0]["roi"]["inferenceContext"]["width"] = 9
        self.assertNotEqual(key, cost.digest(cost.final_plan(raw)))

    def test_too_few_observations_and_floor_are_explicit(self):
        self.assertIn("fewer_than_30_unique_observations", cost.summarize(sequence(count=2))["reasons"])
        result = cost.stats([1, 2, 3, 20])
        self.assertEqual(result["minimum"], 1)
        self.assertEqual(result["median"], 2.5)
        self.assertEqual(result["p95"], 20)


if __name__ == "__main__":
    unittest.main()
