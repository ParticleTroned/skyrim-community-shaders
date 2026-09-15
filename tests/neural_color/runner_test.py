"""Executable dry runner/controller regressions. No PowerShell, network or game.

The process double returns the controller's documented envelopes. It exercises
real Controller command construction, parsing, campaign sequencing and cleanup.
It does not qualify actual PowerShell transport or real renderer behaviour.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nr-color"))
import assess


def configuration():
    profile = dict(domain="unknown", transform="identity", exposureMultiplier=1.0, exposureSource="manual")
    return {"settings": dict(schemaVersion=1, enabled=True, mode="legacy_raw", detailStrength=1.0,
                             appearanceMix=0.0, maximumDetailStops=1.0),
            "experiments": dict(upscaled_center=copy.deepcopy(profile), final_ldr_pre_ui=copy.deepcopy(profile),
                                transportBypass=False, diagnostics=False, captureEngineExposure=False,
                                applyModelEdit=True)}


def runtime_identity():
    return {"verified": True, "complete": True, "listenerPid": 42,
            "process": {"path": "C:/fixture/SkyrimVR.exe", "startTimeUtc": "2026-09-14T22:27:00Z"},
            "build": {"buildId": "fixture-build"},
            "artifact": {"path": "C:/fixture/CommunityShaders.dll", "sha256": "a" * 64}, "missing": []}


def sample(slot=0, frame=100, revision=7, transport=True, shown=True, captured=False):
    values = [0.5, 0.4, 0.3, 64, 0.5, 0.4, 0.3, 0, 0, 0, 0, 0, 0, 0, 0, 64,
              0, 0, 1, 1, 1, 1, 1, 1]
    return {"source": dict(frame=frame, sourceWorldFrame=frame, generation=4, physicalSlot=slot,
                           insertionPoint=0, revision=revision, processed=True, failure="",
                           effectiveMode="managed", rect=[0, 0, 8, 8], sourceFormat=26, outputFormat=26,
                           profile=dict(domain="linear" if captured else "unknown", transform="reversible_proxy" if captured else "identity", exposureMultiplier=1.0, exposureSource="captured_hdr" if captured else "manual"),
                           modelEditShown=shown, transportBypass=transport, exposureBinding="gpu_snapshot_queued",
                           exposure=dict(frame=frame, epoch=2, sequence=10, ambiguous=False)), "values": values}


class Clock:
    def __init__(self): self.now = 0.0
    def time(self): return self.now
    def sleep(self, value): self.now += value


class Fixture:
    """Identity-checked process double; only the existing controller path is used."""
    def __init__(self, root, clock):
        self.clock, self.calls = clock, []
        self.state = configuration()
        self.original = copy.deepcopy(self.state)
        self.revision, self.frame = 1, 100
        self.unavailable_identity = False
        self.fail_label = None
        self.timeout_label = None
        self.change_on_sample = False
        self.hidden_ignored = False
        self.fail_stop = False
        self.multi = False
        self.identity = runtime_identity()
        self.readiness_override = {}
        self.scene_cell = "FixtureCell"
        self.tools = [assess.TOOL, "communityshaders.profiler", "communityshaders.upscaling_api", "communityshaders.screenshot", "record", "input"]
        self.steps = 0
        self.root = root
        for path in ("tools/devbench-control/Invoke-DevBenchControl.ps1", "tools/capture-interaction-control/Invoke-CaptureInteraction.ps1"):
            f = root / path
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text("# process-double path only; never executed\n")
        self.args = argparse.Namespace(automation_root=root, runtime=root / "runtime.json", pwsh="pwsh",
            expected_cell="FixtureCell", timeout=2.0, warmup_frames=0, sample_frames=1,
            insertion="upscaled_center", include_captured=True, capture_episodes=False,
            artifact_path=None, workspace_manifest=None, expected_build_id=None, expected_artifact_sha256=None,
            expected_physical_slots=None)
        self.evidence = root / "evidence"
        self.evidence.mkdir()

    @staticmethod
    def option(command, key, default=None):
        return command[command.index(key) + 1] if key in command else default

    def status(self, label):
        self.frame += 10
        if self.change_on_sample and label == "sample":
            self.change_on_sample = False
            self.revision += 1
            self.state["settings"]["appearanceMix"] = 0.42
        s = copy.deepcopy(self.state)
        s.update(ok=True, apiVersion=2, revision=self.revision)
        p = s["experiments"][self.args.insertion]
        slots = [0, 1, 4, 5] if self.multi else [0, 1]
        groups = [sample(slot, self.frame, self.revision, s["experiments"]["transportBypass"],
                         s["experiments"]["applyModelEdit"] or self.hidden_ignored,
                         p["exposureSource"] == "captured_hdr") for slot in slots]
        for item in groups:
            item["source"]["insertionPoint"] = assess.PROFILE_NAMES.index(self.args.insertion)
            item["source"]["profile"] = copy.deepcopy(p)
            item["source"]["effectiveMode"] = self.state["settings"]["mode"]
        s["slots"] = [copy.deepcopy(item["source"]) for item in groups]
        s["measurements"] = [] if self.unavailable_identity and p["transform"] == "identity" else groups
        return s

    def run(self, command, **kwargs):
        self.calls.append((command[:], kwargs.copy()))
        assert kwargs.get("check") is False and "shell" not in kwargs
        self.clock.now += min(0.05, kwargs["timeout"] / 2)
        label = self.option(command, "-EvidenceLabel", "")
        action = command[command.index("-File") + 2]
        is_capture = "CaptureInteraction" in command[command.index("-File") + 1]
        if is_capture:
            if action == "start" and self.timeout_label == "capture-start":
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])
            value = {"ok": not ((action == "observe" and self.fail_label == "capture-observe") or
                                 (action == "stop" and self.fail_stop))}
        else:
            value = {"ok": True, "transportOk": True, "semantic": {"known": True, "ok": True},
                     "runtimeIdentity": copy.deepcopy(self.identity)}
            pinned = self.option(command, "-ExpectedRuntimeIdentityJson")
            if pinned is not None:
                expected = json.loads(pinned)
                assert expected == {"listenerPid": 42, "processPath": "C:/fixture/SkyrimVR.exe",
                                    "processStartTimeUtc": "2026-09-14T22:27:00Z", "buildId": "fixture-build",
                                    "artifactPath": "C:/fixture/CommunityShaders.dll", "artifactSha256": "a" * 64}
            if action == "list":
                value["data"] = {"tools": [{"name": name} for name in self.tools]}
            elif action == "wait":
                value["data"] = {"observation": {"satisfied": True}}
            else:
                req = json.loads(self.option(command, "-ArgumentsJson"))
                tool = self.option(command, "-Tool")
                if tool == "inspect":
                    assert req == {"kind": "scene"}
                    value["data"] = {"content": [{"playerLoaded": True, "cell": {"editorId": self.scene_cell}}]}
                    return subprocess.CompletedProcess(command, 0, json.dumps(value), "")
                if req["action"] == "nr_readiness":
                    self.frame += 10
                    payload = dict(ok=True, readinessVersion=1, ready=True, reasons=[], targetGeneration=7,
                                   status=dict(frame=self.frame, controller=dict(revision=1, targetEpoch=2)))
                    payload.update(self.readiness_override)
                    value["data"] = {"content": [payload]}
                    return subprocess.CompletedProcess(command, 0, json.dumps(value), "")
                if req["action"] == "configure":
                    if req["expectedRevision"] != self.revision:
                        raise AssertionError("runner sent stale CAS")
                    desired = {k: req[k] for k in ("settings", "experiments")}
                    if desired != self.state:
                        self.revision += 1
                    self.state = copy.deepcopy(desired)
                if label == self.timeout_label:
                    raise subprocess.TimeoutExpired(command, kwargs["timeout"])
                payload = {"ok": True, "allPresent": True} if req["action"] == "assets" else self.status(label)
                if label == self.fail_label:
                    payload = {"ok": False, "error": "fixture failure"}
                    value["ok"] = False
                    value["semantic"]["ok"] = False
                value["data"] = {"content": [{"type": "text", "text": json.dumps(payload)}]}
        return subprocess.CompletedProcess(command, 0, json.dumps(value), "")


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.clock = Clock()
        self.f = Fixture(self.root, self.clock)
        self.addCleanup(patch.stopall)
        patch.object(assess.time, "monotonic", self.clock.time).start()
        patch.object(assess.time, "sleep", self.clock.sleep).start()
        patch.object(assess.subprocess, "run", self.f.run).start()

    def run_campaign(self): return assess.run_live(self.f.args, self.f.evidence)

    def requests(self):
        return [(Fixture.option(c, "-EvidenceLabel"), json.loads(Fixture.option(c, "-ArgumentsJson")))
                for c, _ in self.f.calls if "-ArgumentsJson" in c]

    def test_complete_campaign_and_restore(self):
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        self.assertTrue(report["restored"])
        self.assertEqual(self.f.state, self.f.original)
        self.assertEqual(len(report["candidates"]), 5)
        self.assertFalse(report["domainVerified"])
        self.assertTrue(all(c["classification"] == "candidate_for_visual_review" for c in report["candidates"]))
        self.assertEqual(self.requests()[-1][0], "restore")

    def test_missing_live_services_fail_before_any_setting_call(self):
        self.f.tools = [assess.TOOL, "record", "input"]
        self.f.identity.update(complete=False, missing=["buildId"])
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertIn("communityshaders.renderscale", report["error"])
        self.assertIn("runtime identity: buildId", report["error"])
        self.assertEqual(self.requests(), [])
        self.assertEqual(self.f.state, self.f.original)
        self.assertTrue((self.f.evidence / "preflight.json").is_file())

    def test_visual_preflight_requires_screenshot_service(self):
        self.f.args.capture_episodes = True
        self.f.tools.remove("communityshaders.screenshot")
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertIn("communityshaders.screenshot", report["error"])
        self.assertEqual(self.requests(), [])

    def use_branch_readiness(self):
        self.f.tools.remove("communityshaders.upscaling_api")
        self.f.tools += ["communityshaders.renderscale", "inspect", "menu"]

    def test_branch_readiness_requires_advancing_frame_and_preserves_qualification_limit(self):
        self.use_branch_readiness()
        controller = assess.Controller(self.f.args, self.f.evidence)
        controller.preflight()
        controller.wait_scene()
        evidence = json.loads((self.f.evidence / "prepared-nr-scene.json").read_text())
        self.assertGreaterEqual(evidence["lastFrame"] - evidence["firstFrame"], 5)
        self.assertFalse(evidence["presentationQualified"])
        self.assertEqual(self.f.state, self.f.original)

    def test_branch_readiness_rejects_wrong_cell_before_colour_changes(self):
        self.use_branch_readiness()
        self.f.scene_cell = "OtherCell"
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertIn("expected cell", report["error"])
        self.assertEqual(self.f.state, self.f.original)

    def test_branch_readiness_rejects_unversioned_or_untyped_observations(self):
        self.use_branch_readiness()
        controller = assess.Controller(self.f.args, self.f.evidence)
        controller.preflight()
        for override in ({"readinessVersion": None}, {"ready": "true"}, {"targetGeneration": False}):
            self.f.readiness_override = override
            with self.assertRaises(assess.AssessmentError):
                controller.wait_scene()
        self.assertEqual(self.f.state, self.f.original)

    def test_branch_readiness_does_not_accept_resource_waiting_or_frozen_frames(self):
        self.use_branch_readiness()
        controller = assess.Controller(self.f.args, self.f.evidence)
        controller.preflight()
        for override in ({"ready": False, "reasons": ["resource_transition_pending"]},
                         {"status": dict(frame=100, controller=dict(revision=1, targetEpoch=2))}):
            self.f.readiness_override = override
            with self.assertRaises(assess.AssessmentError):
                controller.wait_scene()
        self.assertEqual(self.f.state, self.f.original)

    def test_missing_profiler_blocks_qualification_preflight(self):
        self.f.tools.remove("communityshaders.profiler")
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertIn("communityshaders.profiler", report["error"])
        self.assertEqual(self.requests(), [])

    def test_preflight_only_never_waits_or_mutates(self):
        self.f.args.preflight_only = True
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        self.assertEqual(report["scope"], "preflight_only")
        self.assertEqual(len(self.f.calls), 1)
        self.assertFalse(report["preflight"]["measurementsTaken"])

    def test_incomplete_or_unverified_identity_is_rejected(self):
        for field, value in (("complete", False), ("verified", False), ("listenerPid", True)):
            identity = runtime_identity()
            identity[field] = value
            controller = assess.Controller(self.f.args, self.f.evidence)
            with self.assertRaises(assess.IdentityUncertain):
                controller.bind_identity({"runtimeIdentity": identity})
        identity = runtime_identity()
        del identity["artifact"]["sha256"]
        with self.assertRaises(assess.IdentityUncertain):
            controller.bind_identity({"runtimeIdentity": identity})

    def test_unavailable_candidate_does_not_end_campaign(self):
        self.f.unavailable_identity = True
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        self.assertEqual(report["candidates"][0]["classification"], "unavailable_no_samples")
        self.assertEqual(report["candidates"][1]["classification"], "candidate_for_visual_review")
        self.assertTrue(report["restored"])

    def test_all_unavailable_is_not_success(self):
        self.f.unavailable_identity = True
        with patch.object(assess, "candidates", return_value=assess.candidates(False)[:1]):
            report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertTrue(report["restored"])

    def test_status_failure_is_fatal_not_candidate_unavailable(self):
        self.f.fail_label = "sample"
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertEqual(len(report["candidates"]), 1)
        self.assertTrue(report["restored"])

    def test_mutation_timeout_not_retried_or_restored(self):
        self.f.timeout_label = "identity_native-transport"
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertFalse(report["restored"])
        writes = [r for _, r in self.requests() if r["action"] == "configure"]
        self.assertEqual(len(writes), 1)
        self.assertTrue(list(self.f.evidence.glob("*.timeout.json")))

    def test_concurrent_ui_change_is_preserved(self):
        self.f.change_on_sample = True
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertFalse(report["restored"])
        self.assertEqual(self.f.state["settings"]["appearanceMix"], 0.42)
        self.assertNotIn("restore", [label for label, _ in self.requests()])

    def test_waits_reuse_identity_and_artifact_arguments(self):
        self.f.args.expected_build_id = "new-build"
        self.f.args.expected_artifact_sha256 = "a" * 64
        self.f.args.workspace_manifest = self.root / "workspace.json"
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        waits = [c for c, _ in self.f.calls if "-Condition" in c]
        self.assertGreater(len(waits), 1)
        self.assertIn("-ExpectedBuildId", waits[0])
        for c in waits[1:]:
            self.assertIn("-ExpectedRuntimeIdentityJson", c)
            self.assertIn("-ExpectedArtifactSha256", c)
            self.assertIn("-WorkspaceManifestPath", c)

    def test_call_passes_remaining_deadline_to_both_layers(self):
        c = assess.Controller(self.f.args, self.f.evidence)
        c.call({"action": "status"}, "probe", deadline=self.clock.now + 3.5)
        command, kwargs = self.f.calls[-1]
        self.assertLessEqual(kwargs["timeout"], 3.5)
        self.assertLessEqual(int(Fixture.option(command, "-TimeoutSeconds")), 3)
        self.assertLessEqual(int(Fixture.option(command, "-RequestTimeoutSeconds")), 3)

    def test_expired_deadline_never_dispatches(self):
        c = assess.Controller(self.f.args, self.f.evidence)
        with self.assertRaises(assess.AssessmentError):
            c.call({"action": "status"}, "probe", deadline=self.clock.now)
        self.assertFalse(self.f.calls)

    def test_late_response_cannot_be_accepted(self):
        real = self.f.run
        def slow(command, **kwargs):
            result = real(command, **kwargs)
            self.clock.now += kwargs["timeout"] + 1
            return result
        with patch.object(assess.subprocess, "run", slow):
            c = assess.Controller(self.f.args, self.f.evidence)
            with self.assertRaisesRegex(assess.AssessmentError, "after its operation deadline"):
                c.call({"action": "status"}, "probe", deadline=self.clock.now + 2)

    def test_collect_rejects_a_late_mock_even_without_controller(self):
        clock = self.clock
        class Slow:
            def call(self, *_args, **_kwargs):
                clock.now += 5
                return {"revision": 7, "measurements": [sample(), sample(1)]}
        with self.assertRaises(assess.AssessmentError):
            assess.collect(Slow(), 7, 0, 0, self.f.args)

    def test_single_second_budget_can_make_a_status_call(self):
        self.f.args.timeout = 1
        class Changed:
            def call(self, *_args, **_kwargs): return {"revision": 99}
        with self.assertRaises(assess.MutationUncertain):
            assess.collect(Changed(), 7, 0, 0, self.f.args)

    def test_successful_ab_episodes_and_restore(self):
        self.f.args.capture_episodes = True
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        self.assertTrue(all(c["baselineVerdict"]["valid"] for c in report["candidates"]))
        self.assertEqual(len(list(self.f.evidence.glob("*-capture-result.json"))), 10)

    def test_hidden_edit_ack_without_application_is_not_enough(self):
        self.f.args.capture_episodes = True
        self.f.hidden_ignored = True
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertEqual(len(report["candidates"]), 1)
        self.assertTrue(report["restored"])

    def test_unknown_capture_start_is_not_blindly_stopped(self):
        self.f.args.capture_episodes = True
        self.f.timeout_label = "capture-start"
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertFalse(report["restored"])
        capture = [c for c, _ in self.f.calls if "CaptureInteraction" in " ".join(c)]
        self.assertEqual(len(capture), 1)
        receipt = json.loads(next(self.f.evidence.glob("*-capture-result.json")).read_text())
        self.assertTrue(receipt["recoveryRequired"])

    def test_primary_capture_error_survives_cleanup_error(self):
        self.f.args.capture_episodes = True
        self.f.fail_label = "capture-observe"
        self.f.fail_stop = True
        report = self.run_campaign()
        self.assertFalse(report["ok"])
        self.assertFalse(report["restored"])
        receipt = json.loads(next(self.f.evidence.glob("*-capture-result.json")).read_text())
        self.assertIn("Capture episode failed", receipt["error"])
        self.assertIn("cleanup failed", receipt["cleanupError"])

    def test_multi_roi_fixture(self):
        self.f.multi = True
        self.f.args.expected_physical_slots = [0, 1, 4, 5]
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        self.assertEqual(report["regionCompleteness"], "explicit_fixture_slots")
        self.assertEqual(len(report["candidates"][0]["transport"]["samples"][0]), 4)

    def test_late_insertion_uses_late_profile(self):
        self.f.args.insertion = "final_ldr_pre_ui"
        report = self.run_campaign()
        self.assertTrue(report["ok"], report)
        req = next(r for label, r in self.requests() if label == "linear_srgb-transport")
        self.assertEqual(req["experiments"]["upscaled_center"]["transform"], "identity")
        self.assertEqual(req["experiments"]["final_ldr_pre_ui"]["transform"], "linear_to_srgb")


class EvidenceTests(unittest.TestCase):
    def test_pair_and_secondary(self):
        left, right, secondary = sample(), sample(1), sample(4)
        status = {"measurements": [left, right], "slots": [secondary["source"]]}
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90))
        status["measurements"].append(secondary)
        self.assertEqual(len(assess.fresh_groups(status, 7, 0, 90)[0]), 3)

    def test_secondary_does_not_disappear_when_observation_advances(self):
        status = {"measurements": [sample(), sample(1)], "slots": [sample(4, 101)["source"]]}
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90))

    def test_explicit_slot_manifest_requires_all_slots(self):
        status = {"measurements": [sample(), sample(1)], "slots": []}
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90, expected_slots={0, 1, 4, 5}))

    def test_duplicate_sample_is_not_silently_overwritten(self):
        self.assertFalse(assess.fresh_groups({"measurements": [sample(), sample(), sample(1)]}, 7, 0, 90))

    def test_missing_or_coerced_provenance_rejected(self):
        for field, value in (("physicalSlot", False), ("revision", "7"), ("processed", 1), ("failure", None), ("generation", None)):
            left = sample(); left["source"][field] = value
            self.assertFalse(assess.fresh_groups({"measurements": [left, sample(1)]}, 7, 0, 90), field)

    def test_boolean_is_not_numeric_measurement(self):
        left = sample(); left["values"][0] = True
        self.assertFalse(assess.fresh_groups({"measurements": [left, sample(1)]}, 7, 0, 90))

    def test_frame_wrap_and_backwards_frames(self):
        self.assertTrue(assess.frame_advanced(2, 0xfffffffe, 3))
        self.assertFalse(assess.frame_advanced(2, 0xfffffffe, 4))
        self.assertFalse(assess.frame_advanced(98, 100))
        self.assertFalse(assess.frame_advanced(True, 0))

    def test_counts_and_exposure_must_be_valid(self):
        for index, value in ((3, 0), (15, 4097), (12, -1), (16, 1), (17, 0.5), (18, 0), (19, 0), (7, -1)):
            left = sample(); left["values"][index] = value
            self.assertFalse(assess.assess_samples([[left, sample(1)]], True)["valid"], index)

    def test_hidden_visual_result_must_be_baseline(self):
        left = sample(shown=False); right = sample(1, shown=False)
        self.assertTrue(assess.assess_samples([[left, right]], True, shown=False)["valid"])
        left["values"][8] = 0.05
        self.assertFalse(assess.assess_samples([[left, right]], True, shown=False)["valid"])

    def test_no_missing_or_string_transport_flag(self):
        for value in (None, "false", 0, 1):
            left = sample(); left["source"]["transportBypass"] = value
            self.assertFalse(assess.assess_samples([[left, sample(1)]], True)["valid"])

    def test_captured_stamp_and_unit_fallback(self):
        for field in ("frame", "epoch", "sequence", "ambiguous"):
            left = sample(captured=True); del left["source"]["exposure"][field]
            self.assertFalse(assess.assess_samples([[left, sample(1, captured=True)]], True)["valid"])
        left = sample(captured=True); left["values"][23] = 2
        self.assertFalse(assess.assess_samples([[left, sample(1, captured=True)]], True)["valid"])

    def test_envelope_semantics(self):
        value = {"ok": True, "transportOk": True, "semantic": {"known": True, "ok": True},
                 "data": {"content": [{"type": "text", "text": '{"ok":true}'}]}}
        self.assertTrue(assess.unwrap(value)["ok"])
        for broken in (None, [], {"ok": True}, {**value, "semantic": "success"}, {**value, "data": None}):
            with self.assertRaises(assess.AssessmentError): assess.unwrap(broken)


if __name__ == "__main__":
    unittest.main()
