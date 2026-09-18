"""Synthetic execution provenance tests; no runtime or image-quality claims."""
from pathlib import Path
import copy
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/nr-color"))
import transaction_evidence as tx
import hmd_assess


def texture(size=8):
    return {"capacityGrid": {"width": size, "height": size}, "format": 10,
            "work": {"x": 0, "y": 0, "width": size, "height": size},
            "capacityPixels": size * size, "workPixels": size * size,
            "capacityLogicalBytes": size * size * 8, "workLogicalBytes": size * size * 8}


def fixture(mode="full_resolution", eyes=2, route="main", *, no_work=False):
    context = {"sourceTransactionId": 101, "captureEpoch": 5, "configurationEpoch": 7,
               "mode": mode, "fovOnly": False,
               "sourceContext": "render_resolution_before_dlss" if mode == "reduced_resolution" else "final_ldr_before_ui",
               "jitterPixels": [0.25, -0.5], "sourceColorOrigin": [0, 0], "sourceGuideOrigin": [0, 0]}
    grid = {"fullInput": {"width": 8, "height": 8}, "input": {"left": 0, "top": 0, "right": 8, "bottom": 8},
            "fullOutput": {"width": 8, "height": 8}, "output": {"left": 0, "top": 0, "right": 8, "bottom": 8}}
    regions = []
    for eye in range(eyes):
        slot = eye + (2 if route == "submit" else 0)
        regions.append({"physicalSlot": slot, "logicalSlot": slot, "eye": eye, "region": 0,
                        "regionIdentity": 0, "clusterIdentity": 0, "source": copy.deepcopy(context),
                        "nrInput": texture(), "nrOutput": texture(), "nrDepthGuide": texture(), "nrMotionGuide": texture(),
                        "controlMask": texture(0), "nrViewport": copy.deepcopy(grid), "motionVectorScale": [8, 8],
                        "characterSelection": False, "featureUpscaling": False, "depthSourceFormat": 45, "depthViewFormat": 46,
                        "evaluationAttempted": True, "evaluationSucceeded": True, "privateOutputCommitted": True,
                        "timing": {"evaluationGpu": {"state": "pending", "microseconds": None, "clock": "d3d12_nr_queue"}}})
    mask = sum(1 << r["physicalSlot"] for r in regions)
    execution = {"submissionId": 501, "source": copy.deepcopy(context), "frame": 20, "sourceWorldFrame": 19,
                 "generation": 3, "colorRevision": 9, "inputEpoch": 2, "route": route, "legacyInsertionPoint": "final_ldr_pre_ui",
                 "logicalEyeCount": eyes, "plannedRegionCount": eyes, "plannedPhysicalSlotMask": mask,
                 "actualEvaluationCount": eyes, "activeEvaluationPixels": eyes * 64, "attemptedPhysicalSlotMask": mask,
                 "succeededPhysicalSlotMask": mask, "privateCommittedPhysicalSlotMask": mask,
                 "transportBypass": False, "regions": regions, "finished": True, "succeeded": True,
                 "configurationFingerprint": "fixed-colour-configuration",
                 "colourExposureConfiguration": {"mode": "preserve_source", "lightingPreservation": 0.5, "exposureScale": 1.0}}
    envelope = {**context, "schemaVersion": 1, "publicationSequence": 11, "frame": 20, "sourceWorldFrame": 19, "generation": 3,
                "route": route, "logicalEyeCount": eyes, "executions": [] if no_work else [execution],
                "sourceContexts": [copy.deepcopy(context) for _ in range(eyes)], "sourceStages": [], "characters": [],
                "dlssDispatches": [], "droppedStageCount": 0, "rendererEvidenceIncomplete": False,
                "executionEvidenceFailures": 0, "sourceEvidenceFailures": 0,
                "workOutcome": ["NoWork" if no_work else "successful"] * eyes,
                "producerBoundary": {"outcome": "NoWork" if no_work else "successful", "pairComplete": True,
                                     "committedEyeMask": 0 if no_work else (1 << eyes) - 1, "bypassedEyeMask": 0}}
    outer = {k: envelope[k] for k in ("sourceTransactionId", "publicationSequence", "captureEpoch", "configurationEpoch", "generation", "route")}
    outer["configurationFingerprint"] = "fixed-colour-configuration"
    outer.update({eye: {"frame": 20, "sourceWorldFrame": 19, "colorRevision": 9, "inputEpoch": 2}
                  for eye in ("left", "right")[:eyes]})
    outer["executionEvidence"] = envelope
    delayed = {"executionEvidence": copy.deepcopy(envelope)}
    delayed["executionEvidence"]["finalized"] = True
    return outer, delayed


