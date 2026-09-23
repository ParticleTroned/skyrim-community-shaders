#!/usr/bin/env python3
"""Execute a frozen HMD colour plan through the selected automation controller.

Plan-only unless --live is explicit. The caller owns an already prepared scene
and MO2 workspace. This module never launches a game or changes its camera/ROI.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import time
from typing import Any

import assess
import hmd_assess as hmd

NR_TOOL = "communityshaders.renderscale"
TERMINAL = {"completed", "completed_with_warnings", "stopped", "cancelled", "cancelled_partial",
            "failed", "failed_partial", "rejected"}
MODE = hmd.MODE


def colour_editable(value: dict) -> dict:
    result = assess.editable(value)
    result["experiments"]["captureFrameEvidence"] = value["experiments"]["captureFrameEvidence"]
    return result


def fixed_upscaling(configuration: dict) -> dict:
    result = copy.deepcopy(configuration["upscaling"])
    hmd.require(type(result.get("neuralRenderingEnabled")) is bool, "full runtime NR settings unavailable")
    result.pop("neuralRenderingEnabled")
    return result


def merge_checked(target: dict, patch: dict) -> dict:
    result = copy.deepcopy(target)
    for key, value in patch.items():
        hmd.require(key in result, "unknown candidate setting: " + key)
        if isinstance(result[key], dict):
            hmd.require(isinstance(value, dict), "object candidate setting required: " + key)
            result[key] = merge_checked(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def candidate_colour(original: dict, candidate: dict) -> dict:
    result = merge_checked(original, candidate["settings"])
    kind = candidate.get("sourceCondition", candidate["condition"])
    hmd.require(kind in MODE, "unsupported candidate condition")
    result["settings"].update(enabled=kind not in ("nr_off", "raw"), mode=MODE[kind])
    experiments = result["experiments"]
    # Readback diagnostics change Raw's resource path and are not an identity gate.
    hmd.require(experiments["diagnostics"] is False and experiments["transportBypass"] is False,
                "image campaign requires diagnostics and transport bypass disabled")
    experiments.update(captureFrameEvidence=True, captureEngineExposure=True, applyModelEdit=candidate["applyModelEdit"])
    if kind in ("nr_off", "raw", "managed_identity"):
        for profile in assess.PROFILE_NAMES:
            experiments[profile] = {"domain": "unknown", "transform": "identity",
                                    "exposureMultiplier": 1, "exposureSource": "manual"}
    for profile in assess.PROFILE_NAMES:
        hmd.require(assess.profile_valid(experiments[profile]), "invalid or unjustified colour profile")
    return result


class VisualController(assess.Controller):
    """Reuse identity, receipts and subprocess boundaries without performance gates."""

    def invoke(self, command: list[str], label: str, timeout: float, **kwargs: Any) -> dict:
        for path, digest in getattr(self, "script_hashes", {}).items():
            if hmd.digest(Path(path)) != digest:
                raise assess.MutationUncertain("Pinned automation implementation changed during campaign")
        command = [item for item in command if item != "-RequirePerformanceNeutral"]
        result = super().invoke(command, label, timeout, **kwargs)
        if label == "preflight":
            self.catalog = {item["name"]: item for item in result.get("data", {}).get("tools", [])}
        return result

    def ownership_guard(self, prepared: dict) -> None:
        owner = prepared["ownershipReceipt"]
        script = self.args.automation_root / "tools/mo2-control/Invoke-MO2Control.ps1"
        command = [self.args.pwsh, "-NoProfile", "-File", str(script), "access-status",
                   "-ConfigPath", owner["configPath"], "-NoExit", "-Compact"]
        receipt = self.invoke(command, "mo2-access-guard", 30)
        current = receipt.get("data", {}).get("access", {})
        if (receipt.get("ok") is not True or current.get("state") != "session-held"
                or current.get("leaseId") != owner["leaseId"] or current.get("sessionId") != owner["sessionId"]):
            raise assess.MutationUncertain("The prepared MO2 session no longer owns its access lease")

    def preflight(self) -> dict:
        result = super().preflight()
        for tool, field in (("record", "expectedCorrelationId"), (NR_TOOL, "expectedConfigurationFingerprint")):
            properties = self.catalog.get(tool, {}).get("inputSchema", {}).get("properties", {})
            hmd.require(field in properties, f"live {tool} schema lacks ownership guard {field}")
        return result

    def capture(self, action: str, session: Path, scheduled: dict) -> dict:
        command = [self.args.pwsh, "-NoProfile", "-File", str(self.capture_script), action,
                   "-SessionDirectory", str(session), "-RuntimePath", str(self.args.runtime),
                   "-ExpectedRuntimeIdentityJson", json.dumps(self.expected_identity, separators=(",", ":")),
                   "-NoExit", "-Compact"]
        if action == "start":
            command += ["-VisualMode", "sequence", "-PreferredView", "left_eye", "-RecordIntervalMs", "10",
                        "-FrameIntervalMs", str(scheduled["intervalMs"]),
                        "-MaximumFrames", str(scheduled["minimumPairs"]), "-CaptureTimeoutSeconds", "30"]
        receipt = self.invoke(command, f"capture-{scheduled['scheduleOrdinal']:03}-{action}", 60)
        if receipt.get("ok") is not True:
            raise assess.AssessmentError("Capture controller failed: " + str(receipt.get("errors")))
        return receipt


def snapshot(controller: Any, label: str) -> tuple[dict, dict]:
    colour = controller.call({"action": "status"}, label + "-colour")
    rendering = controller.call({"action": "nr_status"}, label + "-nr", tool=NR_TOOL)["neuralRendering"]
    hmd.require(colour.get("captureEvidenceSchemaVersion") == 1, "DLL lacks capture evidence instrumentation")
    configuration = rendering.get("requestedConfiguration")
    hmd.require(configuration and rendering.get("requestedConfigurationFingerprint"), "DLL lacks requested configuration identity")
    if colour_editable(configuration["color"]) != colour_editable(colour):
        raise assess.MutationUncertain("Colour settings changed between status snapshots")
    return colour, rendering


class Campaign:
    def __init__(self, controller: Any, plan: dict, prepared: dict, directory: Path,
                 *, timeout: float = 45, warmup: int = 16, clock=time.monotonic, sleep=time.sleep):
        self.controller, self.plan, self.prepared, self.directory = controller, plan, prepared, directory
        self.timeout, self.warmup, self.clock, self.sleep = timeout, warmup, clock, sleep
        self.original: dict | None = None
        self.owned_colour: dict | None = None
        self.owned_revision: int | None = None
        self.owned_upscaling: dict | None = None
        self.last_frame: int | None = None
        self.uncertain = False
        self.active_capture: Path | None = None
        self.index = {"schema": "csx-nr-hmd-capture-v1", "planPath": prepared["planPath"],
                      "planSha256": prepared["planSha256"], "sceneFingerprint": plan["sceneFingerprint"],
                      "fixedSettings": plan["fixedSettings"], "producer": prepared["producer"],
                      "devbench": prepared["devbench"], "sessionId": prepared["screenshotSessionId"],
                      "sequences": [], "performanceCampaign": False, "complete": False,
                      "untestedConditions": ["physical display and lens behaviour", "controlled lighting-change exposure recovery"]}

    def save(self) -> None:
        assess.atomic_json(self.directory / "campaign-index.json", self.index)

    def guard(self, label: str) -> tuple[dict, dict]:
        if hmd.digest(Path(self.prepared["planPath"])) != self.prepared["planSha256"]:
            raise assess.MutationUncertain("Frozen plan changed during capture")
        colour, nr = snapshot(self.controller, label)
        if self.owned_revision is not None:
            if (colour.get("revision") != self.owned_revision or colour_editable(colour) != self.owned_colour
                    or nr["requestedConfiguration"]["upscaling"] != self.owned_upscaling):
                raise assess.MutationUncertain("Configuration ownership changed; no further settings mutation")
        if fixed_upscaling(nr["requestedConfiguration"]) != self.plan["fixedSettings"]:
            raise assess.MutationUncertain("Fixed ROI or graphics settings changed")
        return colour, nr

    def apply(self, candidate: dict) -> tuple[dict, dict]:
        self.controller.ownership_guard(self.prepared)
        colour, nr = self.guard("before-configure")
        desired = candidate_colour(self.original["color"], candidate)
        try:
            if desired != self.owned_colour:
                changed = self.controller.call({"action": "configure", "expectedRevision": self.owned_revision,
                                                **desired}, "configure-colour")
                if (type(changed.get("revision")) is not int or changed["revision"] != self.owned_revision + 1
                        or colour_editable(changed) != desired):
                    raise assess.MutationUncertain("Colour configuration acknowledgement is inconsistent")
                self.owned_revision, self.owned_colour = changed["revision"], desired
                colour, nr = self.guard("after-colour-configure")
            enabled = candidate.get("sourceCondition", candidate["condition"]) != "nr_off"
            if self.owned_upscaling["neuralRenderingEnabled"] is not enabled:
                response = self.controller.call({"action": "nr_configure", "enabled": enabled,
                                                 "expectedConfigurationFingerprint": nr["requestedConfigurationFingerprint"]},
                                                "configure-nr-master", tool=NR_TOOL)
                returned = response["neuralRendering"]["requestedConfiguration"]["upscaling"]
                desired_upscaling = {**self.owned_upscaling, "neuralRenderingEnabled": enabled}
                if returned != desired_upscaling:
                    raise assess.MutationUncertain("NR master change modified unexpected settings")
                self.owned_upscaling = desired_upscaling
            return self.guard("after-configure")
        except BaseException:
            # A lost mutation acknowledgement cannot be retried or rolled back safely.
            self.uncertain = True
            raise

    def await_applied(self, candidate: dict) -> dict:
        deadline = self.clock() + self.timeout
        floor = None
        reason = "no frame-attributed stereo result"
        while self.clock() < deadline:
            colour, nr = self.guard("await-applied")
            evidence = nr.get("captureEvidence", {})
            for pair in evidence.get("routes", []):
                expected = {"configuration": nr["requestedConfiguration"],
                            "configurationFingerprint": nr["requestedConfigurationFingerprint"],
                            "colorRevision": self.owned_revision,
                            "inputEpoch": colour["inputEpoch"][assess.PROFILE_NAMES.index(self.prepared["insertion"])],
                            "insertion": self.prepared["insertion"],
                            "maximumRenderFrameAge": self.prepared.get("maximumRenderFrameAge", 0)}
                try:
                    frames = [pair[eye]["frame"] for eye in hmd.EYES]
                    hmd.check_nr(pair, expected, candidate, {"engineFrame": max(frames)}, require_exposure=False)
                    if floor is None:
                        hmd.require(self.last_frame is None or all(assess.frame_advanced(frame, self.last_frame) for frame in frames),
                                    "applied state has not advanced beyond the previous capture")
                        floor = min(frames)
                    hmd.require(all(assess.frame_advanced(frame, floor, self.warmup) for frame in frames),
                                "applied frame has not advanced beyond warmup")
                    self.last_frame = max(frames)
                    profile = self.owned_colour["experiments"][self.prepared["insertion"]]
                    expected["requiresCapturedExposure"] = profile["exposureSource"] != "manual"
                    expected["exposureAge"] = 1 if profile["exposureSource"] == "captured_hdr_previous" else 0
                    return expected
                except (hmd.EvidenceError, KeyError, TypeError) as error:
                    reason = str(error)
            self.sleep(min(0.25, max(0, deadline - self.clock())))
        raise assess.AssessmentError("Fresh applied-state gate failed: " + reason)

    def collect(self, scheduled: dict, candidate: dict, expected: dict) -> dict:
        session = self.directory / "captures" / f"sequence-{scheduled['scheduleOrdinal']:03}"
        entry: dict[str, Any] = {"scheduleOrdinal": scheduled["scheduleOrdinal"], "expected": expected,
                                 "captureSessionDirectory": str(session)}
        started = False
        failure: BaseException | None = None
        try:
            self.controller.ownership_guard(self.prepared)
            self.guard("before-capture")
            try:
                start = self.controller.capture("start", session, scheduled)
                started = True
                self.active_capture = session
                entry["captureSessionId"] = start["data"]["sessionId"]
            except BaseException as error:
                self.uncertain = True
                raise assess.MutationUncertain("Capture start outcome uncertain; inspect retained startup recovery") from error
            deadline = self.clock() + self.timeout + scheduled["minimumPairs"] * scheduled["intervalMs"] / 1000
            while self.clock() < deadline:
                self.guard("capture-state-guard")
                observation = self.controller.capture("observe", session, scheduled)["data"]["observation"]
                receipt = observation["screenshot"]["receipt"]
                if receipt and receipt.get("state") in TERMINAL:
                    entry["terminalReceipt"] = receipt
                    manifest_path = receipt.get("manifest", {}).get("finalPath")
                    artifacts = [item for item in receipt.get("artifacts", []) if item.get("path") == manifest_path]
                    hmd.require(len(artifacts) == 1, "terminal capture lacks committed final manifest")
                    descriptor = artifacts[0]
                    path = hmd.committed(descriptor, session)
                    manifest = hmd.read_json(path)
                    hmd.require(manifest.get("state") == "final" and manifest["counts"]["inFlight"] == 0,
                                "capture manifest is not finalized")
                    hmd.require(manifest["producer"] == self.prepared["producer"]
                                and manifest["sessionId"] == self.prepared["screenshotSessionId"], "screenshot producer/session changed")
                    hmd.check_capture_descriptor(manifest["requested"])
                    hmd.check_capture_descriptor(manifest["effective"])
                    entry["manifest"] = descriptor
                    entry["captureDiagnostics"] = self.exposure_diagnostics(manifest)
                    diagnostics_path = session / "capture-diagnostics.json"
                    hmd.save_json(diagnostics_path, entry["captureDiagnostics"])
                    entry["captureDiagnosticsArtifact"] = {"path": str(diagnostics_path),
                        "bytes": diagnostics_path.stat().st_size, "sha256": hmd.digest(diagnostics_path)}
                    entry["excludedChildren"] = []
                    pairs = []
                    for child in manifest["children"]:
                        try:
                            acquisition = child["actual"]["acquisition"]
                            if "planes" not in expected:
                                expected["planes"] = {plane["eye"]: plane for plane in acquisition["planes"]}
                            pair = hmd.check_pair(child, path.parent, expected, candidate, self.plan["regionPolicy"],
                                                  capture_diagnostics=entry["captureDiagnostics"], sequence=manifest)
                            pairs.append(pair)
                        except (hmd.EvidenceError, KeyError, TypeError) as error:
                            entry["excludedChildren"].append({"ordinal": child.get("ordinal"), "reason": str(error)})
                    entry["validatedPairs"] = len(pairs)
                    hmd.require(len(pairs) >= scheduled["minimumPairs"], "insufficient valid native stereo pairs")
                    self.last_frame = max(pair["acquisition"]["engineFrame"] for pair in pairs)
                    entry["motionEvidence"] = acquisition_motion(pairs, self.plan)
                    hmd.require(entry["motionEvidence"]["available"], entry["motionEvidence"]["reason"])
                    self.guard("after-capture")
                    break
                self.sleep(min(0.5, max(0, deadline - self.clock())))
            else:
                raise assess.AssessmentError("Screenshot sequence did not commit within its deadline")
        except BaseException as error:
            failure = error
            entry["error"] = str(error)
            if isinstance(error, (assess.MutationUncertain, assess.IdentityUncertain)):
                self.uncertain = True
        finally:
            if started:
                try:
                    # Capture ownership is separate from configuration ownership;
                    # the controller still verifies runtime and recording identities.
                    stopped = self.controller.capture("stop", session, scheduled)
                    entry["stopReceipt"] = stopped
                    recording = stopped["data"]["recording"]["stopReceipt"]
                    hmd.require(recording.get("meta", {}).get("correlationId") == entry["captureSessionId"],
                                "recording stop identity does not match the owned sequence")
                    entry["recording"] = recording
                    self.active_capture = None
                    recording_file = Path(self.prepared["recordingDirectory"]) / Path(recording["path"]).name
                    retained = hmd.preserve(recording_file, self.directory / "recordings" / f"{scheduled['scheduleOrdinal']:03}.json")
                    entry["recordingArtifact"] = retained
                    recording["artifacts"] = [retained]
                    entry["recordedScene"] = recording_scene(hmd.read_json(Path(retained["path"])), self.plan)
                    hmd.require(entry["recordedScene"]["qualified"], entry["recordedScene"]["reason"])
                    hmd.require(recording.get("limitReached") is False and recording.get("unrecordedTailMs") == 0,
                                "recording ended early or reached its limit")
                except BaseException as error:
                    if self.active_capture is not None:
                        self.uncertain = True
                        entry["cleanupError"] = str(error)
                    else:
                        entry["evidenceError"] = str(error)
                    if failure is None:
                        failure = error
            entry["recoveryRequired"] = self.active_capture is not None or self.uncertain
            self.index["sequences"].append(entry)
            self.save()
        if failure is not None:
            raise failure
        return entry

    def exposure_diagnostics(self, manifest: dict) -> dict:
        stamps = {}
        gaps = []
        retained = []
        for child in manifest.get("children", []):
            companion = hmd.captured_diagnostics(child)
            if companion is not None:
                retained.append({"ordinal": child.get("ordinal"), "diagnostics": companion})
                continue
            evidence = child.get("actual", {}).get("acquisition", {}).get("nrEvidence", {})
            sources = [("engine", evidence.get("engineExposure", {}))]
            sources += [(eye, evidence.get(eye, {}).get("exposure", {})) for eye in hmd.EYES]
            for role, source in sources:
                stamp = {key: source.get(key) for key in ("frame", "epoch", "sequence")}
                if all(type(value) is int and value > 0 for value in stamp.values()):
                    stamps[tuple(stamp.values())] = stamp
                else:
                    gaps.append({"ordinal": child.get("ordinal"), "role": role, "reason": "capture has no exact exposure stamp"})
        result = {"requests": [], "responses": [], "exposures": [], "gaps": gaps,
                  "captureOwned": retained}
        if not stamps:
            return result
        hmd.require(len(stamps) <= 64, "exposure stamp count exceeds the bounded diagnostics contract")
        deadline = self.clock() + min(15, self.timeout)
        for attempt in range(3):
            payload = {"action": "capture_diagnostics", "stamps": list(stamps.values())}
            result["requests"].append(payload)
            try:
                reply = self.controller.call(payload, f"capture-exposure-{attempt + 1}", deadline=deadline)
                hmd.require(reply.get("captureEvidenceSchemaVersion") == 1 and isinstance(reply.get("exposures"), list),
                            "invalid exact exposure diagnostics response")
                result["responses"].append(reply)
                result["exposures"] = reply["exposures"]
                pending = any(not item.get("available") and "pending" in str(item.get("reason", "")).lower()
                              for item in reply["exposures"])
                if not pending or self.clock() >= deadline:
                    break
                self.sleep(min(0.25, max(0, deadline - self.clock())))
            except (assess.AssessmentError, hmd.EvidenceError, TimeoutError) as error:
                result["responses"].append({"ok": False, "error": str(error)})
                break
        return result

    def restore(self) -> None:
        if self.original is None or self.uncertain or self.active_capture is not None:
            self.index["restoration"] = {"performed": False, "reason": "ownership or capture finalization unproven"}
            return
        self.controller.ownership_guard(self.prepared)
        colour, nr = self.guard("restore-guard")
        if self.owned_upscaling["neuralRenderingEnabled"] != self.original["upscaling"]["neuralRenderingEnabled"]:
            reply = self.controller.call({"action": "nr_configure", "enabled": self.original["upscaling"]["neuralRenderingEnabled"],
                                          "expectedConfigurationFingerprint": nr["requestedConfigurationFingerprint"]},
                                         "restore-nr-master", tool=NR_TOOL)
            if reply["neuralRendering"]["requestedConfiguration"]["upscaling"] != self.original["upscaling"]:
                raise assess.MutationUncertain("NR restoration acknowledgement differs")
            self.owned_upscaling = copy.deepcopy(self.original["upscaling"])
        self.guard("restore-colour-guard")
        if self.owned_colour != self.original["color"]:
            restored = self.controller.call({"action": "configure", "expectedRevision": self.owned_revision,
                                             **self.original["color"]}, "restore-colour")
            if colour_editable(restored) != self.original["color"] or restored["revision"] != self.owned_revision + 1:
                raise assess.MutationUncertain("Colour restoration acknowledgement differs")
            self.owned_revision, self.owned_colour = restored["revision"], copy.deepcopy(self.original["color"])
        self.guard("restoration-verified")
        self.index["restoration"] = {"performed": True, "revision": self.owned_revision}

    def run(self) -> dict:
        try:
            self.controller.preflight()
            self.controller.ownership_guard(self.prepared)
            self.index["devbench"] = verify_devbench(self.controller, self.prepared["devbench"], self.directory)
            self.controller.wait_scene()
            validate_fixed_scene(self.plan["fixedScene"])
            colour, nr = self.guard("initial")
            self.original = {"upscaling": copy.deepcopy(nr["requestedConfiguration"]["upscaling"]),
                             "color": colour_editable(colour)}
            self.owned_upscaling = copy.deepcopy(self.original["upscaling"])
            self.owned_colour, self.owned_revision = copy.deepcopy(self.original["color"]), colour["revision"]
            hmd.require(nr["requestedConfiguration"] == self.prepared["configuration"], "prepared configuration changed")
            for scheduled in self.plan["schedule"]:
                candidate = self.plan["candidateMapping"][scheduled["candidateId"]]
                self.apply(candidate)
                expected = self.await_applied(candidate)
                self.collect(scheduled, candidate, expected)
                if scheduled["scheduleOrdinal"] == 3:
                    self.index["initialBaselineRepeatability"] = baseline_repeatability(self.index["sequences"], self.plan)
                    self.save()
                    hmd.require(self.index["initialBaselineRepeatability"]["qualified"],
                                "unchanged baselines contain motion-confounded image regions")
            self.index["complete"] = True
        except BaseException as error:
            self.index["error"] = str(error) or type(error).__name__
            self.uncertain |= isinstance(error, (assess.MutationUncertain, assess.IdentityUncertain))
        finally:
            captured = {entry["scheduleOrdinal"] for entry in self.index["sequences"]}
            for scheduled in self.plan["schedule"]:
                if scheduled["scheduleOrdinal"] not in captured:
                    self.index["sequences"].append({"scheduleOrdinal": scheduled["scheduleOrdinal"],
                                                     "notRunReason": self.index.get("error", "campaign stopped")})
            try:
                self.restore()
            except BaseException as error:
                self.uncertain = True
                self.index["restoration"] = {"performed": False, "error": str(error)}
            self.index["recoveryRequired"] = self.uncertain or self.active_capture is not None
            self.index["scheduleComplete"] = self.index["complete"]
            self.index["imageAssessmentComplete"] = False
            self.index["ok"] = self.index["complete"] and not self.index["recoveryRequired"]
            self.save()
        return self.index


def acquisition_motion(pairs: list[dict], plan: dict) -> dict:
    """Only exact acquisition camera evidence can qualify the fixed-scene gate."""
    observations = [pair["acquisition"].get("cameraEvidence") for pair in pairs]
    result = {"available": False, "fixedSceneFingerprint": plan["sceneFingerprint"],
              "basis": "engine_cached_unjittered_world_matrices", "observations": observations, "drift": []}
    try:
        expected = plan["fixedScene"]["cameraEvidence"]
        hmd.require(bool(observations), "no captured camera observations")
        for pair, observed in zip(pairs, observations):
            hmd.check_camera(pair["acquisition"], plan["fixedScene"])
            drift = {"ordinal": pair["ordinal"]}
            result["drift"].append(drift)
            for key in ("view", "projection", "positionAdjust"):
                actual_matrices, fixed_matrices = observed[key], expected[key]
                delta = max(abs(actual - fixed) for actual_eye, fixed_eye in zip(actual_matrices, fixed_matrices)
                            for actual, fixed in zip(actual_eye, fixed_eye))
                drift[key + "MaxAbsDelta"] = delta
        result.update(available=True, reason="")
    except (hmd.EvidenceError, KeyError, TypeError, ValueError) as error:
        result["reason"] = str(error)
    return result


def verify_devbench(controller: Any, expected: dict, directory: Path) -> dict:
    path = Path(expected["path"])
    build = expected["buildIdentity"]
    hmd.require(path.is_absolute() and path.is_file() and type(expected["bytes"]) is int
                and expected["bytes"] > 0 and path.stat().st_size == expected["bytes"], "DevBench physical DLL size/path mismatch")
    hmd.require(hmd.digest(path) == expected["sha256"].lower(), "DevBench physical DLL hash mismatch")
    hmd.require(isinstance(build.get("version"), str) and bool(build["version"])
                and isinstance(build.get("sourceCommit"), str) and len(build["sourceCommit"]) == 40
                and all(char in "0123456789abcdef" for char in build["sourceCommit"].lower()), "DevBench build identity incomplete")
    state = controller.call({"kind": "state"}, "devbench-producer", tool="inspect", explicit_success=False)
    hmd.require(state.get("plugin") == "devbench" and state.get("version") == build["version"]
                and state.get("pid") == controller.expected_identity["listenerPid"] and state.get("vr") is True,
                "DevBench runtime version/process differs from prepared host identity")
    retained = hmd.preserve(path, directory / "provenance" / "devbench.dll")
    hmd.require(retained["sha256"] == expected["sha256"].lower() and retained["bytes"] == expected["bytes"],
                "DevBench DLL changed while runtime identity was observed")
    receipt_path = directory / "provenance" / "devbench-identity.json"
    receipt = {"observedUtc": hmd.utc(), "runtime": state, "physicalPath": str(path),
               "artifact": retained, "buildIdentity": build, "runtimeMatched": True,
               "physicalModulePathVerified": False,
               "limitation": "runtime version/process and physical artifact matched separately; loaded MO2 provider path is not exposed by DevBench"}
    hmd.save_json(receipt_path, receipt)
    return {**retained, "physicalPath": str(path), "buildIdentity": build, "runtimeMatched": True,
            "observedVersion": state["version"], "observedPid": state["pid"], "physicalModulePathVerified": False,
            "identityReceipt": {"path": str(receipt_path), "bytes": receipt_path.stat().st_size, "sha256": hmd.digest(receipt_path)}}


def validate_fixed_scene(scene: dict) -> None:
    camera = scene["cameraEvidence"]
    for key, width in (("view", 16), ("projection", 16), ("positionAdjust", 4)):
        matrices, tolerance = camera[key], camera[key + "Tolerance"]
        hmd.require(len(matrices) == 2 and all(len(matrix) == width for matrix in matrices)
                    and all(assess.number(value) for matrix in matrices for value in matrix), "frozen stereo matrices required")
        hmd.require(assess.number(tolerance) and tolerance >= 0, "frozen camera tolerance required")
    recording = scene["recordingScene"]
    hmd.require(all(key in recording for key in ("cell", "cellFormID", "interior", "weatherFormID")),
                "frozen recording cell and weather identity required")
    hmd.require(assess.number(scene["gameHour"]) and 0 <= scene["gameHour"] < 24
                and assess.number(scene["gameHourTolerance"]) and 0 <= scene["gameHourTolerance"] < 12,
                "frozen game time and allowed drift required")


def recording_scene(recording: dict, plan: dict) -> dict:
    return hmd.recording_scene(recording, plan)


def baseline_repeatability(entries: list[dict], plan: dict) -> dict:
    """Measure originals before candidate dispatch; never normalize or align."""
    references = {}
    rows = []
    rejected = []
    for entry in entries:
        manifest_path = hmd.committed(entry["manifest"], Path(entry["captureSessionDirectory"]))
        manifest = hmd.read_json(manifest_path)
        for child in manifest["children"]:
            for eye in hmd.EYES:
                artifacts = [item for item in child["artifacts"] if item.get("actual", {}).get("view") == eye + "_eye"]
                hmd.require(len(artifacts) == 1, "baseline pair artifact identity missing")
                path = hmd.committed(artifacts[0], manifest_path.parent)
                for region in plan["regionPolicy"]["eyes"][eye]["regions"]:
                    pixels = hmd.crop_pixels(str(path), region["rect"])
                    key = (eye, region["id"])
                    if key not in references:
                        references[key] = pixels
                    metrics = hmd.compare_pixels(pixels, references[key])
                    row = {"scheduleOrdinal": entry["scheduleOrdinal"], "ordinal": child["ordinal"],
                           "eye": eye, "region": region["id"], **metrics}
                    rows.append(row)
                    if metrics["gradient_sign_mismatch"] > plan["regionPolicy"]["motionGradientMismatchLimit"]:
                        rejected.append({"scheduleOrdinal": entry["scheduleOrdinal"], "ordinal": child["ordinal"],
                                         "eye": eye, "region": region["id"], "reason": "baseline gradient mismatch exceeds fixed motion threshold"})
    return {"qualified": not rejected, "measurements": rows, "excluded": rejected,
            "normalization": "none", "interpretation": "baseline animation/exposure drift and regional variation; no candidate ranking"}


def validate_plan(plan: dict) -> None:
    hmd.check_regions(plan["regionPolicy"])
    hmd.require(plan.get("schema") == hmd.VERSION and plan.get("performanceCampaign") is False, "invalid colour plan")
    hmd.require(plan.get("regionPolicySha256") == hmd.value_digest(plan["regionPolicy"]), "region policy hash mismatch")
    candidates = []
    for candidate_id in plan["candidateOrder"]:
        item = plan["candidateMapping"][candidate_id]
        candidates.append({key: value for key, value in item.items() if key not in ("applyModelEdit", "hiddenCandidateId")})
    spec = {"sceneFingerprint": plan["sceneFingerprint"], "fixedScene": plan["fixedScene"],
            "fixedSettings": plan["fixedSettings"], "candidates": candidates, "intervalMs": plan["intervalMs"]}
    regenerated = hmd.make_plan(spec, plan["regionPolicy"], plan["seed"])
    hmd.require(regenerated["schedule"] == plan["schedule"] and regenerated["candidateMapping"] == plan["candidateMapping"],
                "candidate mapping or counterbalanced schedule differs from the frozen seed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepared-session", type=Path)
    parser.add_argument("--transport-selection", type=Path)
    parser.add_argument("--automation-root", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--workspace-manifest", type=Path)
    parser.add_argument("--artifact-path", type=Path)
    parser.add_argument("--expected-build-id")
    parser.add_argument("--expected-artifact-sha256")
    parser.add_argument("--expected-cell")
    parser.add_argument("--pwsh", default="pwsh")
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    plan = hmd.read_json(args.plan)
    validate_plan(plan)
    if not args.live:
        print(json.dumps({"mode": "plan_only", "planSha256": hmd.digest(args.plan), "sequences": len(plan["schedule"]),
                          "settingsChanged": False, "requires": "prepared scene, owned workspace, selected controller lane and --live"}, indent=2))
        return 0
    if not all((args.prepared_session, args.transport_selection, args.automation_root, args.runtime, args.evidence_dir,
                args.workspace_manifest, args.artifact_path, args.expected_build_id, args.expected_artifact_sha256, args.expected_cell)):
        parser.error("--live requires explicit prepared-session, transport-selection, automation-root, runtime, new evidence-dir, workspace-manifest, artifact-path, expected-build-id, expected-artifact-sha256 and expected-cell")
    selected = hmd.read_json(args.transport_selection)
    hmd.require(selected.get("selected") == "controller" and selected.get("directDevBenchTools") == []
                and selected.get("catalogCheckedUtc"), "controller lane requires prior complete callable-tool discovery; direct transport cannot be mixed")
    prepared = hmd.read_json(args.prepared_session)
    hmd.require(prepared.get("sceneFingerprint") == plan["sceneFingerprint"] and prepared.get("ownershipReceipt")
                and prepared.get("runtimeIdentity") and prepared.get("insertion") in assess.PROFILE_NAMES,
                "prepared scene, runtime and ownership evidence required")
    for name in ("producer", "devbench", "screenshotSessionId", "configuration", "recordingDirectory"):
        hmd.require(bool(prepared.get(name)), "prepared session missing " + name)
    hmd.require(Path(prepared["recordingDirectory"]).is_absolute(), "physical recordingDirectory must be absolute")
    ownership = prepared["ownershipReceipt"]
    hmd.require(all(isinstance(ownership.get(key), str) and ownership[key]
                    for key in ("configPath", "leaseId", "sessionId")), "current MO2 lease/session identity required")
    hmd.require(Path(ownership["configPath"]).is_absolute(), "MO2 configuration path must be absolute")
    hmd.require(type(prepared.get("maximumRenderFrameAge", 0)) is int
                and 0 <= prepared.get("maximumRenderFrameAge", 0) <= 3, "render-frame age must be preregistered in 0..3")
    args.plan, args.automation_root, args.runtime = args.plan.resolve(), args.automation_root.resolve(), args.runtime.resolve()
    args.evidence_dir = args.evidence_dir.resolve()
    args.evidence_dir.mkdir(parents=True, exist_ok=False)
    prepared.update(planPath=str(args.plan), planSha256=hmd.digest(args.plan))
    hmd.save_json(args.evidence_dir / "prepared-session.json", prepared)
    hmd.save_json(args.evidence_dir / "transport-selection.json", selected)
    args.capture_episodes = True
    controller = VisualController(args, args.evidence_dir)
    controller.expected_identity = prepared["runtimeIdentity"]
    script_names = ("devbench-control/Invoke-DevBenchControl.ps1", "devbench-control/DevBenchControl.psm1",
                    "capture-interaction-control/Invoke-CaptureInteraction.ps1", "capture-interaction-control/CaptureInteractionControl.psm1",
                    "mo2-control/Invoke-MO2Control.ps1", "mo2-control/MO2Control.psm1")
    controller.script_hashes = {str(args.automation_root / "tools" / name): hmd.digest(args.automation_root / "tools" / name)
                                for name in script_names}
    hmd.save_json(args.evidence_dir / "automation-bindings.json", {"root": str(args.automation_root),
                  "scriptSha256": controller.script_hashes, "expectedSourceCommit": prepared.get("automationSourceCommit"),
                  "sourceCommitVerified": False})
    report = Campaign(controller, plan, prepared, args.evidence_dir).run()
    print(json.dumps({"ok": report["ok"], "complete": report["complete"], "recoveryRequired": report["recoveryRequired"],
                      "index": str(args.evidence_dir / "campaign-index.json"), "error": report.get("error")}, indent=2))
    return 0 if report["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
