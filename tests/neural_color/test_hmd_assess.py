"""Synthetic fixtures test evidence/measurement contracts, not HMD quality."""
from __future__ import annotations

import copy
from datetime import datetime, timedelta, timezone
import importlib.util
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / "tools" / "nr-color" / "hmd_assess.py"
sys.path.insert(0, str(SOURCE.parent))
SPEC = importlib.util.spec_from_file_location("hmd_assess", SOURCE)
HMD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = HMD
SPEC.loader.exec_module(HMD)
try:
    import numpy as np
    from PIL import Image
    import jsonschema
    HAS_IMAGES = True
except ImportError:
    HAS_IMAGES = False


def region_policy():
    regions = [{"id": kind, "class": kind, "description": "Synthetic " + kind, "rect": [0, 0, 6, 6]}
               for kind in sorted(HMD.CLASSES)]
    return {"schemaVersion": 1, "sceneFingerprint": "synthetic-fixed-scene", "motionGradientMismatchLimit": 0.15,
            "eyes": {eye: {"nativeSize": [8, 8], "regions": copy.deepcopy(regions), "unavailable": {}} for eye in HMD.EYES}}


def specification():
    return {"sceneFingerprint": "synthetic-fixed-scene", "fixedScene": {"fixture": "synthetic", "cameraEvidence": camera_plan(),
                "recordingScene": {"cell": "fixture", "cellFormID": 1, "interior": True, "weatherFormID": 0},
                "gameHour": 12, "gameHourTolerance": 0.1},
            "fixedSettings": {"FOV": 0.95, "neuralRenderingInsertionPoint": 0}, "candidates": [
                {"condition": kind, "settings": {"settings": {"mode": "legacy_raw"}}}
                for kind in ("nr_off", "raw", "managed_identity", "preserve_source")]}


def camera_plan():
    matrix = [float(index % 5 == 0) for index in range(16)]
    return {"view": [matrix, matrix], "projection": [matrix, matrix], "positionAdjust": [[0.0] * 4, [0.0] * 4],
            "viewTolerance": 0.0, "projectionTolerance": 0.0, "positionAdjustTolerance": 0.0}


def artifact(path, actual=None):
    result = {"path": str(path), "bytes": path.stat().st_size, "sha256": HMD.digest(path), "committed": True}
    if actual is not None:
        result["actual"] = actual
    return result


def descriptor():
    return {"source": {"kind": "hmd_submission", "fallback": "reject"}, "outputs": [
        {"view": eye + "_eye", "encoding": {"format": "png", "colourContract": "sdr_srgb"}} for eye in HMD.EYES]}


def plane(eye):
    return {"eye": eye, "sourceWidth": 16, "sourceHeight": 8, "stagedWidth": 8, "stagedHeight": 8,
            "dxgiFormat": 28, "colourSpace": 1, "boundsApplied": True,
            "submittedBounds": {"uMin": 0 if eye == "left" else 0.5, "uMax": 0.5 if eye == "left" else 1,
                                "vMin": 0, "vMax": 1},
            "orientation": {"flipHorizontal": False, "flipVertical": False},
            "publicationGeneration": 3, "deviceIdentity": "0x123", "tonemapSceneHdr": False}


def expected(kind="raw", apply_edit=True):
    profile = {"domain": "unknown", "transform": "identity", "exposureSource": "manual", "exposureMultiplier": 1}
    mode = HMD.MODE[kind]
    return {"configurationFingerprint": "test-fingerprint", "configuration": {
                "upscaling": {"FOV": 0.95, "neuralRenderingEnabled": kind != "nr_off", "neuralRenderingInsertionPoint": 0},
                "color": {"settings": {"enabled": True, "mode": mode}, "experiments": {
                    "applyModelEdit": apply_edit, "transportBypass": False, "captureFrameEvidence": True,
                    "upscaled_center": copy.deepcopy(profile), "final_ldr_pre_ui": copy.deepcopy(profile)}}},
            "colorRevision": 4, "inputEpoch": 2, "insertion": "upscaled_center", "planes": {eye: plane(eye) for eye in HMD.EYES}}


