#!/usr/bin/env python3
"""Bounded NR colour candidate assessment through skyrim-vr-automation/dev.

No direct HTTP, tokens, profile edits, scene mutations, or game launches. Default
is plan-only. Live runs require an already prepared static scene/test workspace.
Numerical ranking measures source drift, NOT a proven NVIDIA colour contract.
"""
from __future__ import annotations
import argparse
import copy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import time
from typing import Any
from verify_assets import verify

TOOL = "communityshaders.nr_color"
SETTING_KEYS = ("schemaVersion", "enabled", "mode", "detailStrength", "appearanceMix", "maximumDetailStops")
PROFILE_KEYS = ("domain", "transform", "exposureMultiplier", "exposureSource")
EXPERIMENT_KEYS = ("transportBypass", "diagnostics", "captureEngineExposure", "applyModelEdit")
PROFILE_NAMES = ("upscaled_center", "final_ldr_pre_ui")


class AssessmentError(RuntimeError):
    pass


class MutationUncertain(AssessmentError):
    """No further mutation, including restoration, has proven ownership."""


class CandidateUnavailable(AssessmentError):
    """Only a healthy, unchanged session with no qualifying samples may continue."""


class IdentityUncertain(MutationUncertain):
    pass


def integer(value: Any, maximum: int = (1 << 64) - 1) -> bool:
    return type(value) is int and 0 <= value <= maximum


def number(value: Any) -> bool:
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except (OverflowError, TypeError):
        return False


def frame_advanced(frame: int, floor: int, warmup: int = 0) -> bool:
    # Frame counters are uint32. Reject backwards/stale half-ranges, but allow wrap.
    return (integer(frame, 0xffffffff) and integer(floor, 0xffffffff)
            and warmup < ((frame - floor) & 0xffffffff) < 0x80000000)


def editable(status: dict) -> dict:
    """Never send read-only provenance fields from status back to configure."""
    result = {"settings": {key: status["settings"][key] for key in SETTING_KEYS},
              "experiments": {key: status["experiments"][key] for key in EXPERIMENT_KEYS}}
    for name in PROFILE_NAMES:
        result["experiments"][name] = {key: status["experiments"][name][key] for key in PROFILE_KEYS}
    return result


def unwrap(receipt: dict) -> dict:
    if not isinstance(receipt, dict):
        raise AssessmentError("Controller returned a non-object receipt")
    semantic = receipt.get("semantic") or {}
    if not isinstance(semantic, dict):
        raise AssessmentError("Malformed controller semantic receipt")
    if receipt.get("transportOk") is not True or receipt.get("ok") is not True:
        raise AssessmentError("Controller rejected call or transport failed; inspect its receipt")
    if semantic.get("known") is not True or semantic.get("ok") is not True:
        raise AssessmentError("Unverified/failed DevBench semantic result")
    data = receipt.get("data")
    if not isinstance(data, dict):
        raise AssessmentError("Missing tools/call data")
    content = data.get("content")
    if not isinstance(content, list) or len(content) != 1:
        raise AssessmentError("Unexpected tools/call response shape")
    payload = content[0]
    if isinstance(payload, dict) and payload.get("type") == "text":
        payload = json.loads(payload["text"])
    if not isinstance(payload, dict) or payload.get("ok") is not True:
        raise AssessmentError("NR colour API did not explicitly succeed")
    return payload