def character_fixture(envelope, eye=0, *, support=False, reused=False):
    key = {field: envelope[field] for field in ("frame", "sourceWorldFrame", "generation", "captureEpoch")}
    key.update(eye=eye, logicalSlot=eye + (2 if envelope["route"] == "submit" else 0), contentSerial=65,
               settingsKey=901, viewport={"input": {"left": 0, "top": 0, "right": 8, "bottom": 8}},
               capturedJitterPixels=[0.25, -0.5])
    value = {"available": True, "key": key, "outcome": "success", "prepared": True,
             "requiresEvaluation": True, "reused": reused, "computeSubrect": {}, "regions": []}
    if support:
        value["maskSupport"] = {"producer": copy.deepcopy(key), "state": "pending", "pixels": None}
    return value


class TransactionEvidenceTests(unittest.TestCase):
    def test_modes_routes_and_mono_stereo(self):
        for mode in ("full_resolution", "foveated", "reduced_resolution"):
            for eyes in (1, 2):
                for route in ("main", "submit"):
                    with self.subTest(mode=mode, eyes=eyes, route=route):
                        frozen, delayed = fixture(mode, eyes, route)
                        for r in delayed["executionEvidence"]["executions"][0]["regions"]:
                            r["timing"]["evaluationGpu"].update(state="complete", microseconds=0)
                        joined = tx.join_execution_evidence(frozen, delayed)
                        self.assertEqual(joined["companionState"], "joined")
                        self.assertEqual(joined["executionEvidence"]["executions"][0]["actualEvaluationCount"], eyes)
                        self.assertEqual(frozen["executionEvidence"]["executions"][0]["regions"][0]["timing"]["evaluationGpu"]["state"], "pending")

    def test_all_producer_identity_mismatches_rejected(self):
        for field in tx.IDENTITY:
            frozen, delayed = fixture()
            value = delayed["executionEvidence"][field]
            delayed["executionEvidence"][field] = not value if type(value) is bool else value + 1 if type(value) is int else value + "_other"
            with self.subTest(field=field), self.assertRaises(tx.TransactionEvidenceError):
                tx.join_execution_evidence(frozen, delayed)

    def test_actual_region_context_cannot_use_latest_settings(self):
        for field in tx.CONTEXT_IDENTITY:
            frozen, _ = fixture()
            source = frozen["executionEvidence"]["executions"][0]["regions"][0]["source"]
            source[field] = "newer"
            with self.subTest(field=field), self.assertRaisesRegex(tx.TransactionEvidenceError, "physical region context"):
                tx.join_execution_evidence(frozen)

    def test_no_work_has_no_inference_or_fake_timing(self):
        frozen, delayed = fixture(no_work=True)
        self.assertEqual(tx.join_execution_evidence(frozen, delayed)["executionEvidence"]["workOutcome"], ["NoWork", "NoWork"])
        frozen, _ = fixture()
        frozen["executionEvidence"]["workOutcome"][0] = "NoWork"
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "NoWork"):
            tx.join_execution_evidence(frozen)

    def test_failure_and_presentation_fallback_remain_distinct(self):
        frozen, _ = fixture(eyes=1)
        envelope = frozen["executionEvidence"]
        envelope["producerBoundary"]["outcome"] = "fallback"
        envelope["producerBoundary"]["committedEyeMask"] = 0
        self.assertEqual(tx.join_execution_evidence(frozen)["executionEvidence"]["workOutcome"], ["successful"])
        execution = envelope["executions"][0]
        execution["regions"][0].update(evaluationSucceeded=False, privateOutputCommitted=False)
        execution.update(succeeded=False, succeededPhysicalSlotMask=0, privateCommittedPhysicalSlotMask=0)
        envelope["workOutcome"] = ["failed"]
        self.assertEqual(tx.join_execution_evidence(frozen)["executionEvidence"]["workOutcome"], ["failed"])

    def test_missing_and_unavailable_companions_are_explicit(self):
        frozen, _ = fixture()
        self.assertEqual(tx.join_execution_evidence(frozen)["companionState"], "absent")
        joined = tx.join_execution_evidence(frozen, {"executionEvidence": {"available": False, "reason": "capture_retention_capacity_exhausted"}})
        self.assertEqual(joined["companionState"], "unavailable")
        self.assertEqual(joined["companionReason"], "capture_retention_capacity_exhausted")
        self.assertIsNone(tx.join_execution_evidence({"legacy": True}))

    def test_attempt_count_and_pixel_area_are_actual(self):
        for field in ("actualEvaluationCount", "activeEvaluationPixels", "attemptedPhysicalSlotMask"):
            frozen, _ = fixture()
            frozen["executionEvidence"]["executions"][0][field] += 1
            with self.subTest(field=field), self.assertRaises(tx.TransactionEvidenceError):
                tx.join_execution_evidence(frozen)

    def test_pending_unavailable_and_failed_values_must_be_null(self):
        for state in ("pending", "unavailable", "failed"):
            frozen, _ = fixture()
            timing = frozen["executionEvidence"]["executions"][0]["regions"][0]["timing"]["evaluationGpu"]
            timing.update(state=state, microseconds=None)
            tx.join_execution_evidence(frozen)
            timing["microseconds"] = 0
            with self.subTest(state=state), self.assertRaisesRegex(tx.TransactionEvidenceError, "availability/value mismatch"):
                tx.join_execution_evidence(frozen)

    def test_missing_complete_nonfinite_negative_and_bool_timing_rejected(self):
        for value in (None, float("nan"), float("inf"), -1, True):
            frozen, _ = fixture()
            frozen["executionEvidence"]["executions"][0]["regions"][0]["timing"]["evaluationGpu"].update(state="complete", microseconds=value)
            with self.subTest(value=value), self.assertRaises(tx.TransactionEvidenceError):
                tx.join_execution_evidence(frozen)

    def test_delayed_geometry_and_jitter_are_immutable(self):
        for mutate in (lambda r: r["nrViewport"]["input"].update(left=1),
                       lambda r: r["motionVectorScale"].__setitem__(0, 9),
                       lambda r: r["source"]["jitterPixels"].__setitem__(0, 0.5),
                       lambda r: r["nrOutput"].update(format=26)):
            frozen, delayed = fixture()
            mutate(delayed["executionEvidence"]["executions"][0]["regions"][0])
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "delayed physical descriptor"):
                tx.join_execution_evidence(frozen, delayed)

    def test_mixed_empty_eye_and_reordered_delayed_regions(self):
        frozen, _ = fixture()
        envelope = frozen["executionEvidence"]
        execution = envelope["executions"][0]
        execution["regions"].pop()
        execution.update(logicalEyeCount=1, plannedRegionCount=1, plannedPhysicalSlotMask=1, actualEvaluationCount=1,
                         activeEvaluationPixels=64, attemptedPhysicalSlotMask=1, succeededPhysicalSlotMask=1, privateCommittedPhysicalSlotMask=1)
        envelope["workOutcome"] = ["successful", "NoWork"]
        self.assertEqual(tx.join_execution_evidence(frozen)["executionEvidence"]["workOutcome"][1], "NoWork")
        frozen, delayed = fixture()
        delayed["executionEvidence"]["executions"][0]["regions"].reverse()
        self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")

    def test_wrong_outer_frame_and_color_rejected(self):
        for field in ("frame", "sourceWorldFrame", "colorRevision", "inputEpoch"):
            frozen, _ = fixture()
            frozen["left"][field] += 1
            with self.subTest(field=field), self.assertRaises(tx.TransactionEvidenceError):
                tx.join_execution_evidence(frozen)

    def test_delayed_producer_fallback_cannot_change(self):
        frozen, delayed = fixture()
        delayed["executionEvidence"]["producerBoundary"]["outcome"] = "fallback"
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "delayed frozen producer"):
            tx.join_execution_evidence(frozen, delayed)

    def test_same_source_other_publication_is_not_a_companion(self):
        frozen, delayed = fixture()
        delayed["executionEvidence"]["publicationSequence"] += 1
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "publicationSequence"):
            tx.join_execution_evidence(frozen, delayed)
        frozen, _ = fixture()
        frozen["publicationSequence"] += 1
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "publicationSequence"):
            tx.join_execution_evidence(frozen)

    def test_frozen_colour_and_depth_formats_cannot_change(self):
        for field in ("colourExposureConfiguration", "configurationFingerprint"):
            for operation in ("replace", "remove"):
                frozen, delayed = fixture()
                execution = delayed["executionEvidence"]["executions"][0]
                if operation == "remove":
                    del execution[field]
                else:
                    execution[field] = {"lightingPreservation": 1.0} if field == "colourExposureConfiguration" else "latest"
                with self.subTest(field=field, operation=operation), self.assertRaisesRegex(tx.TransactionEvidenceError, "colour configuration"):
                    tx.join_execution_evidence(frozen, delayed)
        for field in ("depthSourceFormat", "depthViewFormat"):
            frozen, delayed = fixture()
            delayed["executionEvidence"]["executions"][0]["regions"][0][field] += 1
            with self.subTest(field=field), self.assertRaisesRegex(tx.TransactionEvidenceError, "depth formats"):
                tx.join_execution_evidence(frozen, delayed)
        frozen, _ = fixture()
        frozen["executionEvidence"]["executions"][0]["configurationFingerprint"] = "latest"
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "execution configuration"):
            tx.join_execution_evidence(frozen)

    def test_missing_renderer_evidence_is_unavailable_before_quality_campaign(self):
        frozen, _ = fixture()
        envelope = frozen["executionEvidence"]
        envelope.update(executions=[], rendererEvidenceIncomplete=True, executionEvidenceFailures=1)
        joined = tx.join_execution_evidence(frozen)
        self.assertFalse(joined["available"])
        self.assertEqual(joined["reason"], "renderer_evidence_incomplete")
        self.assertEqual(joined["executionEvidence"]["executions"], [])
        with self.assertRaisesRegex(hmd_assess.EvidenceError, "NR execution attribution unavailable: renderer_evidence_incomplete"):
            hmd_assess.check_nr(frozen, {}, {}, {})
        envelope["rendererEvidenceIncomplete"] = False
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "not marked unavailable"):
            tx.join_execution_evidence(frozen)

    def test_invalid_failure_counts_and_delayed_availability_changes_rejected(self):
        for value in (True, -1, 0.5, None):
            frozen, _ = fixture()
            frozen["executionEvidence"]["executionEvidenceFailures"] = value
            with self.subTest(value=value), self.assertRaisesRegex(tx.TransactionEvidenceError, "failure count"):
                tx.join_execution_evidence(frozen)
        frozen, delayed = fixture()
        delayed["executionEvidence"].update(rendererEvidenceIncomplete=True, sourceEvidenceFailures=1)
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "delayed frozen producer"):
            tx.join_execution_evidence(frozen, delayed)

    def test_delayed_input_preparation_timing_can_complete(self):
        frozen, delayed = fixture()
        for value, state, ms in ((frozen, "pending", None), (delayed, "ready", 0.125)):
            value["executionEvidence"]["executions"][0]["regions"][0]["timing"]["sourceInputPreparation"] = {
                "captureId": 900,
                "cpu": {"state": state, "inclusiveMs": ms, "selfMs": ms, "clock": "cpu_steady_clock"}}
        self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")

    def test_delayed_handles_cannot_be_replaced_by_latest_invocations(self):
        for owner in ("execution", "region", "stage", "character"):
            frozen, _ = fixture()
            envelope = frozen["executionEvidence"]
            handle = {"captureId": 123,
                      "gpu": {"state": "pending", "inclusiveMs": None, "selfMs": None}}
            if owner == "execution":
                envelope["executions"][0]["timing"] = {"wholeNrLegacy": handle}
            elif owner == "region":
                envelope["executions"][0]["regions"][0]["timing"]["sourceInputPreparation"] = handle
            elif owner == "stage":
                envelope["sourceStages"] = [{"name": "composite", "eye": 0, "frame": 20, "sourceWorldFrame": 19,
                    "generation": 3, "matchesProducer": True, "dirtyDispatchPixels": 64, "copiedLogicalBytes": None,
                    "timing": handle}]
            else:
                character = character_fixture(envelope)
                character["sourceCapture"] = {"timing": {"bounds": handle}}
                envelope["characters"] = [character]
            delayed = {"executionEvidence": copy.deepcopy(envelope)}
            delayed["executionEvidence"]["finalized"] = True
            if owner == "execution":
                current = delayed["executionEvidence"]["executions"][0]["timing"]["wholeNrLegacy"]
            elif owner == "region":
                current = delayed["executionEvidence"]["executions"][0]["regions"][0]["timing"]["sourceInputPreparation"]
            elif owner == "stage":
                current = delayed["executionEvidence"]["sourceStages"][0]["timing"]
            else:
                current = delayed["executionEvidence"]["characters"][0]["sourceCapture"]["timing"]["bounds"]
            current["gpu"].update(state="ready", inclusiveMs=0.3, selfMs=0.2)
            with self.subTest(owner=owner):
                self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")
                current["captureId"] = 124
                with self.assertRaisesRegex(tx.TransactionEvidenceError, "timing captureId mismatch"):
                    tx.join_execution_evidence(frozen, delayed)
                del current["captureId"]
                with self.assertRaisesRegex(tx.TransactionEvidenceError, "timing captureId mismatch"):
                    tx.join_execution_evidence(frozen, delayed)
                del handle["captureId"]
                self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")
                current["captureId"] = 0
                with self.assertRaisesRegex(tx.TransactionEvidenceError, "timing captureId mismatch"):
                    tx.join_execution_evidence(frozen, delayed)

    def test_character_key_must_match_current_transaction_eye_and_route(self):
        for route in ("main", "submit"):
            for field in ("frame", "sourceWorldFrame", "generation", "captureEpoch", "eye", "logicalSlot"):
                frozen, _ = fixture(route=route)
                envelope = frozen["executionEvidence"]
                envelope["characters"] = [character_fixture(envelope, eye) for eye in range(2)]
                self.assertTrue(tx.join_execution_evidence(frozen)["available"])
                envelope["characters"][1]["key"][field] += 1
                with self.subTest(route=route, field=field), self.assertRaises(tx.TransactionEvidenceError):
                    tx.join_execution_evidence(frozen)

    def test_character_support_is_bound_to_exact_contents(self):
        for field in tx.CHARACTER_CONTENT_IDENTITY:
            frozen, _ = fixture()
            envelope = frozen["executionEvidence"]
            character = character_fixture(envelope, support=True)
            envelope["characters"] = [character]
            producer = character["maskSupport"]["producer"]
            producer[field] = producer[field] + 1 if type(producer[field]) is int else "different"
            with self.subTest(field=field), self.assertRaisesRegex(tx.TransactionEvidenceError, "character support producer"):
                tx.join_execution_evidence(frozen)

    def test_reused_support_preserves_original_evaluation_frame(self):
        frozen, _ = fixture()
        envelope = frozen["executionEvidence"]
        character = character_fixture(envelope, support=True, reused=True)
        envelope["characters"] = [character]
        producer = character["maskSupport"]["producer"]
        producer["frame"] = envelope["sourceWorldFrame"]
        self.assertTrue(tx.join_execution_evidence(frozen)["available"])
        delayed = {"executionEvidence": copy.deepcopy(envelope)}
        delayed["executionEvidence"]["finalized"] = True
        delayed_support = delayed["executionEvidence"]["characters"][0]["maskSupport"]
        delayed_support.update(state="ready", pixels=17)
        self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")
        delayed_support["producer"]["frame"] = envelope["frame"]
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "delayed character support"):
            tx.join_execution_evidence(frozen, delayed)
        character["reused"] = False
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "character support producer"):
            tx.join_execution_evidence(frozen)
        character["reused"] = True
        for frame in (envelope["frame"] + 1, envelope["sourceWorldFrame"] - 1):
            producer["frame"] = frame
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "source lifetime"):
                tx.join_execution_evidence(frozen)

    def test_failed_preparation_and_absent_support_do_not_require_fake_content(self):
        frozen, _ = fixture(eyes=1)
        envelope = frozen["executionEvidence"]
        character = character_fixture(envelope)
        character.update(outcome="failed", prepared=False)
        character["key"]["contentSerial"] = 0
        character["key"]["viewport"] = {}
        character["maskSupport"] = {"state": "unavailable", "pixels": None}
        envelope["characters"] = [character, {"available": False, "reason": "preparation_evidence_unavailable"}]
        self.assertTrue(tx.join_execution_evidence(frozen)["available"])

    def test_recorded_explicit_wait_is_measured_without_invented_state(self):
        frozen, _ = fixture()
        wait = {"operation": "WaitForSingleObject", "microseconds": 42, "result": 0,
                "error": 0, "timeoutMilliseconds": 100, "clock": "cpu_steady_clock"}
        frozen["executionEvidence"]["executions"][0]["timing"] = {"waitSamples": [wait]}
        self.assertTrue(tx.join_execution_evidence(frozen)["available"])
        wait["microseconds"] = None
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "observed wait sample"):
            tx.join_execution_evidence(frozen)

    def test_delayed_evidence_publication_failure_is_explicit(self):
        frozen, delayed = fixture()
        frozen["executionEvidence"]["executions"][0]["evidenceFailed"] = False
        delayed["executionEvidence"]["executions"][0]["evidenceFailed"] = True
        joined = tx.join_execution_evidence(frozen, delayed)
        self.assertFalse(joined["available"])
        self.assertEqual(joined["reason"], "execution_evidence_failed")
        self.assertEqual(joined["companionState"], "joined")
        with self.assertRaisesRegex(hmd_assess.EvidenceError, "execution_evidence_failed"):
            hmd_assess.check_nr(frozen, {}, {}, {}, capture_diagnostics=delayed)
        delayed["executionEvidence"]["executions"][0]["evidenceFailed"] = 1
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "failure state"):
            tx.join_execution_evidence(frozen, delayed)

    def test_hmd_importer_validates_optional_evidence_before_campaign_rules(self):
        frozen, _ = fixture()
        frozen["executionEvidence"]["executions"][0]["actualEvaluationCount"] = 77
        with self.assertRaisesRegex(hmd_assess.EvidenceError, "NR execution attribution"):
            hmd_assess.check_nr(frozen, {}, {}, {})
        no_work, _ = fixture(no_work=True)
        tx.join_execution_evidence(no_work)
        with self.assertRaisesRegex(hmd_assess.EvidenceError, "NR attribution unavailable"):
            hmd_assess.check_nr(no_work, {}, {}, {})


if __name__ == "__main__":
    unittest.main()
