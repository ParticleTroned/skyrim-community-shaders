"""Offline ownership, sequencing, and interrupted-capture checks; no transport."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "nr-color"))
import hmd_capture as capture


def colour():
    profile = {"domain": "unknown", "transform": "identity", "exposureMultiplier": 1,
               "exposureSource": "manual"}
    return {"settings": {"schemaVersion": 1, "enabled": False, "mode": "legacy_raw", "detailStrength": 1,
                         "appearanceMix": 0, "maximumDetailStops": 1, "lightingPreservation": 0.5},
            "experiments": {"transportBypass": False, "diagnostics": False, "captureEngineExposure": True,
                            "captureFrameEvidence": False, "applyModelEdit": True,
                            "upscaled_center": copy.deepcopy(profile), "final_ldr_pre_ui": copy.deepcopy(profile)}}


def candidate(kind="raw", shown=True):
    return {"condition": kind if shown else "display_only_source", "sourceCondition": kind,
            "settings": {}, "applyModelEdit": shown}


def camera():
    return {"view": [[float(i) for i in range(16)]] * 2,
            "projection": [[float(i) / 16 for i in range(16)]] * 2,
            "positionAdjust": [[0.0, 0.0, 0.0, 0.0]] * 2,
            "viewTolerance": 0.001, "projectionTolerance": 0, "positionAdjustTolerance": 0}


class FakeController:
    def __init__(self):
        self.colour = colour()
        self.upscaling = {"neuralRenderingEnabled": False, "fovOnlyCenterScale": 0.5, "method": "DLAA"}
        self.revision = 7
        self.epoch = [11, 23]
        self.frame = 100
        self.events = []
        self.mutations = []
        self.capture_handler = None
        self.lost_ack = False

    def preflight(self):
        self.events.append("preflight")

    def wait_scene(self):
        self.events.append("scene")

    def ownership_guard(self, prepared):
        self.events.append("ownership")

    def status(self):
        return {**copy.deepcopy(self.colour), "revision": self.revision, "inputEpoch": self.epoch.copy(),
                "captureEvidenceSchemaVersion": 1, "ok": True}

    def nr(self):
        configuration = {"upscaling": copy.deepcopy(self.upscaling), "color": copy.deepcopy(self.colour)}
        return {"requestedConfiguration": configuration,
                "requestedConfigurationFingerprint": capture.hmd.value_digest(configuration),
                "captureEvidence": {"schemaVersion": 1, "routes": [{"left": {"frame": self.frame},
                                                                      "right": {"frame": self.frame}}]}}

    def call(self, args, label, tool=None):
        self.frame += 1
        action = args["action"]
        self.events.append(action)
        if action == "status":
            return self.status()
        if action == "nr_status":
            return {"ok": True, "neuralRendering": self.nr()}
        if action == "configure":
            self.mutations.append(copy.deepcopy(args))
            if args["expectedRevision"] != self.revision:
                raise capture.assess.AssessmentError("revision mismatch")
            self.colour = {"settings": copy.deepcopy(args["settings"]), "experiments": copy.deepcopy(args["experiments"])}
            self.revision += 1
            if self.lost_ack:
                raise TimeoutError("lost accepted response")
            return self.status()
        if action == "nr_configure":
            self.mutations.append(copy.deepcopy(args))
            if args["expectedConfigurationFingerprint"] != self.nr()["requestedConfigurationFingerprint"]:
                raise capture.assess.AssessmentError("fingerprint mismatch")
            self.upscaling["neuralRenderingEnabled"] = args["enabled"]
            return {"ok": True, "neuralRendering": self.nr()}
        raise AssertionError(action)

    def capture(self, action, session, scheduled):
        self.events.append("capture:" + action)
        return self.capture_handler(action, session, scheduled)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.controller = FakeController()
        self.fixed = {"cell": "TestCell", "cellFormID": 42, "interior": True, "weatherFormID": 1}
        self.plan = {"sceneFingerprint": "fixed-scene", "fixedSettings": capture.fixed_upscaling(self.controller.nr()["requestedConfiguration"]),
                     "fixedScene": {"cameraEvidence": camera(), "recordingScene": self.fixed, "gameHour": 12,
                                    "gameHourTolerance": 0.01}, "regionPolicy": {}, "schedule": [], "candidateMapping": {}}
        self.plan_path = self.directory / "plan.json"
        self.plan_path.write_text(json.dumps(self.plan), encoding="utf-8")
        self.prepared = {"planPath": str(self.plan_path), "planSha256": capture.hmd.digest(self.plan_path),
                         "producer": {"buildId": "build"}, "devbench": {"sha256": "host"}, "screenshotSessionId": "screenshot-session",
                         "insertion": "upscaled_center", "configuration": self.controller.nr()["requestedConfiguration"],
                         "recordingDirectory": str(self.directory / "host-recordings")}
        Path(self.prepared["recordingDirectory"]).mkdir()
        self.runner = capture.Campaign(self.controller, self.plan, self.prepared, self.directory, warmup=0)
        self.runner.original = copy.deepcopy(self.prepared["configuration"])
        self.runner.owned_colour = colour()
        self.runner.owned_upscaling = copy.deepcopy(self.controller.upscaling)
        self.runner.owned_revision = self.controller.revision

    def tearDown(self):
        self.temp.cleanup()

    def test_modes_and_display_only_are_distinct(self):
        for kind, enabled, mode in (("nr_off", False, "legacy_raw"), ("raw", False, "legacy_raw"),
                                    ("managed_identity", True, "managed"), ("preserve_source", True, "preserve_source"),
                                    ("neural_lighting", True, "neural_lighting")):
            result = capture.candidate_colour(colour(), candidate(kind))
            self.assertEqual((result["settings"]["enabled"], result["settings"]["mode"]), (enabled, mode))
            self.assertTrue(result["experiments"]["captureFrameEvidence"])
            self.assertFalse(result["experiments"]["diagnostics"])
            self.assertEqual(result["settings"]["lightingPreservation"], 0.5)
        hidden = capture.candidate_colour(colour(), candidate("raw", False))
        self.assertFalse(hidden["experiments"]["applyModelEdit"])
        self.assertFalse(hidden["experiments"]["transportBypass"])

    def test_managed_identity_cannot_inherit_conversion(self):
        initial = colour()
        initial["experiments"]["upscaled_center"].update(domain="linear", transform="linear_to_srgb")
        result = capture.candidate_colour(initial, candidate("managed_identity"))
        self.assertEqual(result["experiments"]["upscaled_center"]["transform"], "identity")
        self.assertEqual(result["experiments"]["upscaled_center"]["exposureMultiplier"], 1)

    def test_lighting_candidate_and_restoration_keep_selected_values(self):
        trial = candidate("preserve_source")
        trial["settings"] = {"settings": {"lightingPreservation": 0}}
        self.runner.apply(trial)
        self.assertEqual(self.controller.colour["settings"]["lightingPreservation"], 0)
        self.runner.restore()
        self.assertTrue(self.runner.index["restoration"]["performed"])
        self.assertEqual(self.controller.colour["settings"]["lightingPreservation"], 0.5)

    def test_unrelated_settings_and_raw_diagnostics_rejected(self):
        changed = candidate()
        changed["settings"] = {"unrelatedGraphics": True}
        with self.assertRaises(capture.hmd.EvidenceError):
            capture.candidate_colour(colour(), changed)
        changed["settings"] = {"experiments": {"diagnostics": True}}
        with self.assertRaises(capture.hmd.EvidenceError):
            capture.candidate_colour(colour(), changed)

    def test_master_uses_fingerprint_and_hidden_preserves_inference(self):
        self.runner.apply(candidate())
        self.assertEqual(len(self.controller.mutations), 2)
        self.assertIn("expectedConfigurationFingerprint", self.controller.mutations[1])
        epoch = self.controller.epoch.copy()
        self.controller.mutations.clear()
        self.runner.apply(candidate("raw", False))
        self.assertTrue(self.controller.upscaling["neuralRenderingEnabled"])
        self.assertEqual([item["action"] for item in self.controller.mutations], ["configure"])
        self.assertEqual(self.controller.epoch, epoch)

    def test_foreign_configuration_is_never_overwritten(self):
        self.controller.revision += 1
        with self.assertRaises(capture.assess.MutationUncertain):
            self.runner.apply(candidate())
        self.assertEqual(self.controller.mutations, [])

    def test_plan_changes_are_detected_before_any_settings_write(self):
        self.plan_path.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(capture.assess.MutationUncertain, "Frozen plan changed"):
            self.runner.apply(candidate())
        self.assertEqual(self.controller.mutations, [])

    def test_frozen_schedule_is_independent_of_json_object_order_and_rejects_tampering(self):
        eye = {"nativeSize": [8, 8], "regions": [{"id": "wall", "class": "material", "description": "test wall", "rect": [0, 0, 3, 3]}],
               "unavailable": {name: "not visible" for name in capture.hmd.CLASSES - {"material"}}}
        policy = {"schemaVersion": 1, "sceneFingerprint": "fixed-scene", "motionGradientMismatchLimit": 0.2,
                  "eyes": {"left": eye, "right": copy.deepcopy(eye)}}
        spec = {**self.plan, "candidates": [{"condition": kind, "settings": {"settings": {"mode": capture.MODE[kind]}}}
                                             for kind in ("nr_off", "raw", "managed_identity", "preserve_source")]}
        spec["intervalMs"] = 50
        planned = capture.hmd.make_plan(spec, policy, 19417)
        sorted_plan = json.loads(json.dumps(planned, sort_keys=True))
        capture.validate_plan(sorted_plan)
        self.assertTrue(all(item["intervalMs"] == 50 for item in sorted_plan["schedule"]))
        sorted_plan["schedule"][3]["candidateId"] = sorted_plan["candidateOrder"][1]
        with self.assertRaisesRegex(capture.hmd.EvidenceError, "counterbalanced schedule"):
            capture.validate_plan(sorted_plan)

    def test_lost_mutation_ack_blocks_restoration(self):
        self.controller.lost_ack = True
        with self.assertRaises(TimeoutError):
            self.runner.apply(candidate())
        self.runner.restore()
        self.assertEqual(len(self.controller.mutations), 1)
        self.assertFalse(self.runner.index["restoration"]["performed"])

    def test_fresh_state_uses_active_insertion_epoch(self):
        self.runner.apply(candidate())
        self.prepared["insertion"] = "final_ldr_pre_ui"
        with patch.object(capture.hmd, "check_nr"):
            result = self.runner.await_applied(candidate())
        self.assertEqual(result["inputEpoch"], 23)
        self.assertEqual(result["insertion"], "final_ldr_pre_ui")

    def test_warmup_starts_after_configuration_despite_long_capture_gap(self):
        self.runner.apply(candidate())
        self.runner.last_frame = 1
        self.controller.frame = 1000
        self.runner.warmup = 4
        self.runner.sleep = lambda _: None
        with patch.object(capture.hmd, "check_nr") as validated:
            self.runner.await_applied(candidate())
        self.assertGreaterEqual(validated.call_count, 4)

    def test_stale_applied_evidence_times_out_without_new_mutation(self):
        times = iter((0, 0, 1, 2, 3))
        self.runner.clock = lambda: next(times)
        self.runner.sleep = lambda _: None
        self.runner.timeout = 2
        with patch.object(capture.hmd, "check_nr", side_effect=capture.hmd.EvidenceError("stale revision")):
            with self.assertRaisesRegex(capture.assess.AssessmentError, "stale revision"):
                self.runner.await_applied(candidate())
        self.assertEqual(self.controller.mutations, [])

    def recording_stop(self):
        meta = {**self.fixed, "correlationId": "capture-owner", "gameHour": 12}
        (Path(self.prepared["recordingDirectory"]) / "recording.json").write_text(
            json.dumps({"meta": meta, "activityEvents": []}), encoding="utf-8")
        return {"data": {"recording": {"stopReceipt": {"path": "Data/recording.json", "meta": meta,
                                                          "limitReached": False, "unrecordedTailMs": 0}}}}

    def test_interrupted_owned_capture_is_finalized_without_config_restore(self):
        def handler(action, *_):
            if action == "start":
                return {"data": {"sessionId": "capture-owner"}}
            if action == "observe":
                raise KeyboardInterrupt()
            return self.recording_stop()
        self.controller.capture_handler = handler
        scheduled = {"scheduleOrdinal": 1, "minimumPairs": 12, "intervalMs": 500}
        with self.assertRaises(KeyboardInterrupt):
            self.runner.collect(scheduled, candidate(), {})
        self.assertIn("capture:stop", self.controller.events)
        self.assertIsNone(self.runner.active_capture)
        self.assertEqual(self.controller.mutations, [])
        self.assertTrue((self.directory / "recordings" / "001.json").is_file())

    def test_unknown_capture_start_is_not_retried_or_stopped(self):
        def handler(*_):
            raise TimeoutError("start receipt lost")
        self.controller.capture_handler = handler
        with self.assertRaises(capture.assess.MutationUncertain):
            self.runner.collect({"scheduleOrdinal": 1, "minimumPairs": 12, "intervalMs": 500}, candidate(), {})
        self.assertEqual([event for event in self.controller.events if event.startswith("capture:")], ["capture:start"])
        self.assertTrue(self.runner.uncertain)

    def test_original_manifest_and_pairs_are_verified_before_recording_stop(self):
        manifest_path = self.directory / "final-manifest.json"
        camera_sample = {**camera(), "available": True, "sourceWorldFrame": 150,
                         "provenance": "engine_cached_unjittered_world_matrices"}
        children = [{"ordinal": ordinal, "actual": {"acquisition": {
            "engineFrame": 150, "cameraEvidence": camera_sample,
            "nrEvidence": {eye: {"sourceWorldFrame": 150} for eye in capture.hmd.EYES},
            "planes": [{"eye": eye} for eye in capture.hmd.EYES]}}} for ordinal in range(1, 13)]
        manifest = {"state": "final", "producer": self.prepared["producer"], "sessionId": "screenshot-session",
                    "counts": {"inFlight": 0}, "requested": {}, "effective": {}, "children": children}
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        artifact = {"path": str(manifest_path), "bytes": manifest_path.stat().st_size,
                    "sha256": capture.hmd.digest(manifest_path), "committed": True}
        observations = 0

        def handler(action, *_):
            nonlocal observations
            if action == "start":
                return {"data": {"sessionId": "capture-owner"}}
            if action == "observe":
                observations += 1
                receipt = {"state": "running"} if observations == 1 else {
                    "state": "completed", "manifest": {"finalPath": str(manifest_path)}, "artifacts": [artifact]}
                return {"data": {"observation": {"screenshot": {"receipt": receipt}}}}
            self.assertEqual(self.controller.events.count("validated-original-pair"), 12)
            return self.recording_stop()

        def validate(child, *_args, **_kwargs):
            self.controller.events.append("validated-original-pair")
            return {"ordinal": child["ordinal"], "acquisition": child["actual"]["acquisition"]}

        self.controller.capture_handler = handler
        self.runner.sleep = lambda _: None
        with patch.object(capture.hmd, "check_capture_descriptor"), patch.object(capture.hmd, "check_pair", side_effect=validate):
            result = self.runner.collect({"scheduleOrdinal": 1, "minimumPairs": 12, "intervalMs": 500}, candidate(), {})
        self.assertEqual(observations, 2)
        self.assertEqual(result["validatedPairs"], 12)
        self.assertTrue(result["recordedScene"]["qualified"])
        self.assertFalse(result["recoveryRequired"])
        self.assertTrue(Path(result["captureDiagnosticsArtifact"]["path"]).is_file())

    def test_pending_exposure_receipts_are_retained_without_rewriting_stamps(self):
        stamp = {"frame": 150, "epoch": 2, "sequence": 8}
        manifest = {"children": [{"ordinal": 1, "actual": {"acquisition": {"nrEvidence": {
            "engineExposure": stamp, "left": {"exposure": stamp}, "right": {"exposure": stamp}}}}}]}
        replies = [{"captureEvidenceSchemaVersion": 1, "exposures": [{"key": stamp, "available": False, "reason": "pending"}]},
                   {"captureEvidenceSchemaVersion": 1, "exposures": [{"key": stamp, "available": True, "evidence": {"engineRatioValid": True}}]}]
        self.runner.sleep = lambda _: None
        with patch.object(self.controller, "call", side_effect=replies) as mocked:
            result = self.runner.exposure_diagnostics(manifest)
        self.assertEqual(len(result["responses"]), 2)
        self.assertEqual(len(result["requests"][0]["stamps"]), 1)
        self.assertEqual(mocked.call_args_list[0].args[0]["stamps"], [stamp])
        self.assertTrue(result["exposures"][0]["available"])
        self.assertEqual(manifest["children"][0]["actual"]["acquisition"]["nrEvidence"]["engineExposure"], stamp)

    def test_capture_owned_diagnostics_need_no_live_polling(self):
        stamp = {"frame": 150, "epoch": 2, "sequence": 8}
        owned = {"schemaVersion": 1, "finalized": True, "transactionId": "tx",
                 "measurementBatches": [], "measurementRequests": [],
                 "exposures": [{"key": stamp, "available": False,
                                "reason": "resources_retired_before_readback"}]}
        manifest = {"children": [{"ordinal": 1, "actual": {
            "acquisition": {"nrEvidence": {"transactionId": "tx", "engineExposure": stamp}},
            "captureDiagnostics": owned}}]}
        with patch.object(self.controller, "call", side_effect=AssertionError("unexpected live call")):
            result = self.runner.exposure_diagnostics(manifest)
        self.assertEqual(result["requests"], [])
        self.assertEqual(result["exposures"], [])
        self.assertEqual(result["captureOwned"][0]["diagnostics"], owned)

    def test_legacy_lookup_remains_separate_from_owned_capture_with_same_stamp(self):
        stamp = {"frame": 150, "epoch": 2, "sequence": 8}
        owned = {"schemaVersion": 1, "finalized": True, "transactionId": "tx",
                 "measurementBatches": [], "measurementRequests": [],
                 "exposures": [{"key": stamp, "available": False, "reason": "readback_pending_at_capture_finalization"}]}
        acquisition = {"nrEvidence": {"transactionId": "tx", "engineExposure": stamp}}
        manifest = {"children": [
            {"ordinal": 1, "actual": {"acquisition": acquisition, "captureDiagnostics": owned}},
            {"ordinal": 2, "actual": {"acquisition": acquisition}}]}
        completed = {"key": stamp, "available": True, "evidence": {
            **stamp, "readbackComplete": True, "engineRatioValid": True, "ambiguous": False}}
        reply = {"captureEvidenceSchemaVersion": 1, "exposures": [completed]}
        with patch.object(self.controller, "call", return_value=reply) as mocked:
            result = self.runner.exposure_diagnostics(manifest)
        self.assertEqual(mocked.call_count, 1)
        self.assertEqual(capture.hmd.resolved_exposure(stamp, result), completed["evidence"])
        self.assertEqual(result["captureOwned"][0]["diagnostics"], owned)

    def test_host_version_mismatch_rejects_even_when_physical_hash_matches(self):
        host = self.directory / "devbench.dll"
        host.write_bytes(b"offline host artifact")
        expected = {"path": str(host), "bytes": host.stat().st_size, "sha256": capture.hmd.digest(host),
                    "buildIdentity": {"version": "expected-version", "sourceCommit": "a" * 40}}
        self.controller.expected_identity = {"listenerPid": 123}
        with patch.object(self.controller, "call", return_value={"plugin": "devbench", "version": "other", "pid": 123, "vr": True}):
            with self.assertRaisesRegex(capture.hmd.EvidenceError, "runtime version/process"):
                capture.verify_devbench(self.controller, expected, self.directory)

    def test_host_snapshot_retains_original_bytes_after_deployed_file_changes(self):
        host = self.directory / "deployed-devbench.dll"
        host.write_bytes(b"original host")
        expected = {"path": str(host), "bytes": host.stat().st_size, "sha256": capture.hmd.digest(host),
                    "buildIdentity": {"version": "expected-version", "sourceCommit": "a" * 40}}
        self.controller.expected_identity = {"listenerPid": 123}
        with patch.object(self.controller, "call", return_value={"plugin": "devbench", "version": "expected-version", "pid": 123, "vr": True}):
            result = capture.verify_devbench(self.controller, expected, self.directory)
        host.write_bytes(b"later deployment")
        self.assertEqual(Path(result["path"]).read_bytes(), b"original host")
        self.assertTrue(result["runtimeMatched"])
        self.assertFalse(result["physicalModulePathVerified"])

    def test_failed_unchanged_baselines_prevent_candidate_dispatch(self):
        self.plan["candidateMapping"] = {"reference": candidate("nr_off"), "neural": candidate("raw")}
        self.plan["schedule"] = [{"scheduleOrdinal": ordinal, "candidateId": "reference" if ordinal <= 3 else "neural"}
                                 for ordinal in range(1, 6)]
        self.plan_path.write_text(json.dumps(self.plan), encoding="utf-8")
        self.prepared["planSha256"] = capture.hmd.digest(self.plan_path)
        captured = []

        def collect(scheduled, *_):
            captured.append(scheduled["scheduleOrdinal"])
            self.runner.index["sequences"].append({"scheduleOrdinal": scheduled["scheduleOrdinal"]})

        with patch.object(capture, "verify_devbench", return_value=self.prepared["devbench"]), \
                patch.object(capture.hmd, "check_nr"), patch.object(self.runner, "collect", side_effect=collect), \
                patch.object(capture, "baseline_repeatability", return_value={"qualified": False, "excluded": ["motion"]}):
            result = self.runner.run()
        self.assertEqual(captured, [1, 2, 3])
        self.assertFalse(result["complete"])
        self.assertEqual([item["scheduleOrdinal"] for item in result["sequences"] if "notRunReason" in item], [4, 5])
        self.assertFalse(any(item["action"] == "nr_configure" for item in self.controller.mutations))
        self.assertTrue(result["restoration"]["performed"])

    def test_changed_colour_during_capture_still_stops_its_owned_recording(self):
        def handler(action, *_):
            if action == "start":
                self.controller.revision += 1
                return {"data": {"sessionId": "capture-owner"}}
            self.assertEqual(action, "stop")
            return self.recording_stop()
        self.controller.capture_handler = handler
        with self.assertRaises(capture.assess.MutationUncertain):
            self.runner.collect({"scheduleOrdinal": 1, "minimumPairs": 12, "intervalMs": 500}, candidate(), {})
        self.assertIsNone(self.runner.active_capture)
        self.assertTrue(self.runner.uncertain)
        self.assertEqual(self.controller.mutations, [])

    def test_post_stop_evidence_failure_does_not_invalidate_settings_ownership(self):
        self.runner.apply(candidate())

        def handler(action, *_):
            if action == "start":
                return {"data": {"sessionId": "capture-owner"}}
            if action == "observe":
                raise KeyboardInterrupt()
            return {"data": {"recording": {"stopReceipt": {"path": "missing-recording.json",
                    "meta": {"correlationId": "capture-owner"}, "limitReached": False, "unrecordedTailMs": 0}}}}

        self.controller.capture_handler = handler
        with self.assertRaises(KeyboardInterrupt):
            self.runner.collect({"scheduleOrdinal": 1, "minimumPairs": 12, "intervalMs": 500}, candidate(), {})
        self.assertIsNone(self.runner.active_capture)
        self.assertFalse(self.runner.uncertain)
        self.assertIn("evidenceError", self.runner.index["sequences"][0])
        self.runner.restore()
        self.assertTrue(self.runner.index["restoration"]["performed"])

    def test_camera_frame_drift_and_unavailable_evidence_fail_closed(self):
        observed = {**camera(), "available": True, "sourceWorldFrame": 10,
                    "provenance": "engine_cached_unjittered_world_matrices"}
        pair = {"ordinal": 1, "acquisition": {"cameraEvidence": observed,
                "nrEvidence": {eye: {"sourceWorldFrame": 10} for eye in capture.hmd.EYES}}}
        self.assertTrue(capture.acquisition_motion([pair], self.plan)["available"])
        observed["sourceWorldFrame"] = 9
        self.assertFalse(capture.acquisition_motion([pair], self.plan)["available"])
        observed["sourceWorldFrame"] = 10
        observed["view"] = copy.deepcopy(observed["view"])
        observed["view"][0][0] += 1
        self.assertFalse(capture.acquisition_motion([pair], self.plan)["available"])
        observed["available"] = False
        self.assertFalse(capture.acquisition_motion([pair], self.plan)["available"])

    def test_lifecycle_and_time_drift_cannot_qualify_fixed_scene(self):
        raw = {"meta": {**self.fixed, "gameHour": 12}, "activityEvents": []}
        self.assertTrue(capture.recording_scene(raw, self.plan)["qualified"])
        raw["activityEvents"] = [{"kind": "cell"}]
        self.assertFalse(capture.recording_scene(raw, self.plan)["qualified"])
        raw["activityEvents"] = []
        raw["meta"]["gameHour"] = 13
        result = capture.recording_scene(raw, self.plan)
        self.assertFalse(result["qualified"])
        self.assertEqual(result["gameHourDelta"], 1)


if __name__ == "__main__":
    unittest.main()
