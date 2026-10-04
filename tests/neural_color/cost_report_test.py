"""Synthetic cost-admission regressions; no GPU performance claims."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/nr-color"))
import cost_report as cost
from transaction_evidence_test import fixture, native_layout, shared_fixture


def sequence(start=0, timing=4, count=32, *, prototype=fixture, configuration=None):
    children = []
    for index in range(start, start + count):
        acquisition, delayed = prototype()
        for envelope in (acquisition["executionEvidence"], delayed["executionEvidence"]):
            envelope.update(frame=index, sourceWorldFrame=index, sourceTransactionId=index + 100,
                            publicationSequence=index + 1)
            for context in envelope["sourceContexts"]:
                context["sourceTransactionId"] = index + 100
            batch = envelope["executions"][0]
            batch.update(frame=index, sourceWorldFrame=index, finished=True, succeeded=True)
            if isinstance(batch.get("sharedContext"), dict):
                batch["sharedContext"].update(frame=index, sourceWorldFrame=index)
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
        acquisition["configuration"] = copy.deepcopy(configuration) if configuration is not None else {
            "upscaling": {"neuralCharacterRegionLimit": 2}, "color": {}}
        children.append({"ordinal": index-start+1, "state": "completed", "actual": {"acquisition": {"nrEvidence": acquisition,
                            "cameraEvidence": {"available": True, "sourceWorldFrame": index, "position": [0, 0, 0]}},
                            "captureDiagnostics": delayed}})
    return {"state": "final", "terminalOutcome": "completed", "continuity": {"complete": True, "requested": count, "acquired": count},
            "counts": {"requested": count, "scheduled": count, "acquired": count, "written": count,
                       "failed": 0, "dropped": 0, "cancelled": 0, "inFlight": 0}, "effective": {"frameCount": count},
            "producer": {"buildId": "synthetic_fixture"}, "children": children}


def triple():
    return [cost.summarize(sequence(start, timing)) for start, timing in ((0, 4), (40, 7), (80, 4))]


def shared_sequence(mode, start=0, *, halo=64):
    setting = {"mode": mode, "halo": halo}
    configuration = {"upscaling": {"neuralCharacterRegionLimit": 2, "neuralRenderingMode": 2},
                     "color": {"settings": {"mode": "legacy_raw", "lightingPreservation": 1.0},
                               "experiments": {"sharedContext": setting, "compactInputs": False,
                                               "sharedSourceTransport": False, "transportBypass": False,
                                               "applyModelEdit": True, "captureFrameEvidence": True,
                                               "upscaled_center": {"exposureSource": "manual", "exposureMultiplier": 1.0}}}}
    owned = [{"x": x, "y": 128, "width": 128, "height": 128} for x in (64, 320)]
    context = {"x": 0, "y": 0, "width": 512, "height": 512} if mode == "full_eye" else {
        "x": 0, "y": 64, "width": 512, "height": 256}

    def prototype():
        pair = shared_fixture()
        for record in pair:
            batch = record["executionEvidence"]["executions"][0]
            batch["colourExposureConfiguration"] = copy.deepcopy(configuration["color"])
            regions = []
            for original in batch["regions"]:
                for index, work in enumerate(owned if mode == "off" else [context]):
                    region = copy.deepcopy(original)
                    area = work["width"] * work["height"]
                    for name in ("nrInput", "nrOutput", "nrDepthGuide", "nrMotionGuide"):
                        region[name].update(work=copy.deepcopy(work), workPixels=area, workLogicalBytes=area * 8)
                    roi = region["roi"]
                    roi.update(inferenceContext=copy.deepcopy(work), inferencePixels=area,
                               ownedOutputs=copy.deepcopy(owned), ownedOutputPixels=32768,
                               samplingSupport={"x": 64, "y": 128, "width": 384, "height": 128},
                               samplingSupportEnclosurePixels=384 * 128)
                    if mode == "off":
                        region.update(physicalSlot=original["logicalSlot"] + 4 * index, region=index)
                        del roi["ownedOutputs"]
                        roi.update(contextPolicy="retained_envelope", ownedOutput=copy.deepcopy(work), ownedOutputPixels=area,
                                   samplingSupport=copy.deepcopy(work), samplingSupportEnclosurePixels=area,
                                   temporalEnvelope=copy.deepcopy(work), temporalEnvelopePixels=area)
                    region["nativeLayout"] = native_layout(region)
                    regions.append(region)
            mask = sum(1 << region["physicalSlot"] for region in regions)
            batch.update(regions=regions, plannedRegionCount=len(regions), actualEvaluationCount=len(regions),
                         activeEvaluationPixels=sum(r["nrOutput"]["workPixels"] for r in regions),
                         plannedPhysicalSlotMask=mask, attemptedPhysicalSlotMask=mask,
                         succeededPhysicalSlotMask=mask, privateCommittedPhysicalSlotMask=mask)
            batch["sharedContext"] = None if mode == "off" else {
                **setting, "phase": "selection_before_native_execution", "applied": True,
                "reason": "shared_context_selected", "outputOwnershipPreserved": True, "productionQualified": False,
                "coordinateDomain": "original_output_crop_local", "plannedEvaluations": 2, "requestedEvaluations": 4,
                "eyes": [{"eye": eye, "inferenceContext": copy.deepcopy(context), "ownedOutputs": copy.deepcopy(owned)}
                         for eye in (0, 1)]}
        return pair

    return sequence(start, prototype=prototype, configuration=configuration)


def shared_triple(mode="enclosing"):
    return [cost.summarize(shared_sequence(kind, start), comparison_axis=cost.SHARED_CONTEXT_AXIS)
            for kind, start in (("off", 0), (mode, 40), ("off", 80))]


class CostReportTest(unittest.TestCase):
    def test_shared_context_axis_is_explicit_and_preserves_raw_configuration(self):
        raw = [shared_sequence(mode, start) for mode, start in (("off", 0), ("enclosing", 40), ("off", 80))]
        default = cost.bracket(*(cost.summarize(value) for value in raw))
        self.assertIn("unmatched_configurations", default["reasons"])
        for mode in ("enclosing", "full_eye"):
            runs = shared_triple(mode)
            result = cost.bracket(*runs, comparison_axis=cost.SHARED_CONTEXT_AXIS)
            self.assertTrue(result["comparable"], result["reasons"])
            self.assertFalse(result["automaticProfileAdoption"])
            self.assertEqual(result["variedConfigurationPath"], "color.experiments.sharedContext")
            self.assertEqual(next(iter(runs[1]["rawConfigurations"].values()))["color"]["experiments"]["sharedContext"]["mode"], mode)
            self.assertIn("neuralCharacterRegionLimit", next(iter(runs[1]["configurations"].values()))["upscaling"])
            self.assertEqual(runs[0]["outputOwnership"], runs[1]["outputOwnership"])
            with self.assertRaisesRegex(ValueError, "comparison axis"):
                cost.bracket(*runs)

    def test_shared_context_axis_does_not_normalize_unrelated_settings(self):
        for section, field, value in (("settings", "lightingPreservation", 0.5),
                                      ("settings", "mode", "managed"),
                                      ("profile", "exposureMultiplier", 2.0),
                                      ("upscaling", "neuralCharacterRegionLimit", 4)):
            raw = shared_sequence("enclosing", 40)
            for child in raw["children"]:
                actual = child["actual"]
                config = actual["acquisition"]["nrEvidence"]["configuration"]
                target = config["upscaling"] if section == "upscaling" else (
                    config["color"]["experiments"]["upscaled_center"] if section == "profile" else config["color"]["settings"])
                target[field] = value
                for record in (actual["acquisition"]["nrEvidence"], actual["captureDiagnostics"]):
                    record["executionEvidence"]["executions"][0]["colourExposureConfiguration"] = copy.deepcopy(config["color"])
            runs = shared_triple()
            runs[1] = cost.summarize(raw, comparison_axis=cost.SHARED_CONTEXT_AXIS)
            result = cost.bracket(*runs, comparison_axis=cost.SHARED_CONTEXT_AXIS)
            self.assertIn("unmatched_configurations", result["reasons"])

    def test_shared_context_axis_requires_applied_frozen_selection(self):
        for mutation in (lambda b: b.update(sharedContext=None),
                         lambda b: b["sharedContext"].update(applied=False),
                         lambda b: b["sharedContext"].update(reason="ineligible_original_plan_retained"),
                         lambda b: b["sharedContext"].update(halo=256),
                         lambda b: b["sharedContext"].update(plannedEvaluations=4),
                         lambda b: b["sharedContext"]["eyes"][0].update(eye=1),
                         lambda b: b["sharedContext"]["eyes"][0].update(eye=False),
                         lambda b: b["sharedContext"]["eyes"][0]["inferenceContext"].update(x=False),
                         lambda b: b["sharedContext"]["eyes"][0]["ownedOutputs"][0].update(x=65)):
            raw = shared_sequence("enclosing", 40)
            actual = raw["children"][0]["actual"]
            for record in (actual["acquisition"]["nrEvidence"], actual["captureDiagnostics"]):
                mutation(record["executionEvidence"]["executions"][0])
            with self.assertRaises(ValueError):
                cost.summarize(raw, comparison_axis=cost.SHARED_CONTEXT_AXIS)

    def test_shared_context_axis_rejects_typed_colour_configuration_mismatch(self):
        raw = shared_sequence("enclosing", 40)
        for child in raw["children"]:
            actual = child["actual"]
            for record in (actual["acquisition"]["nrEvidence"], actual["captureDiagnostics"]):
                batch = record["executionEvidence"]["executions"][0]
                batch["colourExposureConfiguration"]["settings"]["lightingPreservation"] = True
        with self.assertRaisesRegex(ValueError, "colour configuration differs"):
            cost.summarize(raw, comparison_axis=cost.SHARED_CONTEXT_AXIS)

    def test_shared_context_axis_rejects_changed_owned_outputs_and_restores_raw_baseline(self):
        runs = shared_triple()
        changed = copy.deepcopy(runs[1])
        owned = next(iter(changed["outputOwnership"].values()))
        owned["0"]["rects"][0]["x"] += 1
        changed["outputOwnership"] = {cost.digest(owned): owned}
        self.assertIn("original_output_ownership_changed", cost.bracket(runs[0], changed, runs[2],
                                                                       comparison_axis=cost.SHARED_CONTEXT_AXIS)["reasons"])
        after = cost.summarize(shared_sequence("off", 80, halo=128), comparison_axis=cost.SHARED_CONTEXT_AXIS)
        self.assertIn("baseline_raw_configuration_not_restored", cost.bracket(runs[0], runs[1], after,
                                                                               comparison_axis=cost.SHARED_CONTEXT_AXIS)["reasons"])

    def test_shared_context_axis_detects_changed_ownership_in_actual_records(self):
        before, _, after = shared_triple()
        for whole_window in (False, True):
            raw = shared_sequence("enclosing", 40)
            for child in raw["children"] if whole_window else raw["children"][:1]:
                actual = child["actual"]
                for record in (actual["acquisition"]["nrEvidence"], actual["captureDiagnostics"]):
                    batch = record["executionEvidence"]["executions"][0]
                    for region, eye in zip(batch["regions"], batch["sharedContext"]["eyes"]):
                        roi = region["roi"]
                        roi["ownedOutputs"][0]["x"] += 1
                        roi["samplingSupport"]["x"] += 1
                        roi["samplingSupport"]["width"] -= 1
                        roi["samplingSupportEnclosurePixels"] -= roi["samplingSupport"]["height"]
                        eye["ownedOutputs"] = copy.deepcopy(roi["ownedOutputs"])
            candidate = cost.summarize(raw, comparison_axis=cost.SHARED_CONTEXT_AXIS)
            if not whole_window:
                self.assertIn("output_ownership_changed_in_window", candidate["reasons"])
            result = cost.bracket(before, candidate, after, comparison_axis=cost.SHARED_CONTEXT_AXIS)
            self.assertIn("original_output_ownership_changed", result["reasons"])

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

    def test_shared_output_ownership_is_part_of_the_cost_key(self):
        raw = shared_fixture()[0]["executionEvidence"]["executions"][0]
        plan = cost.final_plan(raw)
        key = cost.digest(plan)
        self.assertIsNone(plan["regions"][0]["roi"]["ownedOutput"])
        self.assertEqual(len(plan["regions"][0]["roi"]["ownedOutputs"]), 2)
        raw["regions"][0]["roi"]["ownedOutputs"][0]["x"] += 1
        self.assertNotEqual(key, cost.digest(cost.final_plan(raw)))
        self.assertEqual(plan["regions"][0]["roi"]["ownedOutputs"][0]["x"], 96)

    def test_too_few_observations_and_floor_are_explicit(self):
        self.assertIn("fewer_than_30_unique_observations", cost.summarize(sequence(count=2))["reasons"])
        result = cost.stats([1, 2, 3, 20])
        self.assertEqual(result["minimum"], 1)
        self.assertEqual(result["median"], 2.5)
        self.assertEqual(result["p95"], 20)


if __name__ == "__main__":
    unittest.main()