def make_pair(root, ordinal=1, kind="raw", apply_edit=True, value=100, frame=100):
    enabled = kind != "nr_off"
    mode = HMD.MODE[kind]
    record = {"frame": frame, "sourceWorldFrame": frame - 1, "colorRevision": 4, "inputEpoch": 2,
              "effectiveMode": mode, "applyModelEdit": apply_edit, "nrEnabled": enabled,
              "inferenceAttempted": enabled, "inferenceSucceeded": enabled,
              "outputCommitted": enabled and apply_edit, "disposition": "applied" if enabled else "disabled",
              "exposure": {"valid": False, "reason": "manual profile"}}
    evidence = {"schemaVersion": 1, "available": True, "reason": "", "transactionId": f"transaction-{frame}",
                "insertionPoint": 0,
                "configurationFingerprint": "test-fingerprint", "configuration": expected(kind, apply_edit)["configuration"],
                "left": copy.deepcopy(record), "right": copy.deepcopy(record)}
    artifacts = []
    for eye in HMD.EYES:
        path = root / f"frame-{ordinal:04}-{eye}.png"
        Image.fromarray(np.full((8, 8, 3), value, dtype=np.uint8)).save(path)
        artifacts.append(artifact(path, {"view": eye + "_eye", "format": "png", "colourContract": "sdr_srgb", "width": 8, "height": 8}))
    return {"ordinal": ordinal, "requestId": f"child-{root.name}-{ordinal}", "state": "completed", "requested": {"capture": descriptor()},
            "effective": descriptor(), "actual": {"source": {"kind": "hmd_submission", "fallback": "reject"},
                "acquisition": {"sourceKind": "hmd_submission", "engineFrame": frame, "compositorCycle": frame + 1000,
                    "monotonicTimestampUs": frame * 500000, "planes": [plane(eye) for eye in HMD.EYES], "nrEvidence": evidence,
                    "cameraEvidence": {**camera_plan(), "available": True, "sourceWorldFrame": frame - 1,
                        "provenance": "engine_cached_unjittered_world_matrices"}}},
            "artifacts": artifacts, "warnings": [], "errors": []}


class LightingEvidenceTests(unittest.TestCase):
    def test_neural_lighting_uses_effective_zero_without_rewriting_saved_value(self):
        target = expected("neural_lighting")
        target["configuration"]["color"]["settings"].update(appearanceMix=0.75, lightingPreservation=1.0)
        candidate = {"condition": "neural_lighting", "applyModelEdit": True,
                     "settings": {"settings": {"mode": "neural_lighting", "appearanceMix": 0.75,
                                                "lightingPreservation": 1.0}}}
        record = {"frame": 100, "sourceWorldFrame": 99, "colorRevision": 4, "inputEpoch": 2,
                  "effectiveMode": "neural_lighting", "applyModelEdit": True, "nrEnabled": True,
                  "inferenceAttempted": True, "inferenceSucceeded": True, "outputCommitted": True,
                  "disposition": "applied", "exposure": {},
                  "physicalRegions": [{"lightingPreservation": 0.0}]}
        evidence = {"schemaVersion": 1, "available": True, "transactionId": "fixture",
                    "configurationFingerprint": target["configurationFingerprint"],
                    "configuration": target["configuration"], "insertionPoint": 0,
                    "left": copy.deepcopy(record), "right": copy.deepcopy(record)}
        HMD.check_nr(evidence, target, candidate, {"engineFrame": 100})
        evidence["right"]["physicalRegions"][0]["lightingPreservation"] = 1.0
        with self.assertRaisesRegex(HMD.EvidenceError, "lighting preservation"):
            HMD.check_nr(evidence, target, candidate, {"engineFrame": 100})

    def test_new_setting_requires_exact_region_and_companion_values(self):
        target = expected("preserve_source")
        target["configuration"]["color"]["settings"]["lightingPreservation"] = 0.5
        candidate = {"condition": "preserve_source", "applyModelEdit": True,
                     "settings": {"settings": {"lightingPreservation": 0.5}}}
        record = {"frame": 100, "sourceWorldFrame": 99, "colorRevision": 4, "inputEpoch": 2,
                  "effectiveMode": "preserve_source", "applyModelEdit": True, "nrEnabled": True,
                  "inferenceAttempted": True, "inferenceSucceeded": True, "outputCommitted": True,
                  "disposition": "applied", "exposure": {},
                  "physicalRegions": [{"lightingPreservation": 0.5}, {"lightingPreservation": 0.5}]}
        evidence = {"schemaVersion": 1, "available": True, "transactionId": "fixture",
                    "configurationFingerprint": target["configurationFingerprint"], "configuration": target["configuration"],
                    "insertionPoint": 0, "left": copy.deepcopy(record), "right": copy.deepcopy(record)}
        HMD.check_nr(evidence, target, candidate, {"engineFrame": 100})
        for bad in (None, 0, 1, True, "0.5", float("nan")):
            broken = copy.deepcopy(evidence)
            broken["right"]["physicalRegions"][-1]["lightingPreservation"] = bad
            with self.assertRaisesRegex(HMD.EvidenceError, "lighting preservation"):
                HMD.check_nr(broken, target, candidate, {"engineFrame": 100})
        missing = copy.deepcopy(evidence)
        missing["right"]["physicalRegions"] = []
        with self.assertRaisesRegex(HMD.EvidenceError, "lighting preservation"):
            HMD.check_nr(missing, target, candidate, {"engineFrame": 100})
        companion = {"measurementBatches": [{"measurements": [{"source": {"lightingPreservation": 1}}]}]}
        with self.assertRaisesRegex(HMD.EvidenceError, "lighting preservation"):
            HMD.check_nr(evidence, target, candidate, {"engineFrame": 100}, capture_diagnostics=companion)
        absent = copy.deepcopy(evidence)
        old_target = copy.deepcopy(target)
        del old_target["configuration"]["color"]["settings"]["lightingPreservation"]
        absent["configuration"] = old_target["configuration"]
        with self.assertRaisesRegex(HMD.EvidenceError, "absent"):
            HMD.check_nr(absent, old_target, candidate, {"engineFrame": 100})