def fresh_groups(status: dict, revision: int, insertion: int, after_frame: int,
                 warmup: int = 0, expected_slots: set[int] | None = None) -> list[list[dict]]:
    """Group only typed, frame-matched measurements; retain known secondary slots.

    API v2 does not publish an immutable per-transaction region manifest. An
    explicitly supplied slot set is required to prove a fixed multi-ROI fixture;
    otherwise this checks all regions visible in the retained observations.
    """
    groups: dict[tuple, dict[int, dict]] = {}
    duplicates: set[tuple] = set()
    for item in status.get("measurements", []):
        if not isinstance(item, dict):
            continue
        source, values = item.get("source"), item.get("values")
        if not isinstance(source, dict) or not isinstance(values, list):
            continue
        slot = source.get("physicalSlot")
        if not integer(slot, 7):
            continue
        if (source.get("revision") != revision or type(source.get("revision")) is not int
                or source.get("insertionPoint") != insertion or type(source.get("insertionPoint")) is not int
                or source.get("processed") is not True or source.get("failure") != ""
                or not integer(source.get("frame"), 0xffffffff)
                or not integer(source.get("generation"))
                or not integer(source.get("sourceWorldFrame"), 0xfffffffe)
                or not frame_advanced(source.get("sourceWorldFrame"), after_frame, warmup)):
            continue
        if len(values) != 24 or not all(number(x) for x in values):
            continue
        key = (source["frame"], source["sourceWorldFrame"], source["generation"], slot % 4 // 2)
        if slot in groups.setdefault(key, {}):
            duplicates.add(key)
        groups[key][slot] = item
    output = []
    for key, items in groups.items():
        if key in duplicates:
            continue
        if expected_slots and not any(slot % 4 // 2 == key[3] for slot in expected_slots):
            continue
        primary = key[3] * 2
        expected = {primary, primary + 1}
        expected.update(slot for slot in (expected_slots or set()) if slot % 4 // 2 == key[3])
        # Delayed samples must not lose a secondary just because the corresponding
        # current observation has already advanced a few frames.
        expected.update(o["physicalSlot"] for o in status.get("slots", [])
                        if isinstance(o, dict) and integer(o.get("physicalSlot"), 7)
                        and o.get("revision") == revision and o.get("generation") == key[2]
                        and o.get("insertionPoint") == insertion
                        and o["physicalSlot"] % 4 // 2 == key[3])
        if not expected.issubset(items):
            continue
        output.append([items[slot] for slot in sorted(items)])
    return output


def assess_samples(groups: list[list[dict]], transport: bool, shown: bool = True) -> dict:
    failures: list[str] = []
    weighted_score = 0.0
    total_weight = 0.0
    count_indices = (3, 9, 10, 11, 12, 13, 14, 15, 16, 17)
    for group in groups:
        capture_sequences = set()
        for item in group:
            s, v = item["source"], item["values"]
            profile = s.get("profile")
            if not profile_valid(profile) or s.get("effectiveMode") != "managed":
                failures.append("missing/invalid effective mode or colour profile")
                continue
            rect = s.get("rect")
            if (not isinstance(rect, list) or len(rect) != 4 or not all(integer(x, 16384) for x in rect)
                    or rect[2] == 0 or rect[3] == 0 or rect[0] + rect[2] > 16384 or rect[1] + rect[3] > 16384
                    or not integer(s.get("sourceFormat"), 0xffffffff) or s.get("sourceFormat") == 0
                    or not integer(s.get("outputFormat"), 0xffffffff) or s.get("outputFormat") == 0):
                failures.append("missing/invalid resource extent or format")
                continue
            if len(v) != 24 or not all(number(x) for x in v):
                failures.append("malformed measurement")
                continue
            area = rect[2] * rect[3]
            stride = max(1, (area + 4095) // 4096)
            expected_count = (area + stride - 1) // stride
            if (v[15] != expected_count or not 1 <= v[15] <= 4096 or any(v[i] < 0 or v[i] > v[15] or int(v[i]) != v[i] for i in count_indices)
                    or v[3] != v[15] or any(v[i] != 0 for i in (12, 13, 14, 16, 17))):
                failures.append("invalid counts, nonfinite pixels, invalid codec or incomplete samples")
            if v[19] != 1 or not 1 / 256 <= v[18] <= 256:
                failures.append("effective exposure invalid")
            if s.get("profile", {}).get("exposureSource") == "captured_hdr":
                stamp = s.get("exposure", {})
                if (s.get("exposureBinding") != "gpu_snapshot_queued" or v[23] != 1
                        or stamp.get("frame") != s["sourceWorldFrame"] or stamp.get("ambiguous") is not False
                        or not integer(stamp.get("epoch")) or stamp.get("epoch", 0) == 0
                        or not integer(stamp.get("sequence")) or stamp.get("sequence", 0) == 0):
                    failures.append("missing/stale/ambiguous engine exposure")
                capture_sequences.add((stamp.get("epoch"), stamp.get("sequence")))
                if (v[20] <= 0 or v[21] <= 0 or not 1 / 256 <= v[22] <= 256
                        or not math.isclose(v[22], v[21] / v[20], rel_tol=1e-5)
                        or not math.isclose(v[18], v[22] * profile["exposureMultiplier"], rel_tol=1e-5)):
                    failures.append("inconsistent captured/effective exposure values")
            elif not math.isclose(v[18], profile["exposureMultiplier"], rel_tol=1e-5):
                failures.append("inconsistent manual exposure value")
            if s.get("modelEditShown") is not shown or s.get("transportBypass") is not transport:
                failures.append("wrong A/B or transport state")
            if v[7] < 0 or v[8] < 0 or v[7] > v[8] + 1e-6:
                failures.append("invalid error metrics")
            tolerance = 1e-5 * max(1.0, *(abs(x) for x in v[:3]))
            if (transport or not shown) and v[8] > tolerance:
                failures.append("transport or hidden-edit baseline error")
            scale = max(1e-4, sum(abs(x) for x in v[:3]) / 3)
            weighted_score += v[7] / scale * max(0, v[3])
            total_weight += max(0, v[3])
        if len(capture_sequences) > 1:
            failures.append("different captured exposure identities within a stereo transaction")
    if not groups:
        failures.append("no coherent fresh stereo samples")
    return {"valid": not failures, "reasons": sorted(set(failures)),
            "sourceDriftScore": weighted_score / total_weight if total_weight else None,
            "domainVerified": False,
            "meaning": "Sample-count-weighted source-relative RGB change, not physical lighting accuracy or domain proof"}


def profile_valid(profile: Any) -> bool:
    if not isinstance(profile, dict) or any(key not in profile for key in PROFILE_KEYS):
        return False
    if profile["domain"] not in ("unknown", "linear", "srgb") or profile["transform"] not in ("identity", "linear_to_srgb", "reversible_proxy"):
        return False
    if profile["exposureSource"] not in ("manual", "captured_hdr") or not number(profile["exposureMultiplier"]) or not 1 / 256 <= profile["exposureMultiplier"] <= 256:
        return False
    return (profile["exposureSource"] == "manual" and profile["exposureMultiplier"] == 1) if profile["transform"] == "identity" else profile["domain"] == "linear"


def frame_floor(status: dict, insertion: int) -> int | None:
    # uint32 numeric max is not the newest frame across a wrap; ignore unknowns.
    frames = [o.get("sourceWorldFrame") for o in status.get("slots", [])
              if isinstance(o, dict) and o.get("insertionPoint") == insertion
              and integer(o.get("sourceWorldFrame"), 0xfffffffe)]
    newest = None
    for frame in frames:
        if newest is None or frame_advanced(frame, newest):
            newest = frame
    return newest


def candidates(include_captured: bool) -> list[dict]:
    choices = [dict(name="identity_native", domain="unknown", transform="identity", exposureSource="manual"),
               dict(name="linear_srgb", domain="linear", transform="linear_to_srgb", exposureSource="manual"),
               dict(name="linear_proxy", domain="linear", transform="reversible_proxy", exposureSource="manual")]
    if include_captured:
        choices += [dict(name="captured_" + transform, domain="linear", transform=transform, exposureSource="captured_hdr")
                    for transform in ("linear_to_srgb", "reversible_proxy")]
    return choices


def atomic_json(path: Path, data: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False), encoding="utf-8")
    temporary.replace(path)


class Controller:
    def __init__(self, args: argparse.Namespace, directory: Path):
        self.args, self.directory, self.index = args, directory, 0
        self.expected_identity: dict | None = None
        self.script = args.automation_root / "tools/devbench-control/Invoke-DevBenchControl.ps1"
        self.capture_script = args.automation_root / "tools/capture-interaction-control/Invoke-CaptureInteraction.ps1"
        if not self.script.is_file():
            raise AssessmentError("Missing automation/dev DevBench controller")

    def invoke(self, command: list[str], label: str, timeout: float, *, deadline: float | None = None) -> dict:
        deadline = min(deadline, time.monotonic() + timeout) if deadline is not None else time.monotonic() + timeout
        self.index += 1
        stem = self.directory / f"{self.index:03d}-{label}"
        atomic_json(stem.with_suffix(".request.json"), {"command": command, "utc": datetime.now(timezone.utc).isoformat()})
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssessmentError("Operation deadline expired before controller dispatch")
        # No shell and no unbounded RPC retry. A timeout may have mutated state.
        try:
            result = subprocess.run(command, text=True, encoding="utf-8-sig", capture_output=True, timeout=remaining, check=False)
        except subprocess.TimeoutExpired as error:
            atomic_json(stem.with_suffix(".timeout.json"), {"error": str(error), "indeterminate": True})
            raise AssessmentError("Timed-out controller; inspect its mutation journal") from error
        stem.with_suffix(".stdout.txt").write_text(result.stdout, encoding="utf-8")
        stem.with_suffix(".stderr.txt").write_text(result.stderr, encoding="utf-8")
        try:
            value = json.loads(result.stdout)
        except (ValueError, TypeError) as error:
            raise AssessmentError("Controller returned non-JSON output") from error
        atomic_json(stem.with_suffix(".json"), value)
        if time.monotonic() > deadline:
            raise AssessmentError("Controller response arrived after its operation deadline")
        if not isinstance(value, dict):
            raise AssessmentError("Controller returned a non-object receipt")
        if result.returncode != 0:
            raise AssessmentError(f"Controller process returned {result.returncode}")
        return value

    def identity_arguments(self) -> list[str]:
        command: list[str] = []
        for option, field in (("-ArtifactPath", "artifact_path"), ("-WorkspaceManifestPath", "workspace_manifest"),
                              ("-ExpectedBuildId", "expected_build_id"), ("-ExpectedArtifactSha256", "expected_artifact_sha256")):
            value = getattr(self.args, field, None)
            if value is not None:
                command += [option, str(value)]
        if self.expected_identity is not None:
            command += ["-ExpectedRuntimeIdentityJson", json.dumps(self.expected_identity, separators=(",", ":"))]
        return command

    def bind_identity(self, receipt: dict) -> None:
        identity = receipt.get("runtimeIdentity")
        if not isinstance(identity, dict) or not identity:
            raise IdentityUncertain("Controller supplied no runtime identity for a qualified live run")
        if self.expected_identity is None:
            self.expected_identity = copy.deepcopy(identity)
        # Subsequent invocations carry ExpectedRuntimeIdentityJson; the controller,
        # not ad hoc equality of timestamp-bearing JSON, verifies stable identity.

    def call(self, payload: dict, label: str, *, deadline: float | None = None) -> dict:
        deadline = deadline if deadline is not None else time.monotonic() + 45
        remaining = deadline - time.monotonic()
        if remaining < 0.5:
            raise AssessmentError("Insufficient budget for a bounded controller call")
        # Both the controller's integer budget and Python's hard envelope shrink.
        # Keep up to two seconds inside that envelope for controller cleanup.
        seconds = max(1, min(600, math.floor(remaining - 2)))
        command = [self.args.pwsh, "-NoProfile", "-File", str(self.script), "call", "-Tool", TOOL,
                   "-ArgumentsJson", json.dumps(payload, separators=(",", ":")), "-RuntimePath", str(self.args.runtime),
                   "-EvidenceDirectory", str(self.directory / "controller"), "-EvidenceLabel", label,
                   "-RequireSuccess", "-RequirePerformanceNeutral", "-MaxTransientRetries", "0", "-NoExit", "-Compact",
                   "-TimeoutSeconds", str(seconds), "-RequestTimeoutSeconds", str(min(15, seconds))]
        command += self.identity_arguments()
        receipt = self.invoke(command, label, remaining, deadline=deadline)
        payload = unwrap(receipt)
        self.bind_identity(receipt)
        return payload

    def wait_scene(self) -> None:
        command = [self.args.pwsh, "-NoProfile", "-File", str(self.script), "wait", "-Condition", "upscalingStable",
                   "-ExpectedCell", self.args.expected_cell, "-RuntimePath", str(self.args.runtime),
                   "-TimeoutSeconds", "30", "-RequireSuccess", "-NoExit", "-Compact",
                   "-MaxTransientRetries", "0", "-EvidenceDirectory", str(self.directory / "controller")]
        command += self.identity_arguments()
        result = self.invoke(command, "scene-barrier", 40)
        semantic = result.get("semantic") or {}
        if (result.get("transportOk") is not True or result.get("ok") is not True
                or semantic.get("known") is not True or semantic.get("ok") is not True):
            raise AssessmentError("Prepared scene/upscaling barrier failed; no scene mutation attempted")
        self.bind_identity(result)

    def episode(self, label: str, revision: int | None = None) -> dict:
        if not self.capture_script.is_file():
            raise AssessmentError("Missing automation/dev capture-interaction controller")
        session = self.directory / (label + "-capture")
        result: dict[str, Any] = {"sessionDirectory": str(session), "sameFrameAsNumericSample": False}
        started = False
        primary: Exception | None = None
        try:
            for action in ("start", "observe"):
                status = self.call({"action": "status"}, label + "-" + action + "-guard")
                if revision is not None and status.get("revision") != revision:
                    raise MutationUncertain("Configuration ownership changed before capture")
                command = [self.args.pwsh, "-NoProfile", "-File", str(self.capture_script), action,
                           "-SessionDirectory", str(session), "-RuntimePath", str(self.args.runtime), "-NoExit", "-Compact"]
                if action == "start":
                    command += ["-VisualMode", "sequence", "-PreferredView", "side_by_side", "-MaximumFrames", "8"]
                try:
                    receipt = self.invoke(command, label + "-" + action, 60)
                except (Exception, KeyboardInterrupt) as error:
                    if action == "start":
                        result["recoveryRequired"] = True
                        raise MutationUncertain("Capture start outcome unknown; do not retry or blindly stop its owner") from error
                    raise
                result[action] = receipt
                if receipt.get("ok") is not True:
                    if action == "start":
                        result["recoveryRequired"] = True
                        raise MutationUncertain("Capture start not acknowledged; inspect retained session before cleanup")
                    raise AssessmentError("Capture episode failed; retain its state/receipts")
                if action == "start":
                    started = True
        except Exception as error:
            primary = error
            result["error"] = str(error)
        finally:
            if started and not isinstance(primary, MutationUncertain):
                try:
                    # The existing capture session owns cleanup. Never blindly stop
                    # after a lost start, changed configuration or failed identity guard.
                    status = self.call({"action": "status"}, label + "-stop-guard")
                    if revision is not None and status.get("revision") != revision:
                        raise MutationUncertain("Configuration ownership changed before capture cleanup")
                    command = [self.args.pwsh, "-NoProfile", "-File", str(self.capture_script), "stop",
                               "-SessionDirectory", str(session), "-RuntimePath", str(self.args.runtime), "-NoExit", "-Compact"]
                    result["stop"] = self.invoke(command, label + "-stop", 60)
                    if result["stop"].get("ok") is not True:
                        raise AssessmentError("Owned capture cleanup failed")
                except Exception as error:
                    result["cleanupError"] = str(error)
                    result["recoveryRequired"] = True
                    if primary is None:
                        primary = MutationUncertain("Capture cleanup unproven; inspect retained session")
            elif started:
                result["recoveryRequired"] = True
            atomic_json(session.parent / (label + "-capture-result.json"), result)
        if primary is not None:
            if result.get("recoveryRequired") and not isinstance(primary, MutationUncertain):
                raise MutationUncertain(str(primary) + "; capture cleanup remains unproven") from primary
            raise primary
        return result


def collect(controller: Controller, revision: int, insertion: int, after_frame: int | None,
            args: argparse.Namespace, *, shown: bool = True, transport: bool | None = None, expected_profile: dict | None = None) -> list[list[dict]]:
    deadline = time.monotonic() + args.timeout
    samples: dict[tuple, list[dict]] = {}
    observed_status = False
    selected_generation_route = None
    expected_slots = set(getattr(args, "expected_physical_slots", None) or [])
    while time.monotonic() < deadline:
        # Leave tiny remainders unused; the outer hard deadline also caps subsecond calls.
        if deadline - time.monotonic() < 0.5:
            break
        status = controller.call({"action": "status"}, "sample", deadline=deadline)
        observed_status = True
        if type(status.get("revision")) is not int or status.get("revision") != revision:
            raise MutationUncertain("UI/another agent changed the configuration; automatic restore would overwrite it")
        if not isinstance(status.get("slots"), list) or not isinstance(status.get("measurements"), list):
            raise AssessmentError("Malformed colour telemetry arrays")
        if time.monotonic() > deadline:
            raise AssessmentError("Controller overran the sample deadline; no late evidence accepted")
        if after_frame is None:
            after_frame = frame_floor(status, insertion)
            if after_frame is None:
                time.sleep(min(0.25, max(0, deadline - time.monotonic())))
                continue
        for group in fresh_groups(status, revision, insertion, after_frame, args.warmup_frames, expected_slots):
            if any(item["source"].get("modelEditShown") is not shown or
                   (transport is not None and item["source"].get("transportBypass") is not transport) for item in group):
                continue
            if expected_profile is not None and any(
                    {key: item["source"].get("profile", {}).get(key) for key in PROFILE_KEYS} != expected_profile
                    or item["source"].get("effectiveMode") != "managed" for item in group):
                raise AssessmentError("Processed mode/profile does not match the owned candidate")
            source = group[0]["source"]
            pair_key = (source["generation"], source["physicalSlot"] % 4 // 2)
            if selected_generation_route is None:
                selected_generation_route = pair_key
            elif pair_key[1] != selected_generation_route[1]:
                continue  # Never count two routes as independent sample frames.
            elif pair_key[0] != selected_generation_route[0]:
                raise CandidateUnavailable("Resource generation changed during the sample window")
            samples[(source["sourceWorldFrame"], source["generation"], source["physicalSlot"] % 4 // 2)] = group
        if len(samples) >= args.sample_frames:
            return list(samples.values())[-args.sample_frames:]
        time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    if not observed_status:
        raise AssessmentError("No qualified status call fit inside the sample deadline")
    raise CandidateUnavailable("Healthy unchanged session, but no complete fresh samples before the deadline")


def run_live(args: argparse.Namespace, directory: Path) -> dict:
    controller = Controller(args, directory)
    report: dict[str, Any] = {"ok": False, "candidates": [], "domainVerified": False, "restored": False,
                              "outputCommitVerified": False,
                              "measurementScope": "private reconstruction buffers; final presentation requires live evidence",
                              "regionCompleteness": "explicit_fixture_slots" if getattr(args, "expected_physical_slots", None) else "retained_observations_only",
                              "automationReferenceReviewed": "a4ab2cf6ea6c853926918e5626b8d17f15cf5d79", "insertion": args.insertion}
    original = None
    owned_revision = None
    current = None
    try:
        controller.wait_scene()
        assets = controller.call({"action": "assets"}, "assets")
        if not assets.get("allPresent"):
            raise AssessmentError("Runtime colour shader inventory is incomplete")
        current = controller.call({"action": "status"}, "initial")
        if current.get("apiVersion", 0) < 2:
            raise AssessmentError("The running plugin does not expose NR colour API v2")
        original = editable(current)
        atomic_json(directory / "original-configuration.json", {"revision": current["revision"], **original})
        owned_revision = current["revision"]
        insertion = PROFILE_NAMES.index(args.insertion)

        def apply(desired: dict, label: str) -> dict:
            nonlocal owned_revision, current
            expected = owned_revision + (desired != editable(current))
            try:
                updated = controller.call({"action": "configure", "expectedRevision": owned_revision, **desired}, label)
            except (Exception, KeyboardInterrupt) as error:
                owned_revision = None
                raise MutationUncertain("Mutation outcome/ownership unproven; no retry or blind restore") from error
            if updated.get("revision") != expected or editable(updated) != desired:
                owned_revision = None
                raise MutationUncertain("Configuration changed during acknowledgement")
            owned_revision, current = expected, updated
            return updated

        for candidate in candidates(args.include_captured):
            controller.wait_scene()
            latest = controller.call({"action": "status"}, "candidate-owner")
            if latest.get("revision") != owned_revision or editable(latest) != editable(current):
                raise MutationUncertain("Configuration changed between candidates")
            current = latest
            entry: dict[str, Any] = {"candidate": candidate, "domainVerified": False}
            report["candidates"].append(entry)
            desired = copy.deepcopy(original)
            desired["settings"].update(enabled=True, mode="managed")
            desired["experiments"].update(diagnostics=True, applyModelEdit=True, captureEngineExposure=True)
            desired["experiments"][args.insertion] = {k: candidate[k] for k in ("domain", "transform", "exposureSource")}
            desired["experiments"][args.insertion]["exposureMultiplier"] = 1.0
            for phase in ("transport", "neural"):
                desired["experiments"]["transportBypass"] = phase == "transport"
                latest = controller.call({"action": "status"}, "phase-owner")
                if latest.get("revision") != owned_revision or editable(latest) != editable(current):
                    raise MutationUncertain("Configuration changed before the next phase")
                current = latest
                floor = frame_floor(current, insertion)
                apply(desired, candidate["name"] + "-" + phase)
                try:
                    groups = collect(controller, owned_revision, insertion, floor, args, transport=phase == "transport", expected_profile=desired["experiments"][args.insertion])
                except CandidateUnavailable as error:
                    entry[phase] = {"revision": owned_revision, "verdict": {"valid": False, "reasons": [str(error)]}}
                    entry["classification"] = "unavailable_no_samples"
                    break
                verdict = assess_samples(groups, phase == "transport")
                entry[phase] = {"revision": owned_revision, "verdict": verdict, "samples": groups}
                atomic_json(directory / "report.json", report)
                if not verdict["valid"]:
                    entry["classification"] = "rejected_or_unavailable"
                    break
            else:
                entry["classification"] = "candidate_for_visual_review"
                if args.capture_episodes:
                    # Paired visual A/B with unchanged model input. Camera is not
                    # frozen by this script; preserve both frame manifests.
                    entry["editShownCapture"] = controller.episode(candidate["name"] + "-shown", owned_revision)
                    desired["experiments"]["applyModelEdit"] = False
                    apply(desired, candidate["name"] + "-hide-edit")
                    # Wait for actual application of the A/B state, not just an API ack.
                    floor = frame_floor(current, insertion)
                    baseline_groups = collect(controller, owned_revision, insertion, floor, args, shown=False, transport=False, expected_profile=desired["experiments"][args.insertion])
                    baseline_verdict = assess_samples(baseline_groups, False, shown=False)
                    entry["baselineVerdict"] = baseline_verdict
                    if not baseline_verdict["valid"]:
                        raise AssessmentError("Hidden-edit A/B did not reproduce baseline")
                    entry["baselineCapture"] = controller.episode(candidate["name"] + "-baseline", owned_revision)
            atomic_json(directory / "report.json", report)
        valid = [e for e in report["candidates"] if e.get("classification") == "candidate_for_visual_review"]
        report["sourceFidelityRanking"] = [e["candidate"]["name"] for e in sorted(valid, key=lambda e: e["neural"]["verdict"]["sourceDriftScore"])]
        report["ok"] = bool(valid)
        report["conclusion"] = "Candidates ranked for source fidelity; image/producer evidence must adjudicate colour domain. No automatic production profile was selected."
    except Exception as error:
        if isinstance(error, MutationUncertain):
            owned_revision = None
        report["error"] = str(error)
    finally:
        if original is not None and owned_revision is not None:
            try:
                latest = controller.call({"action": "status"}, "before-restore")
                if latest.get("revision") != owned_revision:
                    raise AssessmentError("Concurrent change detected: preserve the user's newer configuration")
                restored = controller.call({"action": "configure", "expectedRevision": owned_revision, **original}, "restore")
                expected = owned_revision + (editable(latest) != original)
                report["restored"] = restored.get("revision") == expected and editable(restored) == original
                if not report["restored"]:
                    report["restoreError"] = "Restore did not acknowledge the original configuration"
            except Exception as error:
                report["restoreError"] = str(error)
        elif original is not None:
            report["restoreError"] = "Ownership unproven after a mutation failure; original-configuration.json is retained for explicit recovery"
        if not report["restored"] and original is not None:
            report["ok"] = False
        atomic_json(directory / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--automation-root", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--expected-cell")
    parser.add_argument("--insertion", choices=PROFILE_NAMES, default="upscaled_center")
    parser.add_argument("--deployed-data", type=Path)
    parser.add_argument("--pwsh", default="pwsh")
    parser.add_argument("--artifact-path", type=Path)
    parser.add_argument("--workspace-manifest", type=Path)
    parser.add_argument("--expected-build-id")
    parser.add_argument("--expected-artifact-sha256")
    parser.add_argument("--expected-physical-slots", type=int, nargs="+", choices=range(8),
                        help="Authoritative physical slots for a fixed multi-ROI fixture (optional)")
    parser.add_argument("--sample-frames", type=int, default=3)
    parser.add_argument("--warmup-frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--include-captured", action="store_true")
    parser.add_argument("--capture-episodes", action="store_true")
    parser.add_argument("--confirm-static-scene", action="store_true")
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    if args.expected_physical_slots and len({slot % 4 // 2 for slot in args.expected_physical_slots}) != 1:
        parser.error("expected-physical-slots must describe a single main or submit route")
    if not math.isfinite(args.timeout):
        parser.error("timeout must be finite")
    if not 1 <= args.sample_frames <= 30 or not 0 <= args.warmup_frames <= 600 or not 1 <= args.timeout <= 300:
        parser.error("sample/warmup/timeout outside bounded test limits")
    if not args.live:
        print(json.dumps({"mode": "plan_only", "candidates": candidates(args.include_captured),
                          "insertion": args.insertion, "worldMutations": False, "domainVerified": False,
                          "requires": "Prepared static scene, identity-bound automation/dev runtime, new evidence directory and explicit --live"}, indent=2))
        return 0
    if not all((args.automation_root, args.runtime, args.evidence_dir, args.expected_cell, args.confirm_static_scene)):
        parser.error("--live requires automation-root, runtime, new evidence-dir, expected-cell and confirm-static-scene")
    args.automation_root, args.runtime = args.automation_root.resolve(), args.runtime.resolve()
    args.evidence_dir = args.evidence_dir.resolve()
    # Never reuse a prior run's evidence or overwrite a shared profile directory.
    args.evidence_dir.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    assets = verify(root, args.deployed_data.resolve() if args.deployed_data else None)
    atomic_json(args.evidence_dir / "asset-check.json", assets)
    if not assets["ok"]:
        print(json.dumps(assets, indent=2)); return 2
    try:
        result = run_live(args, args.evidence_dir)
    except Exception as error:
        result = {"ok": False, "error": str(error), "domainVerified": False}
        atomic_json(args.evidence_dir / "report.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
