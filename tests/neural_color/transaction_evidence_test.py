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


def native_layout(region):
    result = {"coordinateDomain": "resource_local_texels",
              "inputInitializationContract": "valid_rectangles_before_evaluation",
              "nativeReadableFootprint": None, "motionSourceUnits": "full_input_normalized",
              "motionConsumerUnits": "native_guide_pixels", "motionConversion": "native_parameter_scale_once",
              "creationInputExtent": region["nrDepthGuide"]["capacityGrid"],
              "creationOutputExtent": region["nrOutput"]["capacityGrid"],
              "motionVectorScale": region["motionVectorScale"], "featureUpscaling": region["featureUpscaling"]}
    for role, name in (("color", "nrInput"), ("depth", "nrDepthGuide"), ("motion", "nrMotionGuide"),
                       ("output", "nrOutput"), ("controlMask", "controlMask")):
        result[role] = {"backingExtent": region[name]["capacityGrid"], "validRect": region[name]["work"]}
    if region["controlMask"]["capacityPixels"] == 0:
        result["controlMask"] = None
    return copy.deepcopy(result)


class TransactionEvidenceTests(unittest.TestCase):
    def test_roi_roles_stay_with_the_frozen_execution(self):
        frozen, delayed = fixture("reduced_resolution")
        roles = {"coordinateDomain": "output_crop_local",
                 "samplingSupport": {"x": 1, "y": 2, "width": 3, "height": 4},
                 "samplingSupportKind": "conservative_guarded_enclosure",
                 "samplingSupportEnclosurePixels": 12,
                 "ownedOutput": texture()["work"], "ownedOutputPixels": 64,
                 "inferenceContext": texture()["work"], "inferencePixels": 64,
                 "temporalEnvelope": texture()["work"], "temporalEnvelopePixels": 64,
                 "allocationCapacity": texture()["capacityGrid"], "capacityPixels": 64}
        for evidence in (frozen, delayed):
            evidence["executionEvidence"]["executions"][0]["regions"][0]["roi"] = copy.deepcopy(roles)
        tx.join_execution_evidence(frozen, delayed)
        for field in roles:
            with self.subTest(field=field):
                changed = copy.deepcopy(delayed)
                changed["executionEvidence"]["executions"][0]["regions"][0]["roi"][field] = None
                with self.assertRaisesRegex(tx.TransactionEvidenceError, "ROI roles"):
                    tx.join_execution_evidence(frozen, changed)
        for strip_frozen in (False, True):
            original, companion = copy.deepcopy(frozen), copy.deepcopy(delayed)
            changed = original if strip_frozen else companion
            del changed["executionEvidence"]["executions"][0]["regions"][0]["roi"]
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "ROI roles"):
                tx.join_execution_evidence(original, companion)

    def test_character_roi_roles_cannot_be_replaced_by_a_new_plan(self):
        frozen, delayed = fixture()
        for evidence in (frozen, delayed):
            envelope = evidence["executionEvidence"]
            character = character_fixture(envelope, support=True)
            character["roi"] = [{"samplingSupport": None, "inferenceContext": texture()["work"]}]
            envelope["characters"] = [character]
        tx.join_execution_evidence(frozen, delayed)
        changed = copy.deepcopy(delayed)
        changed["executionEvidence"]["characters"][0]["roi"][0]["samplingSupport"] = texture()["work"]
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "character ROI roles"):
            tx.join_execution_evidence(frozen, changed)

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

    def test_renderscale_fov_is_immutable_optional_evidence(self):
        for masked in (False, True):
            frozen, delayed = fixture("reduced_resolution")
            for envelope in (frozen["executionEvidence"], delayed["executionEvidence"]):
                envelope["renderscaleFov"] = masked
                for context in envelope["sourceContexts"]:
                    context["renderscaleFov"] = masked
                for execution in envelope["executions"]:
                    execution["source"]["renderscaleFov"] = masked
                    for region in execution["regions"]:
                        region["source"]["renderscaleFov"] = masked
            self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")
            targets = [delayed["executionEvidence"],
                       delayed["executionEvidence"]["executions"][0]["source"],
                       delayed["executionEvidence"]["executions"][0]["regions"][0]["source"]]
            for target in targets:
                for invalid in (not masked, 0, None, "false"):
                    target["renderscaleFov"] = invalid
                    with self.subTest(masked=masked, invalid=invalid), self.assertRaises(tx.TransactionEvidenceError):
                        tx.join_execution_evidence(frozen, delayed)
                del target["renderscaleFov"]
                with self.assertRaises(tx.TransactionEvidenceError):
                    tx.join_execution_evidence(frozen, delayed)
                target["renderscaleFov"] = masked
        # Older captures omit this additive field on both sides.
        frozen, delayed = fixture("reduced_resolution")
        self.assertEqual(tx.join_execution_evidence(frozen, delayed)["companionState"], "joined")

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

    def test_native_layout_is_frozen_and_legacy_absence_remains_supported(self):
        frozen, delayed = fixture()
        tx.join_execution_evidence(frozen, delayed)
        for record in (frozen, delayed):
            region = record["executionEvidence"]["executions"][0]["regions"][0]
            region["nativeLayout"] = native_layout(region)
        tx.join_execution_evidence(frozen, delayed)
        for mutate in (lambda r: r["nativeLayout"]["creationOutputExtent"].update(width=16),
                       lambda r: r["nativeLayout"]["output"]["validRect"].update(x=2),
                       lambda r: r["nativeLayout"]["motionVectorScale"].__setitem__(0, 3),
                       lambda r: r.update(nativeLayout=None), lambda r: r.pop("nativeLayout")):
            for side in (0, 1):
                pair = copy.deepcopy((frozen, delayed))
                mutate(pair[side]["executionEvidence"]["executions"][0]["regions"][0])
                with self.assertRaisesRegex(tx.TransactionEvidenceError, "native layout"):
                    tx.join_execution_evidence(*pair)
        for record in (frozen, delayed):
            record["executionEvidence"]["executions"][0]["regions"][0]["nativeLayout"] = None
        tx.join_execution_evidence(frozen, delayed)

    def test_native_layout_cannot_contradict_its_own_physical_record(self):
        for mutate in (lambda r: r["nativeLayout"]["creationOutputExtent"].update(width=16),
                       lambda r: r["nativeLayout"]["depth"]["validRect"].update(x=1),
                       lambda r: r["nativeLayout"]["motionVectorScale"].__setitem__(0, 3),
                       lambda r: r["nativeLayout"].update(controlMask={}),
                       lambda r: r["nativeLayout"].update(featureUpscaling=True),
                       lambda r: r["nativeLayout"].update(motionConversion="already_scaled"),
                       lambda r: r["nativeLayout"]["output"]["validRect"].update(x=False),
                       lambda r: r["nativeLayout"]["creationOutputExtent"].update(width=8.0),
                       lambda r: r["nativeLayout"].pop("nativeReadableFootprint"),
                       lambda r: r.update(nativeLayout=[]),
                       lambda r: r["nrViewport"]["fullInput"].update(width=16)):
            frozen, delayed = fixture()
            for record in (frozen, delayed):
                region = record["executionEvidence"]["executions"][0]["regions"][0]
                region["nativeLayout"] = native_layout(region)
                mutate(region)
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "native layout"):
                tx.join_execution_evidence(frozen, delayed)
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "native layout"):
                tx.join_execution_evidence(frozen)

    def test_native_layout_supports_control_mask_and_full_eye_motion_scale(self):
        frozen, delayed = fixture()
        for record in (frozen, delayed):
            region = record["executionEvidence"]["executions"][0]["regions"][0]
            region["controlMask"] = texture()
            region["nrViewport"]["fullInput"] = {"width": 32, "height": 32}
            region["motionVectorScale"] = [32, 32]
            region["nativeLayout"] = native_layout(region)
        tx.join_execution_evidence(frozen, delayed)

    def test_compact_origin_full_initialization_and_stateless_contract(self):
        frozen, delayed = fixture(mode="reduced_resolution")
        for record in (frozen, delayed):
            region = record["executionEvidence"]["executions"][0]["regions"][0]
            region["nrViewport"]["fullInput"] = {"width": 32, "height": 32}
            region["nrViewport"]["output"].update(right=32, bottom=32)
            region["nrViewport"]["fullOutput"] = {"width": 32, "height": 32}
            region["motionVectorScale"] = [32, 32]
            region["effectiveReset"] = True
            region["nativeLayout"] = native_layout(region)
            region["roi"] = {"coordinateDomain": "compact_storage_local",
                             "compactSource": {"x": 4, "y": 3, "width": 8, "height": 8},
                             "allocationCapacity": {"width": 8, "height": 8}, "temporalEnvelope": None,
                             "inferenceContext": {"x": 0, "y": 0, "width": 8, "height": 8},
                             "ownedOutput": {"x": 1, "y": 1, "width": 6, "height": 6},
                             "samplingSupport": {"x": 2, "y": 2, "width": 4, "height": 4}}
        tx.join_execution_evidence(frozen, delayed)
        for mutate in (lambda r: r["roi"]["compactSource"].update(x=30),
                       lambda r: r["roi"]["samplingSupport"].update(x=0),
                       lambda r: r["roi"].update(temporalEnvelope={}),
                       lambda r: r["nrViewport"]["fullOutput"].update(width=8),
                       lambda r: r.update(effectiveReset=False),
                       lambda r: r["roi"]["compactSource"].update(width=16)):
            pair = copy.deepcopy((frozen, delayed))
            for record in pair:
                mutate(record["executionEvidence"]["executions"][0]["regions"][0])
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "compact"):
                tx.join_execution_evidence(*pair)
        for value in ([], None, 0):
            pair = copy.deepcopy((frozen, delayed))
            pair[0]["executionEvidence"]["executions"][0]["regions"][0]["roi"] = value
            with self.assertRaisesRegex(tx.TransactionEvidenceError, "ROI object"):
                tx.join_execution_evidence(*pair)

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

    def test_empty_proof_is_bound_and_immutable(self):
        frozen, delayed = fixture(no_work=True)
        for record in (frozen, delayed):
            envelope = record["executionEvidence"]
            character = character_fixture(envelope)
            character.update(emptyProof="gpu_category_superset", requiresEvaluation=False, outcome="no_work")
            character["key"]["sourceCaptureSerial"] = 17
            envelope["characters"] = [character]
        tx.join_execution_evidence(frozen, delayed)
        for mutate in (lambda c: c.update(emptyProof="cpu_selection"),
                       lambda c: c.pop("emptyProof"),
                       lambda c: c["key"].update(sourceCaptureSerial=18)):
            changed = copy.deepcopy(delayed)
            mutate(changed["executionEvidence"]["characters"][0])
            with self.assertRaises(tx.TransactionEvidenceError):
                tx.join_execution_evidence(frozen, changed)
        for mutate in (lambda c: c.update(requiresEvaluation=True),
                       lambda c: c.update(emptyProof="none"),
                       lambda c: c["key"].update(sourceCaptureSerial=0),
                       lambda c: c["key"].pop("sourceCaptureSerial")):
            changed = copy.deepcopy(frozen)
            mutate(changed["executionEvidence"]["characters"][0])
            with self.assertRaises(tx.TransactionEvidenceError):
                tx.join_execution_evidence(changed)

    def test_support_source_capture_cannot_cross_prepared_contents(self):
        frozen, _ = fixture()
        envelope = frozen["executionEvidence"]
        character = character_fixture(envelope, support=True)
        character["key"]["sourceCaptureSerial"] = 17
        character["maskSupport"]["producer"]["sourceCaptureSerial"] = 17
        envelope["characters"] = [character]
        tx.join_execution_evidence(frozen)
        character["maskSupport"]["producer"]["sourceCaptureSerial"] = 18
        with self.assertRaisesRegex(tx.TransactionEvidenceError, "source capture"):
            tx.join_execution_evidence(frozen)

    def test_empty_proof_rejects_non_boolean_states(self):
        for field in ("prepared", "requiresEvaluation"):
            for value in (None, 0, 1, "false"):
                frozen, _ = fixture(no_work=True)
                character = character_fixture(frozen["executionEvidence"])
                character.update(emptyProof="cpu_selection", requiresEvaluation=False, outcome="no_work")
                character["key"]["sourceCaptureSerial"] = 17
                character[field] = value
                frozen["executionEvidence"]["characters"] = [character]
                with self.subTest(field=field, value=value), self.assertRaisesRegex(
                        tx.TransactionEvidenceError, "preparation/evaluation state"):
                    tx.join_execution_evidence(frozen)

    def test_source_capture_matches_prepared_proof(self):
        frozen, _ = fixture(no_work=True)
        character = character_fixture(frozen["executionEvidence"])
        character.update(emptyProof="gpu_category_superset", requiresEvaluation=False, outcome="no_work")
        character["key"]["sourceCaptureSerial"] = 17
        source = {"available": True, "boundsCaptureSerial": 17,
                  **{k: character["key"][k] for k in ("sourceWorldFrame", "captureEpoch")}}
        character["sourceCapture"] = source
        frozen["executionEvidence"]["characters"] = [character]
        tx.join_execution_evidence(frozen)
        for field in ("sourceWorldFrame", "captureEpoch", "boundsCaptureSerial"):
            changed = copy.deepcopy(frozen)
            changed["executionEvidence"]["characters"][0]["sourceCapture"][field] += 1
            with self.subTest(field=field), self.assertRaisesRegex(tx.TransactionEvidenceError, "source capture"):
                tx.join_execution_evidence(changed)
        character["sourceCapture"] = {"available": False, "reason": "source_not_captured_in_epoch"}
        tx.join_execution_evidence(frozen)

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