class PlanTests(unittest.TestCase):
    def test_distinct_randomized_counterbalanced_schedule(self):
        plan = HMD.make_plan(specification(), region_policy(), 918)
        other = HMD.make_plan(specification(), region_policy(), 919)
        self.assertNotEqual(plan["candidateMapping"], other["candidateMapping"])
        self.assertEqual([s["role"] for s in plan["schedule"][:3]], ["baseline"] * 3)
        self.assertEqual({v["condition"] for v in plan["candidateMapping"].values()},
                         {"nr_off", "raw", "managed_identity", "preserve_source", "display_only_source"})
        for value in plan["candidateMapping"].values():
            if value["condition"] == "display_only_source":
                shown = plan["candidateMapping"][value["shownCandidateId"]]
                self.assertEqual(value["settings"], shown["settings"])
                self.assertFalse(value["applyModelEdit"])
        for repetition in (1, 2, 3):
            blocks = {}
            for direction in ("forward", "reverse"):
                blocks[direction] = list(dict.fromkeys(row["block"] for row in plan["schedule"]
                    if row["repetition"] == repetition and row["direction"] == direction and row["role"] == "candidate"))
            self.assertEqual(blocks["forward"], blocks["reverse"][::-1])

    def test_conversions_need_producer_hypothesis(self):
        spec = specification()
        spec["candidates"].append({"condition": "conversion", "settings": {"settings": {"mode": "managed"}}})
        with self.assertRaisesRegex(HMD.EvidenceError, "producer"):
            HMD.make_plan(spec, region_policy(), 5)

    def test_neural_lighting_is_optional_and_unique(self):
        spec = specification()
        candidate = {"condition": "neural_lighting",
                     "settings": {"settings": {"mode": "neural_lighting"}}}
        spec["candidates"].append(candidate)
        plan = HMD.make_plan(spec, region_policy(), 5)
        self.assertEqual(sum(value["condition"] == "neural_lighting"
                             for value in plan["candidateMapping"].values()), 1)
        spec["candidates"].append(copy.deepcopy(candidate))
        with self.assertRaisesRegex(HMD.EvidenceError, "at most one Neural Lighting"):
            HMD.make_plan(spec, region_policy(), 5)

    def test_cadence_is_frozen_within_controller_bounds(self):
        spec = specification() | {"intervalMs": 50}
        plan = HMD.make_plan(spec, region_policy(), 7)
        self.assertEqual(plan["intervalMs"], 50)
        self.assertTrue(all(row["intervalMs"] == 50 for row in plan["schedule"]))
        for interval in (49, 60001, 50.5, True):
            with self.assertRaisesRegex(HMD.EvidenceError, "intervalMs"):
                HMD.make_plan(spec | {"intervalMs": interval}, region_policy(), 7)

    def test_capture_orders_must_agree_after_anonymous_order_is_resolved(self):
        forward = {"firstCandidateId": "CA", "secondCandidateId": "CB", "direction": "forward", "status": "reviewed",
                   "objectives": {name: {"imageVerdict": "first_better", "qualifiedVerdict": "first_better"} for name in HMD.OBJECTIVES}}
        reverse = {"firstCandidateId": "CB", "secondCandidateId": "CA", "direction": "reverse", "status": "reviewed",
                   "objectives": {name: {"imageVerdict": "second_better", "qualifiedVerdict": "second_better"} for name in HMD.OBJECTIVES}}
        HMD.reconcile_acquisition_orders([forward, reverse])
        self.assertTrue(forward["objectives"]["colourFidelity"]["acquisitionOrderAgreement"])
        reverse["objectives"]["colourFidelity"]["imageVerdict"] = "first_better"
        HMD.reconcile_acquisition_orders([forward, reverse])
        self.assertEqual(forward["objectives"]["colourFidelity"]["qualifiedVerdict"], "indeterminate")
        self.assertEqual(reverse["objectives"]["colourFidelity"]["qualifiedVerdict"], "indeterminate")

    def test_regions_account_for_absent_classes(self):
        policy = region_policy()
        policy["eyes"]["left"]["regions"].pop()
        with self.assertRaisesRegex(HMD.EvidenceError, "every region class"):
            HMD.check_regions(policy)

    def test_region_outside_native_extent_rejected(self):
        policy = region_policy()
        policy["eyes"]["left"]["regions"][0]["rect"] = [2, 2, 9, 9]
        with self.assertRaisesRegex(HMD.EvidenceError, "outside"):
            HMD.check_regions(policy)

    def test_resize_and_wrong_encoding_rejected(self):
        for patch in ({"width": 4}, {"format": "bmp"}, {"colourContract": "linear"}):
            capture = descriptor()
            if "width" in patch:
                capture["outputs"][0].update(patch)
            else:
                capture["outputs"][0]["encoding"].update(patch)
            with self.assertRaises(HMD.EvidenceError):
                HMD.check_capture_descriptor(capture)


