"""Adversarial evidence checks, using the real runner and process-double fixture."""
import copy
import sys
from pathlib import Path
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import runner_test as runner
sample = runner.sample
import assess


class EvidenceTests(unittest.TestCase):
    def pair(self, **kwargs): return [[sample(0, **kwargs), sample(1, **kwargs)]]

    def test_mode_and_profile_cannot_be_omitted_or_wrong(self):
        for field in ("effectiveMode", "profile", "rect", "sourceFormat", "outputFormat"):
            group = self.pair(); group[0][0]["source"].pop(field)
            self.assertFalse(assess.assess_samples(group, True)["valid"], field)
        group = self.pair(); group[0][0]["source"]["effectiveMode"] = "legacy_raw"
        self.assertFalse(assess.assess_samples(group, True)["valid"])

    def test_invalid_profile_does_not_become_manual(self):
        for field in assess.PROFILE_KEYS:
            group = self.pair(); group[0][0]["source"]["profile"].pop(field)
            self.assertFalse(assess.assess_samples(group, True)["valid"], field)

    def test_actual_sample_count_must_match_extent(self):
        group = self.pair(); group[0][0]["source"]["rect"] = [0, 0, 64, 64]
        self.assertFalse(assess.assess_samples(group, True)["valid"])

    def test_inconsistent_exposure_is_not_valid_evidence(self):
        for index, value in ((20, -1), (21, 2), (22, 2), (18, 2)):
            group = self.pair(captured=True); group[0][0]["values"][index] = value
            self.assertFalse(assess.assess_samples(group, True)["valid"], index)

    def test_numeric_overflow_and_bool_are_not_measurements(self):
        self.assertFalse(assess.number(10**400)); self.assertFalse(assess.number(True))
        group = self.pair(); group[0][0]["values"][1] = 10**400
        self.assertFalse(assess.assess_samples(group, True)["valid"])

    def test_modular_frame_floor_ignores_unknown(self):
        slots = [{"sourceWorldFrame": f, "insertionPoint": 0} for f in (0xfffffffe, 2, 0xffffffff)]
        self.assertEqual(assess.frame_floor({"slots": slots}, 0), 2)
        self.assertIsNone(assess.frame_floor({"slots": slots}, 1))


class WorkflowTests(unittest.TestCase):
    # Reuse the process fixture, not duplicate execution of its entire test suite.
    setUp = runner.RunnerTests.setUp
    run_campaign = runner.RunnerTests.run_campaign
    requests = runner.RunnerTests.requests
    def test_v3_campaign_uses_complete_batches(self):
        original = self.f.status
        def batched(label):
            status = original(label)
            status["apiVersion"] = 3
            status["measurementBatches"] = []
            items = status.get("measurements", [])
            if items:
                source = items[0]["source"]
                key = {field: source[field] for field in ("frame", "sourceWorldFrame", "generation", "revision", "insertionPoint")}
                key.update(measurementBatchId=source["frame"] + 1, expectedMeasurementSlotMask=3, atomicColourBatch=True)
                for item in items: item["source"].update(key)
                status["measurementBatches"] = [{**key, "measurements": items}]
            return status
        self.f.status = batched
        report = self.run_campaign()
        self.assertTrue(report["ok"], report.get("error"))
        self.assertEqual(report["regionCompleteness"], "immutable_batch_manifest")
        self.assertIs(report["outputCommitVerified"], False)

    def test_wrong_effective_profile_stops_campaign(self):
        original = self.f.status
        def wrong(label):
            status = original(label)
            if label == "sample":
                for item in status["measurements"]: item["source"]["effectiveMode"] = "legacy_raw"
            return status
        self.f.status = wrong
        report = self.run_campaign()
        self.assertFalse(report["ok"]); self.assertTrue(report["restored"])
        self.assertIn("mode/profile", report["error"])

    def test_keyboard_interrupt_during_mutation_never_blindly_restores(self):
        original = self.f.run
        def interrupt(command, **kwargs):
            result = original(command, **kwargs)
            if self.f.option(command, "-EvidenceLabel") == "identity_native-transport":
                raise KeyboardInterrupt()
            return result
        with patch.object(assess.subprocess, "run", interrupt): report = self.run_campaign()
        self.assertFalse(report["restored"]); self.assertFalse(report["ok"])
        self.assertNotIn("restore", [label for label, _ in self.requests()])

    def test_two_routes_do_not_count_as_two_source_frames(self):
        self.f.args.sample_frames = 2
        base = [sample(i) for i in (0, 1, 2, 3)]
        class Fixed:
            def call(self, *a, **kw):
                return {"revision": 7, "slots": [i["source"] for i in base], "measurements": copy.deepcopy(base)}
        with self.assertRaises(assess.CandidateUnavailable):
            assess.collect(Fixed(), 7, 0, 90, self.f.args)

    def test_no_claim_of_final_presentation(self):
        report = self.run_campaign()
        self.assertTrue(report["ok"])
        self.assertIs(report["outputCommitVerified"], False)
        self.assertIs(report["domainVerified"], False)


if __name__ == "__main__": unittest.main()