@unittest.skipUnless(HAS_IMAGES, "install tools/nr-color/hmd_requirements.txt for image evidence fixtures")
class ImageEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.pair = make_pair(self.root)
        self.candidate = {"condition": "raw", "applyModelEdit": True}

    def tearDown(self):
        HMD.original_pixels.cache_clear()
        self.temporary.cleanup()

    def validate(self):
        return HMD.check_pair(self.pair, self.root, expected(self.candidate["condition"], self.candidate["applyModelEdit"]), self.candidate, region_policy())

    def test_raw_without_diagnostics_is_accepted(self):
        self.assertEqual(self.validate()["requestId"], self.pair["requestId"])

    def test_abbreviated_sequence_child_requires_exact_parent(self):
        parent = {"requestId": "parent-1", "requested": {"sequence": {"capture": descriptor()}},
                  "effective": {"capture": descriptor()}}
        self.pair["requested"] = {"action": "capture", "clientId": "sequence:parent-1",
                                  "commandId": "frame:1", "contractMajor": 1}
        with self.assertRaisesRegex(HMD.EvidenceError, "parent required"):
            self.validate()
        HMD.check_pair(self.pair, self.root, expected(), self.candidate, region_policy(), sequence=parent)
        for field, value in (("clientId", "sequence:foreign"), ("commandId", "frame:2"), ("contractMajor", 2)):
            original = self.pair["requested"][field]
            self.pair["requested"][field] = value
            with self.assertRaisesRegex(HMD.EvidenceError, "does not match"):
                HMD.check_pair(self.pair, self.root, expected(), self.candidate, region_policy(), sequence=parent)
            self.pair["requested"][field] = original
        parent["requested"]["sequence"]["capture"]["source"]["fallback"] = "allow"
        with self.assertRaisesRegex(HMD.EvidenceError, "reject fallback"):
            HMD.check_pair(self.pair, self.root, expected(), self.candidate, region_policy(), sequence=parent)

    def test_nr_off_is_distinct_from_hidden_inference(self):
        for kind, apply in (("nr_off", True), ("raw", False)):
            with self.subTest(kind=kind):
                self.pair = make_pair(self.root, kind=kind, apply_edit=apply)
                self.candidate = {"condition": kind, "applyModelEdit": apply}
                self.validate()

    def test_hash_mismatch_is_rejected(self):
        Path(self.pair["artifacts"][0]["path"]).write_bytes(b"changed")
        with self.assertRaisesRegex(HMD.EvidenceError, "length mismatch"):
            self.validate()

    def test_same_size_hash_mismatch_is_rejected(self):
        self.pair["artifacts"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(HMD.EvidenceError, "SHA-256"):
            self.validate()

    def test_actual_source_fallback_rejected(self):
        self.pair["actual"]["source"]["fallbackApplied"] = True
        with self.assertRaisesRegex(HMD.EvidenceError, "actual source"):
            self.validate()

    def test_cross_revision_eye_pair_rejected(self):
        self.pair["actual"]["acquisition"]["nrEvidence"]["right"]["colorRevision"] += 1
        with self.assertRaisesRegex(HMD.EvidenceError, "stale applied"):
            self.validate()

    def test_current_configuration_cannot_replace_applied(self):
        self.pair["actual"]["acquisition"]["nrEvidence"]["configuration"]["color"] = {"mode": "managed"}
        with self.assertRaisesRegex(HMD.EvidenceError, "applied configuration"):
            self.validate()

    def test_native_dimensions_and_encoding_checked_from_actual(self):
        for name, value in (("width", 4), ("colourContract", "linear")):
            with self.subTest(name=name):
                self.pair = make_pair(self.root)
                self.pair["artifacts"][0]["actual"][name] = value
                with self.assertRaises(HMD.EvidenceError):
                    self.validate()

    def test_output_fallback_after_successful_inference_rejected(self):
        self.pair["actual"]["acquisition"]["nrEvidence"]["right"]["outputCommitted"] = False
        with self.assertRaisesRegex(HMD.EvidenceError, "commit outcome"):
            self.validate()

    def test_captured_exposure_requires_exact_world_frame_and_age(self):
        pinned = expected() | {"requiresCapturedExposure": True, "exposureAge": 1}
        for eye in HMD.EYES:
            self.pair["actual"]["acquisition"]["nrEvidence"][eye]["exposure"] = {"valid": True, "sourceWorldFrame": 99, "frame": 98, "age": 1,
                                                                                  "epoch": 3, "sequence": 12}
        HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy())
        self.pair["actual"]["acquisition"]["nrEvidence"]["left"]["exposure"]["age"] = 0
        with self.assertRaisesRegex(HMD.EvidenceError, "age mismatch"):
            HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy())

    def test_delayed_exposure_joins_exact_companion_without_rewriting_capture(self):
        pinned = expected() | {"requiresCapturedExposure": True, "exposureAge": 1}
        stamp = {"frame": 98, "epoch": 3, "sequence": 12}
        for eye in HMD.EYES:
            self.pair["actual"]["acquisition"]["nrEvidence"][eye]["exposure"] = {
                **stamp, "valid": False, "sourceWorldFrame": 99, "age": 1}
        original = copy.deepcopy(self.pair)
        companion = {"exposures": [{"key": stamp, "available": True, "reason": "", "evidence": {
            **stamp, "readbackComplete": True, "engineRatioValid": True, "ambiguous": False, "rawValues": [2, 1, 0.5, 1]}}]}
        with self.assertRaisesRegex(HMD.EvidenceError, "companion missing"):
            HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy())
        HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy(), capture_diagnostics=companion)
        self.assertEqual(self.pair, original)
        companion["exposures"][0]["evidence"]["frame"] = 99
        with self.assertRaisesRegex(HMD.EvidenceError, "stamp mismatch"):
            HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy(), capture_diagnostics=companion)

    def test_exposure_companion_cannot_use_latest_or_ambiguous_sample(self):
        stamp = {"frame": 98, "epoch": 3, "sequence": 12}
        evidence = {**stamp, "readbackComplete": True, "engineRatioValid": True, "ambiguous": True}
        companion = {"exposures": [{"key": stamp, "available": True, "evidence": evidence}]}
        with self.assertRaisesRegex(HMD.EvidenceError, "ambiguous"):
            HMD.resolved_exposure(stamp, companion)
        companion["exposures"][0]["key"] = {**stamp, "sequence": 13}
        with self.assertRaisesRegex(HMD.EvidenceError, "missing"):
            HMD.resolved_exposure(stamp, companion)

    def test_capture_owned_companions_survive_without_live_lookup(self):
        pinned = expected() | {"requiresCapturedExposure": True, "exposureAge": 1}
        stamp = {"frame": 98, "epoch": 3, "sequence": 12}
        evidence = self.pair["actual"]["acquisition"]["nrEvidence"]
        for eye in HMD.EYES:
            evidence[eye]["exposure"] = {**stamp, "valid": False, "sourceWorldFrame": 99, "age": 1}
        companion = {"schemaVersion": 1, "finalized": True, "transactionId": evidence["transactionId"],
                     "measurementBatches": [], "measurementRequests": [],
                     "exposures": [{"key": stamp, "available": True, "evidence": {
                         **stamp, "readbackComplete": True, "engineRatioValid": True, "ambiguous": False}}]}
        self.pair["actual"]["captureDiagnostics"] = companion
        original = copy.deepcopy(self.pair)
        pair = HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy())
        self.assertEqual(pair["captureDiagnostics"], companion)
        self.assertEqual(self.pair, original)
        companion["transactionId"] = "unrelated"
        with self.assertRaisesRegex(HMD.EvidenceError, "transaction mismatch"):
            HMD.check_pair(self.pair, self.root, pinned, self.candidate, region_policy())

    def test_completed_companion_overrides_earlier_valid_exposure(self):
        stamp = {"frame": 98, "epoch": 3, "sequence": 12}
        frozen = {**stamp, "valid": True, "engineRatioValid": True, "ambiguous": False}
        completed = {**stamp, "readbackComplete": True, "engineRatioValid": False, "ambiguous": True}
        companion = {"exposures": [{"key": stamp, "available": True, "evidence": completed}]}
        with self.assertRaisesRegex(HMD.EvidenceError, "ambiguous"):
            HMD.resolved_exposure(frozen, companion)
        companion["exposures"][0].update(available=False, reason="resources_retired_before_readback")
        with self.assertRaisesRegex(HMD.EvidenceError, "unavailable"):
            HMD.resolved_exposure(frozen, companion)
        self.assertEqual(HMD.resolved_exposure(frozen), frozen)

    def test_recording_scene_rejects_transition_and_time_drift(self):
        plan = HMD.make_plan(specification(), region_policy(), 7)
        recording = {"meta": {**plan["fixedScene"]["recordingScene"], "gameHour": 12}, "activityEvents": []}
        self.assertTrue(HMD.recording_scene(recording, plan)["qualified"])
        recording["activityEvents"] = [{"kind": "cell"}]
        self.assertFalse(HMD.recording_scene(recording, plan)["qualified"])
        recording["activityEvents"] = []
        recording["meta"]["gameHour"] = 13
        self.assertFalse(HMD.recording_scene(recording, plan)["qualified"])

    def test_stale_frame_rejected_unless_explicitly_permitted(self):
        self.pair["actual"]["acquisition"]["engineFrame"] += 2
        with self.assertRaisesRegex(HMD.EvidenceError, "older"):
            self.validate()
        HMD.check_pair(self.pair, self.root, expected() | {"maximumRenderFrameAge": 2}, self.candidate, region_policy())

    def test_equal_epochs_cannot_admit_wrong_insertion(self):
        self.pair["actual"]["acquisition"]["nrEvidence"]["insertionPoint"] = 1
        with self.assertRaisesRegex(HMD.EvidenceError, "insertion differs"):
            self.validate()

    def test_signed_rgb_and_fixed_transfer_function(self):
        baseline = np.full((8, 8, 3), 100, dtype=np.uint8)
        sample = baseline.copy()
        sample[:, :, 0] += 10
        sample[:, :, 2] -= 7
        measured = HMD.compare_pixels(sample, baseline)
        self.assertEqual(measured["delta_mean_r"], 10)
        self.assertEqual(measured["delta_mean_g"], 0)
        self.assertEqual(measured["delta_mean_b"], -7)
        self.assertAlmostEqual(measured["delta_luma_mean"], 10 * .2126 - 7 * .0722)
        self.assertAlmostEqual(measured["absolute_rgb_error"], 17 / 3)
        white = HMD.region_metrics(np.full((8, 8, 3), 255, dtype=np.uint8))
        self.assertAlmostEqual(white["luminance_mean"], 1)
        self.assertEqual(white["near_white_fraction"], 1)

    def test_geometry_confound_is_detected_without_alignment(self):
        reference = np.zeros((8, 8, 3), dtype=np.uint8)
        reference[:, 4:, :] = 200
        sample = 200 - reference
        measured = HMD.compare_pixels(sample, reference)
        self.assertEqual(measured["gradient_sign_mismatch"], 1)
        self.assertGreater(measured["gradient_sign_samples"], 0)
        moved = np.zeros_like(reference)
        moved[:, 5:, :] = 200
        self.assertEqual(HMD.compare_pixels(moved, reference)["gradient_sign_mismatch"], 1)

    def test_exact_camera_matrices_reject_motion(self):
        acquisition = self.pair["actual"]["acquisition"]
        HMD.check_camera(acquisition, specification()["fixedScene"])
        acquisition["cameraEvidence"]["view"][0][12] = 0.1
        with self.assertRaisesRegex(HMD.EvidenceError, "drift"):
            HMD.check_camera(acquisition, specification()["fixedScene"])

    def test_end_to_end_import_preserves_originals_and_retains_failed_pair(self):
        plan = HMD.make_plan(specification(), region_policy(), 41)
        plan["createdUtc"] = "2020-01-01T00:00:00+00:00"
        plan_path = self.root / "plan.json"
        HMD.save_json(plan_path, plan)
        selected = plan["schedule"][:3]
        selected += [next(row for row in plan["schedule"] if row["role"] == "candidate")]
        index = {"planPath": str(plan_path), "planSha256": HMD.digest(plan_path), "sceneFingerprint": plan["sceneFingerprint"],
                 "fixedSettings": plan["fixedSettings"], "producer": {"buildId": "synthetic-only"},
                 "sessionId": "synthetic", "sequences": []}
        host_path = self.root / "synthetic-devbench.dll"
        host_path.write_bytes(b"synthetic fixture, not a DLL")
        host_receipt = self.root / "host-receipt.json"
        HMD.save_json(host_receipt, {"synthetic": True})
        index["devbench"] = {**artifact(host_path), "buildIdentity": {"version": "synthetic", "sourceCommit": "f" * 40},
                            "identityReceipt": artifact(host_receipt), "runtimeMatched": True,
                            "observedPid": 99, "observedVersion": "synthetic", "physicalModulePathVerified": False}
        for sequence in selected:
            number = sequence["scheduleOrdinal"]
            directory = self.root / f"sequence-{number}"
            directory.mkdir()
            candidate = plan["candidateMapping"][sequence["candidateId"]]
            kind = candidate.get("sourceCondition", candidate["condition"])
            children = [make_pair(directory, ordinal, kind, candidate["applyModelEdit"], 100 + sequence["repetition"], number * 100 + ordinal)
                        for ordinal in range(1, 13)]
            if sequence["role"] == "candidate":
                failed = make_pair(directory, 13, kind, candidate["applyModelEdit"], 103, number * 100 + 13)
                failed["actual"]["source"]["fallbackApplied"] = True
                children.append(failed)
            manifest = {"state": "final", "producer": index["producer"], "sessionId": index["sessionId"],
                        "contract": {"name": "csx.screenshot", "major": 1}, "requestId": f"sequence-{number}",
                        "acceptedUtc": HMD.utc(), "requested": {"sequence": {"capture": descriptor()}}, "effective": descriptor(),
                        "counts": {"inFlight": 0, "requested": len(children), "scheduled": len(children)}, "children": children}
            path = directory / "manifest.json"
            HMD.save_json(path, manifest)
            recording = directory / "recording.json"
            HMD.save_json(recording, {"meta": {**plan["fixedScene"]["recordingScene"], "gameHour": 12}, "activityEvents": []})
            index["sequences"].append({"scheduleOrdinal": number, "manifest": artifact(path), "expected": expected(kind, candidate["applyModelEdit"]),
                "recording": {"limitReached": False, "unrecordedTailMs": 0, "artifacts": [artifact(recording)]},
                "motionEvidence": {"available": True, "fixedSceneFingerprint": plan["sceneFingerprint"]}})
        index_path = self.root / "campaign.json"
        HMD.save_json(index_path, index)
        output = self.root / "assessment"
        report = HMD.analyse(index_path, output)
        self.assertEqual(report["sequencesAccepted"], 4)
        self.assertEqual(report["assessmentStatus"], "awaiting_blinded_image_review")
        self.assertFalse(report["runtimeQualificationComplete"])
        self.assertFalse(report["frameRateFlickerQualified"])
        self.assertIn("alias", report["temporalSampling"]["limitation"])
        self.assertFalse(report["hostProvenance"]["physicalModulePathVerified"])
        copies = list((output / "review" / "originals").glob("*.png"))
        self.assertEqual(len(copies), 4 * 12 * 2)
        raw_hashes = {HMD.digest(path) for path in self.root.glob("sequence-*/*.png")}
        self.assertTrue({HMD.digest(path) for path in copies} <= raw_hashes)
        review_text = "".join(path.read_text(encoding="utf-8") for path in (output / "review" / "requests").glob("*.json"))
        self.assertNotIn("candidateMapping", review_text)
        self.assertNotIn("synthetic-only", review_text)
        rows = HMD.read_json(output / "private" / "regions.json")
        self.assertEqual(len(rows), 4 * 12 * 2 * 5)
        temporal = HMD.read_json(output / "private" / "temporal.json")
        self.assertTrue(all(row["spacingUs"] == 500000 for row in temporal))
        self.assertFalse((output / "final-assessment.json").exists())
        exclusions = HMD.read_json(output / "private" / "exclusions.json")
        self.assertTrue(any(row.get("ordinal") == 13 and "actual source" in row["reason"] for row in exclusions))
        self.assertEqual(len(list((output / "private" / "captures").glob("**/P0013-*.png"))), 2)
        with self.assertRaisesRegex(HMD.EvidenceError, "already exists"):
            HMD.analyse(index_path, output)

    def test_structured_review_rejects_scalar_winner(self):
        schema = HMD.read_json(SOURCE.parent / "hmd-review.schema.json")
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.validate({"best": "raw"}, schema)

    def test_review_requests_are_swapped_replicates_without_private_mapping(self):
        plan = HMD.make_plan(specification(), region_policy(), 7)
        sequences = []
        for scheduled in plan["schedule"]:
            number = scheduled["scheduleOrdinal"]
            anonymous = f"Q{number:04}"
            pairs = []
            for ordinal in range(1, 13):
                pairs.append({"ordinal": ordinal, "acquisition": {"monotonicTimestampUs": ordinal * 500000},
                              "images": {eye: {"imageId": f"{anonymous}-P{ordinal:04}-{eye}",
                                  "anonymousPath": str(self.root / f"{anonymous}-P{ordinal:04}-{eye}.png")}
                                  for eye in HMD.EYES}})
            sequences.append({**scheduled, "anonymousId": anonymous, "pairs": pairs, "crops": []})
        output = self.root / "requests-only"
        requests = HMD.make_reviews(plan, sequences, output)
        self.assertTrue(all(row["ready"] for row in requests))
        for request_path in (output / "review" / "requests").glob("*.json"):
            request = HMD.read_json(request_path)
            self.assertEqual(len(request["passes"]), 2)
            self.assertTrue(all(len(p["batches"]) == 3 for p in request["passes"]))
            for replicate in range(3):
                payloads = [json.loads(p["batches"][replicate]["promptText"].split("Anonymous request:\n", 1)[1])
                            for p in request["passes"]]
                self.assertEqual(payloads[0]["first"], payloads[1]["second"])
                self.assertEqual(payloads[0]["second"], payloads[1]["first"])
                self.assertTrue(all(len(sequence["frames"]) == 12 for sequence in payloads[0]["first"]))
            serialized = request_path.read_text(encoding="utf-8")
            for candidate_id in plan["candidateMapping"]:
                self.assertNotIn(candidate_id, serialized)
            self.assertNotIn("candidateMapping", serialized)

    def review_fixture(self, verdict="first_better"):
        output = self.root / "review-fixture"
        image_path = Path(self.pair["artifacts"][0]["path"])
        mapping = [{"comparisonId": "V001", "firstCandidateId": "CA", "secondCandidateId": "CB", "ready": True}]
        HMD.save_json(output / "private" / "analysis.json", {"reviewRequests": mapping})
        HMD.save_json(output / "private" / "review-mapping.json", mapping)
        HMD.save_json(output / "private" / "plan.json", {"candidateMapping": {"CA": {"condition": "raw"}, "CB": {"condition": "managed_identity"}}})
        for name in ("sequence-repeatability", "temporal", "stereo"):
            HMD.save_json(output / "private" / (name + ".json"), [])
        HMD.save_json(output / "private" / "baseline-repeatability.json", {"envelopes": {}})
        HMD.preserve(SOURCE.parent / "hmd-review.schema.json", output / "review" / "hmd-review.schema.json")
        request = {"comparisonId": "V001", "passes": []}
        receipt = {"schema": "csx-codex-visual-review-execution-v1", "ok": True, "provider": "codex_cli", "batches": []}
        for presentation in (1, 2):
            batches = []
            for replicate in (1, 2, 3):
                response_path = output / "review" / "responses" / f"p{presentation}r{replicate}.json"
                events_path = response_path.with_suffix(".jsonl")
                batch = {"replicate": replicate, "presentationPass": presentation, "promptText": "synthetic fixture only",
                         "images": [str(image_path)], "responsePath": str(response_path), "eventsPath": str(events_path)}
                batches.append(batch)
                result_verdict = "second_better" if verdict == "first_better" and presentation == 2 else verdict
                response = {"comparisonId": "V001", "presentationPass": presentation, "replicate": replicate,
                    "inspectedImageIds": [image_path.stem], "missingImageIds": [], "exclusions": [], "uncertainties": [],
                    "untestedConditions": ["synthetic fixture; no real images evaluated"], "imageAssessment": "Synthetic fixture only.",
                    "objectives": {name: {"verdict": result_verdict, "confidence": "high", "baselineVariation": "synthetic",
                        "evidence": [{"eye": "both", "regionId": "skin", "imageIds": [image_path.stem], "finding": "synthetic"}]}
                        for name in HMD.OBJECTIVES}}
                HMD.save_json(response_path, response)
                events_path.write_text('{}\n', encoding="utf-8")
                receipt["batches"].append({**batch, "ok": True,
                    "promptSha256": hashlib.sha256(batch["promptText"].encode()).hexdigest(),
                    "imageBindings": [{"path": str(image_path), "sha256": HMD.digest(image_path)}],
                    "responseSha256": HMD.digest(response_path), "eventsSha256": HMD.digest(events_path)})
            request["passes"].append({"presentationPass": presentation, "batches": batches})
        HMD.save_json(output / "review" / "requests" / "V001.json", request)
        receipt_path = self.root / "synthetic-provider.json"
        HMD.save_json(receipt_path, receipt)
        return output, receipt_path

    def test_zero_rgb_difference_cannot_qualify_a_neural_winner(self):
        output, receipt = self.review_fixture()
        result = HMD.finalise(output, [receipt])
        self.assertFalse(result["productionDefaultSelected"])
        for objective in result["comparisons"][0]["objectives"].values():
            self.assertEqual(objective["imageVerdict"], "first_better")
            self.assertEqual(objective["qualifiedVerdict"], "indeterminate")
        seal = HMD.read_json(output / "sealed-reviews.json")
        self.assertFalse(seal["unblindingOccurred"])
        self.assertEqual(len(seal["assessments"]), 6)
        self.assertEqual(result["sealSha256"], HMD.digest(output / "sealed-reviews.json"))

    def test_changed_provider_images_fail_before_unblinding(self):
        output, receipt = self.review_fixture()
        Path(self.pair["artifacts"][0]["path"]).write_bytes(b"changed image")
        with self.assertRaisesRegex(HMD.EvidenceError, "input image changed"):
            HMD.finalise(output, [receipt])
        self.assertFalse((output / "sealed-reviews.json").exists())
        self.assertTrue(list((output / "private" / "provider-receipts").glob("*.json")))


if __name__ == "__main__":
    unittest.main()
