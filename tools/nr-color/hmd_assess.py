#!/usr/bin/env python3
"""Preserve and assess attributed HMD PNG sequences; never controls a runtime.

Commands are append-only: plan freezes settings/regions/order, analyse imports
committed evidence and prepares blinded reviews, finalise seals provider results
before unblinding. Images are never corrected, registered, or independently scaled.
See hmd_workflow.md for the input contracts and maintained review provider.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import copy
import csv
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import random
import shutil
import sys
from typing import Any

from assess import AssessmentError, effective_reconstruction_settings, lighting_evidence
from transaction_evidence import TransactionEvidenceError, join_execution_evidence

VERSION = "csx-nr-hmd-assessment-v1"
EYES = ("left", "right")
CLASSES = {"skin", "material", "shadow", "highlight", "background"}
OBJECTIVES = ("colourFidelity", "usefulNeuralDetail", "temporalStability", "stereoConsistency")
MODE = {"nr_off": "legacy_raw", "raw": "legacy_raw", "managed_identity": "managed",
        "conversion": "managed", "preserve_source": "preserve_source",
        "neural_lighting": "neural_lighting"}
HERE = Path(__file__).resolve().parent
SAMPLING_LIMITATION = (
    "Temporal conclusions cover the sampled acquisition cadence only. Sparse or irregular captures can alias or miss "
    "HMD frame-rate flicker; neither reviewer agreement nor low sampled variance proves frame-rate stability."
)


class EvidenceError(ValueError):
    """A retained input cannot substantiate the claimed acquisition."""


def require(condition: Any, reason: str) -> None:
    if not condition:
        raise EvidenceError(reason)


def read_json(path: Path) -> Any:
    with path.open(encoding="utf-8-sig") as stream:
        return json.load(stream)


def canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def value_digest(value: Any) -> str:
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def save_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def resolve(base: Path, value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def preserve(source: Path, destination: Path) -> dict:
    require(source.is_file(), f"missing artifact: {source}")
    require(not destination.exists(), f"refusing to overwrite {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    require(source.stat().st_size == destination.stat().st_size and digest(source) == digest(destination),
            f"preservation verification failed: {source}")
    return {"path": str(destination.resolve()), "bytes": destination.stat().st_size, "sha256": digest(destination)}


def libraries() -> tuple:
    try:
        import numpy as np
        from PIL import Image, ImageDraw
        import jsonschema
    except ImportError as error:
        raise EvidenceError("Install the fixed tools/nr-color/hmd_requirements.txt dependencies") from error
    return np, Image, ImageDraw, jsonschema


def check_regions(policy: dict) -> None:
    require(policy.get("schemaVersion") == 1, "unsupported region policy")
    require(bool(policy.get("sceneFingerprint")), "region sceneFingerprint required")
    require(type(policy.get("motionGradientMismatchLimit")) in (int, float)
            and 0 < policy["motionGradientMismatchLimit"] < 1, "fixed motion threshold required")
    region_ids = []
    for eye in EYES:
        entry = policy["eyes"][eye]
        size = entry["nativeSize"]
        require(len(size) == 2 and all(type(v) is int and v > 1 for v in size), "invalid nativeSize")
        names = set()
        classes = set()
        for region in entry["regions"]:
            name = region["id"]
            require(isinstance(name, str) and name.isascii() and name.replace("_", "").isalnum(), "unsafe region id")
            require(name not in names, "duplicate region id")
            names.add(name)
            require(region["class"] in CLASSES and region.get("description"), "region semantics missing")
            classes.add(region["class"])
            x, y, w, h = region["rect"]
            require(all(type(v) is int for v in (x, y, w, h)) and min(x, y) >= 0 and min(w, h) >= 3
                    and x + w <= size[0] and y + h <= size[1], "region outside native eye extent")
        absent = entry.get("unavailable", {})
        require(classes | set(absent) == CLASSES and all(absent.values()), "account for every region class")
        region_ids.append(names)
    require(region_ids[0] == region_ids[1], "both eyes need corresponding region ids (separate rectangles)")


def check_candidate(candidate: dict) -> None:
    kind = candidate["condition"]
    require(kind in MODE, "unknown condition")
    require(isinstance(candidate.get("settings"), dict) and candidate["settings"], "candidate settings required")
    if kind == "conversion":
        require(candidate.get("rationale") and candidate.get("domainHypothesis")
                and candidate.get("producerEvidence"), "conversion requires producer evidence and explicit hypothesis")


def make_plan(spec: dict, policy: dict, seed: int) -> dict:
    check_regions(policy)
    require(spec.get("sceneFingerprint") == policy["sceneFingerprint"], "scene and regions disagree")
    require(spec.get("fixedScene") and spec.get("fixedSettings"), "fixed scene and graphics settings required")
    interval_ms = spec.get("intervalMs", 500)
    require(type(interval_ms) is int and 50 <= interval_ms <= 60000, "intervalMs must use controller-supported 50..60000 ms")
    candidates = copy.deepcopy(spec["candidates"])
    for candidate in candidates:
        check_candidate(candidate)
    kinds = [candidate["condition"] for candidate in candidates]
    require(all(kinds.count(kind) == 1 for kind in ("nr_off", "raw", "managed_identity", "preserve_source")),
            "retain one distinct NR-off, Raw, Managed identity and Preserve Source condition")
    require(kinds.count("neural_lighting") <= 1, "retain at most one Neural Lighting condition")
    generator = random.Random(seed)
    identities = generator.sample(range(0x100000, 0xFFFFFF), len(candidates) * 2)
    mapping = {}
    candidate_order = []
    for index, candidate in enumerate(candidates):
        candidate_id = f"C{identities[index]:06x}"
        candidate_order.append(candidate_id)
        mapping[candidate_id] = {**candidate, "applyModelEdit": True}
        if candidate["condition"] != "nr_off":
            hidden_id = f"C{identities[index + len(candidates)]:06x}"
            mapping[hidden_id] = {**copy.deepcopy(candidate), "condition": "display_only_source",
                                  "sourceCondition": candidate["condition"], "shownCandidateId": candidate_id,
                                  "applyModelEdit": False}
            mapping[candidate_id]["hiddenCandidateId"] = hidden_id
    baseline = next(key for key, value in mapping.items() if value["condition"] == "nr_off")
    shown = [key for key, value in mapping.items() if value["condition"] not in ("nr_off", "display_only_source")]
    schedule = []

    def add(candidate_id: str, repetition: int, direction: str, role: str, block: str) -> None:
        schedule.append({"scheduleOrdinal": len(schedule) + 1, "candidateId": candidate_id,
                         "repetition": repetition, "direction": direction, "role": role, "block": block,
                         "minimumPairs": 12, "intervalMs": interval_ms})

    for repetition in range(1, 4):
        add(baseline, repetition, "initial", "baseline", "unchanged_baseline")
    for repetition in range(1, 4):
        order = generator.sample(shown, len(shown))
        for direction, ordered in (("forward", order), ("reverse", list(reversed(order)))):
            add(baseline, repetition, direction, "baseline", "flanking")
            for candidate_id in ordered:
                hidden_id = mapping[candidate_id]["hiddenCandidateId"]
                phases = (candidate_id, hidden_id, candidate_id) if direction == "forward" else (hidden_id, candidate_id, hidden_id)
                for phase in phases:
                    add(phase, repetition, direction, "candidate", candidate_id)
            add(baseline, repetition, direction, "baseline", "flanking")
    return {"schema": VERSION, "createdUtc": utc(), "seed": seed, "intervalMs": interval_ms, "regionPolicy": policy,
            "regionPolicySha256": value_digest(policy), "sceneFingerprint": spec["sceneFingerprint"],
            "fixedScene": spec["fixedScene"], "fixedSettings": spec["fixedSettings"],
            "candidateMapping": mapping, "candidateOrder": candidate_order, "schedule": schedule,
            "untestedConditions": spec.get("untestedConditions", []),
            "performanceCampaign": False}


def recorded_file(descriptor: dict, base: Path) -> Path:
    """Check a preserved recording/binary receipt without inventing service commit state."""
    require(not descriptor.get("integrityError"), "artifact integrity was not established by producer")
    path = resolve(base, descriptor["path"])
    require(path.is_file(), "committed artifact is missing")
    require(type(descriptor.get("bytes")) is int and path.stat().st_size == descriptor["bytes"], "artifact byte length mismatch")
    require(isinstance(descriptor.get("sha256"), str) and digest(path) == descriptor["sha256"].lower(), "artifact SHA-256 mismatch")
    return path


def committed(descriptor: dict, base: Path) -> Path:
    require(descriptor.get("committed") is True, "artifact is not committed")
    return recorded_file(descriptor, base)


def recording_scene(recording: dict, plan: dict) -> dict:
    """Validate preserved recorder scene metadata and transition coverage."""
    meta = recording.get("meta", {})
    result = {"qualified": False, "observed": meta, "reason": "", "gameHourDelta": None}
    try:
        scene = plan["fixedScene"]
        for key, value in scene["recordingScene"].items():
            require(meta.get(key) == value, "recording scene changed: " + key)
        hour = meta.get("gameHour")
        require(type(hour) in (int, float) and math.isfinite(hour), "recording gameHour missing")
        delta = (hour - scene["gameHour"] + 12) % 24 - 12
        result["gameHourDelta"] = delta
        require(abs(delta) <= scene["gameHourTolerance"], "scene lighting time drift exceeds frozen tolerance")
        activity = recording.get("activityEvents")
        require(isinstance(activity, list), "recording activity events missing")
        require(not any(item.get("kind") in ("cell", "lifecycle") for item in activity),
                "cell or lifecycle transition during image sequence")
        result["qualified"] = True
    except (EvidenceError, KeyError, TypeError) as error:
        result["reason"] = str(error)
    return result


def preserve_devbench_identity(identity: dict, base: Path, output: Path) -> dict:
    """A version/process match is not a loaded-module hash measurement."""
    require(identity.get("runtimeMatched") is True and type(identity.get("observedPid")) is int
            and identity["observedPid"] > 0, "DevBench version/process match unavailable")
    build = identity["buildIdentity"]
    require(bool(build.get("version")) and build["version"] == identity.get("observedVersion")
            and bool(build.get("sourceCommit")), "DevBench observed version/build provenance missing")
    path = recorded_file(identity, base)
    receipt = recorded_file(identity["identityReceipt"], base)
    preserve(path, output / "private" / "host" / "devbench.dll")
    preserve(receipt, output / "private" / "host" / "identity-receipt.json")
    return {"runtimeVersionAndProcessMatched": True,
            "physicalModulePathVerified": identity.get("physicalModulePathVerified") is True,
            "physicalSnapshotSha256": identity["sha256"],
            "limitation": "Recorded version/process matching does not prove the loaded host DLL hash; exact enabled-provider verification is separate."}


def capture_descriptor(value: dict) -> dict:
    if "sequence" in value:
        return value["sequence"]["capture"]
    return value.get("capture", value)


def check_capture_descriptor(value: dict) -> None:
    capture = capture_descriptor(value)
    require(capture.get("source") == {"kind": "hmd_submission", "fallback": "reject"}, "HMD source and reject fallback must be explicit")
    outputs = capture.get("outputs", [])
    require(all(output.get("view") in ("left_eye", "right_eye", "side_by_side") for output in outputs),
            "colour campaign permits native eye outputs and optional stereo overview only")
    eyes = [item for item in outputs if item.get("view") in ("left_eye", "right_eye")]
    require(len(eyes) == 2 and {item["view"] for item in eyes} == {"left_eye", "right_eye"}, "native output eye pair missing")
    for output in eyes:
        encoding = output.get("encoding", {})
        require(encoding.get("format") == "png" and encoding.get("colourContract") == "sdr_srgb", "identical PNG/sdr_srgb output required")
        require(all(output.get(key) is None for key in ("width", "height", "crop", "resize")), "capture crop or resize is not native")


def resolved_exposure(frozen: dict, diagnostics: dict | None = None) -> dict:
    """Join only an exact producer stamp; never substitute a newer readback."""
    require(isinstance(frozen, dict) and (diagnostics is None or isinstance(diagnostics, dict)), "invalid exposure evidence object")
    stamp = {name: frozen.get(name) for name in ("frame", "epoch", "sequence")}
    require(all(type(value) is int and value > 0 for value in stamp.values()), "exposure producer stamp unavailable")
    matches = [item for item in (diagnostics or {}).get("exposures", []) if item.get("key") == stamp]
    if not matches and (frozen.get("valid") is True or frozen.get("engineRatioValid") is True):
        require(frozen.get("ambiguous") is not True, "frozen exposure is ambiguous")
        return frozen
    require(len(matches) == 1, "exact exposure companion missing or duplicated")
    match = matches[0]
    require(match.get("available") is True, "exposure companion unavailable: " + str(match.get("reason", "unspecified")))
    evidence = match.get("evidence", {})
    require(isinstance(evidence, dict), "exposure companion lacks evidence object")
    require(all(evidence.get(key) == value for key, value in stamp.items()), "exposure companion stamp mismatch")
    require(evidence.get("readbackComplete") is True and evidence.get("engineRatioValid") is True
            and evidence.get("ambiguous") is False, "exposure companion is pending, invalid or ambiguous")
    return evidence


def check_nr(evidence: dict, expected: dict, candidate: dict, acquisition: dict, *,
             capture_diagnostics: dict | None = None, require_exposure: bool = True) -> None:
    try:
        execution = join_execution_evidence(evidence, capture_diagnostics)
    except TransactionEvidenceError as error:
        raise EvidenceError("NR execution attribution: " + str(error)) from error
    require(execution is None or execution["available"],
            "NR execution attribution unavailable: " + (execution["reason"] if execution else "missing record"))
    require(evidence.get("schemaVersion") == 1 and evidence.get("available") is True,
            "NR attribution unavailable: " + str(evidence.get("reason", "missing record")))
    require(bool(evidence.get("transactionId")), "missing NR transaction")
    require(bool(expected.get("configurationFingerprint"))
            and evidence.get("configurationFingerprint") == expected["configurationFingerprint"], "configuration fingerprint mismatch")
    require(evidence.get("configuration") == expected.get("configuration") and bool(expected.get("configuration")),
            "applied configuration differs from frozen capture state")
    kind = candidate.get("sourceCondition", candidate["condition"])
    nr_enabled = kind != "nr_off"
    configuration = evidence["configuration"]
    require(configuration["upscaling"].get("neuralRenderingEnabled") is nr_enabled, "configuration and NR master outcome disagree")
    colour = configuration["color"]
    declared = candidate.get("settings", {}).get("settings", {})
    if "lightingPreservation" in declared:
        try:
            lighting_evidence(declared, [colour["settings"]])
        except AssessmentError as error:
            raise EvidenceError("lighting preservation differs from declared candidate or is absent") from error
    observations = [region for eye in EYES for region in evidence[eye].get("physicalRegions", [])]
    if nr_enabled and kind in ("preserve_source", "neural_lighting", "managed_identity", "conversion") and "lightingPreservation" in colour["settings"]:
        require(all(evidence[eye].get("physicalRegions") for eye in EYES), "lighting preservation observations missing for an eye")
    observations.extend(item["source"] for batch in (capture_diagnostics or {}).get("measurementBatches", [])
                        for item in batch.get("measurements", []))
    try:
        lighting_evidence(effective_reconstruction_settings(colour["settings"]), observations)
    except AssessmentError as error:
        raise EvidenceError(str(error)) from error
    experiments = colour["experiments"]
    require(experiments.get("applyModelEdit") is candidate["applyModelEdit"]
            and experiments.get("transportBypass") is False and experiments.get("captureFrameEvidence") is True,
            "display/bypass/evidence configuration mismatch")
    insertion = expected["insertion"]
    require(insertion in ("upscaled_center", "final_ldr_pre_ui"), "unknown effective insertion")
    insertion_index = ("upscaled_center", "final_ldr_pre_ui").index(insertion)
    require(evidence.get("insertionPoint") == insertion_index
            and configuration["upscaling"].get("neuralRenderingInsertionPoint") == insertion_index,
            "applied route/profile insertion differs from the frozen test")
    profile = experiments[insertion]
    if kind == "managed_identity":
        require(profile.get("transform") == "identity" and profile.get("exposureSource") == "manual"
                and profile.get("exposureMultiplier") == 1, "Managed identity profile is not identity")
    if kind == "conversion":
        declared = candidate["settings"]["experiments"][insertion]
        require(all(profile.get(key) == declared.get(key) for key in ("domain", "transform", "exposureSource", "exposureMultiplier")),
                "conversion differs from preregistered profile")
        require(profile.get("domain") == "linear" and profile.get("transform") in ("linear_to_srgb", "reversible_proxy"),
                "conversion lacks its explicit domain/transform")
    for eye in EYES:
        record = evidence[eye]
        require(isinstance(record, dict), "invalid per-eye NR evidence")
        for name in ("frame", "sourceWorldFrame", "colorRevision", "inputEpoch"):
            require(type(record.get(name)) is int and record[name] >= 0, f"{eye} missing {name}")
        require(record["frame"] <= acquisition["engineFrame"], "future render frame")
        require(acquisition["engineFrame"] - record["frame"] <= expected.get("maximumRenderFrameAge", 0),
                "applied frame is older than the explicitly permitted capture age")
        require(record["colorRevision"] == expected["colorRevision"] and record["inputEpoch"] == expected["inputEpoch"],
                "stale applied colour revision/input epoch")
        require(record.get("effectiveMode") == MODE[kind], "effective colour mode mismatch")
        require(record.get("nrEnabled") is nr_enabled, "NR enablement mismatch")
        require(record.get("applyModelEdit") is candidate["applyModelEdit"], "display-only state mismatch")
        for name in ("inferenceAttempted", "inferenceSucceeded"):
            require(record.get(name) is nr_enabled, f"{eye} inference outcome mismatch: {name}")
        require(record.get("outputCommitted") is (nr_enabled and candidate["applyModelEdit"]), "neural output commit outcome mismatch")
        require(isinstance(record.get("disposition"), str) and record["disposition"], "missing final route disposition")
        require(isinstance(record.get("exposure"), dict), "exposure availability must be explicit")
        needs_exposure = profile.get("exposureSource") in ("captured_hdr", "captured_hdr_previous")
        if require_exposure and (needs_exposure or expected.get("requiresCapturedExposure")):
            exposure = record["exposure"]
            resolved = resolved_exposure(exposure, capture_diagnostics)
            require(exposure.get("sourceWorldFrame") == record["sourceWorldFrame"],
                    "captured exposure is not bound to this source transaction")
            age = 1 if profile.get("exposureSource") == "captured_hdr_previous" else 0
            expected_age = age if needs_exposure else expected.get("exposureAge", age)
            require(record["sourceWorldFrame"] - resolved["frame"] == expected_age
                    and exposure.get("age") == expected_age, "captured exposure age mismatch")
    for key in ("frame", "sourceWorldFrame", "colorRevision", "inputEpoch", "effectiveMode", "applyModelEdit", "disposition"):
        require(evidence["left"][key] == evidence["right"][key], f"mixed stereo NR {key}")


def check_camera(acquisition: dict, fixed_scene: dict) -> None:
    camera = acquisition.get("cameraEvidence", {})
    require(camera.get("available") is True and camera.get("provenance") == "engine_cached_unjittered_world_matrices",
            "exact acquisition camera matrices unavailable")
    require(camera.get("sourceWorldFrame") == acquisition["nrEvidence"]["left"]["sourceWorldFrame"],
            "camera belongs to another source frame")
    pinned = fixed_scene["cameraEvidence"]
    for kind in ("view", "projection", "positionAdjust"):
        tolerance = pinned[kind + "Tolerance"]
        require(type(tolerance) in (int, float) and math.isfinite(tolerance) and tolerance >= 0,
                "camera comparison tolerance must be fixed and finite")
        actual = camera[kind]
        reference = pinned[kind]
        require(len(actual) == len(reference) == 2, "camera eye pair missing")
        for actual_eye, reference_eye in zip(actual, reference):
            size = 4 if kind == "positionAdjust" else 16
            require(len(actual_eye) == len(reference_eye) == size, "camera matrix/vector extent changed")
            require(all(type(value) in (int, float) and math.isfinite(value) for value in actual_eye + reference_eye),
                    "camera matrix has invalid elements")
            require(max(abs(a - b) for a, b in zip(actual_eye, reference_eye)) <= tolerance,
                    "camera " + kind + " drift exceeds frozen tolerance")


def captured_diagnostics(child: dict) -> dict | None:
    """Use only immutable companions belonging to this acquisition."""
    actual = child.get("actual", {})
    result = actual.get("captureDiagnostics")
    if result is None:
        return None
    evidence = actual.get("acquisition", {}).get("nrEvidence", {})
    require(isinstance(result, dict) and result.get("schemaVersion") == 1
            and result.get("finalized") is True, "invalid capture-owned diagnostics")
    require(bool(evidence.get("transactionId"))
            and result.get("transactionId") == evidence["transactionId"],
            "capture-owned diagnostics transaction mismatch")
    require(isinstance(result.get("exposures"), list)
            and isinstance(result.get("measurementBatches"), list)
            and isinstance(result.get("measurementRequests"), list),
            "capture-owned diagnostic collections missing")
    return result


def check_pair(child: dict, base: Path, expected: dict, candidate: dict, policy: dict, *,
               capture_diagnostics: dict | None = None, sequence: dict | None = None) -> dict:
    _, Image, _, _ = libraries()
    require(child.get("state") in ("completed", "completed_with_warnings"), "capture did not complete")
    require(bool(child.get("requestId")), "missing child request identity")
    requested = child["requested"]
    if "source" not in requested and "capture" not in requested:
        require(sequence is not None and bool(sequence.get("requestId")), "sequence parent required for abbreviated child request")
        require(requested == {"action": "capture", "clientId": "sequence:" + sequence["requestId"],
                              "commandId": "frame:" + str(child["ordinal"]), "contractMajor": 1},
                "abbreviated child request does not match its sequence parent")
        check_capture_descriptor(sequence["requested"])
        check_capture_descriptor(sequence["effective"])
    else:
        check_capture_descriptor(requested)
    check_capture_descriptor(child["effective"])
    source = child["actual"]["source"]
    require(source.get("kind") == "hmd_submission" and source.get("fallback") == "reject"
            and not source.get("fallbackApplied"), "actual source differs from strict HMD request")
    require(not any(item.get("code") in ("source_fallback", "artifact_hash_failed") for item in child.get("warnings", [])), "capture warning invalidates provenance")
    acquisition = child["actual"]["acquisition"]
    require(acquisition.get("sourceKind") == "hmd_submission", "non-HMD acquisition")
    for key in ("engineFrame", "compositorCycle", "monotonicTimestampUs"):
        require(type(acquisition.get(key)) is int and acquisition[key] > 0, "missing actual acquisition " + key)
    companions = captured_diagnostics(child)
    check_nr(acquisition.get("nrEvidence", {}), expected, candidate, acquisition,
             capture_diagnostics=companions if companions is not None else capture_diagnostics)
    planes = acquisition["planes"]
    require(len(planes) == 2 and {plane.get("eye") for plane in planes} == set(EYES), "incoherent acquired eye pair")
    outputs = {}
    for eye in EYES:
        plane = next(item for item in planes if item["eye"] == eye)
        size = policy["eyes"][eye]["nativeSize"]
        require([plane.get("stagedWidth"), plane.get("stagedHeight")] == size, "native eye extent changed")
        for key in ("sourceWidth", "sourceHeight", "dxgiFormat", "colourSpace", "boundsApplied", "submittedBounds", "orientation", "publicationGeneration", "deviceIdentity", "tonemapSceneHdr"):
            require(key in plane and plane[key] is not None, "missing source plane " + key)
        require(plane == expected["planes"][eye], "source dimensions/bounds/format/generation/device changed")
        artifacts = [item for item in child["artifacts"] if item.get("actual", {}).get("view") == eye + "_eye"]
        require(len(artifacts) == 1, "duplicate or missing committed eye artifact")
        artifact = artifacts[0]
        path = committed(artifact, base)
        actual = artifact["actual"]
        require(actual.get("format") == "png" and actual.get("colourContract") == "sdr_srgb", "actual artifact encoding differs")
        require([actual.get("width"), actual.get("height")] == size, "actual dimensions differ")
        with Image.open(path) as image:
            require(image.format == "PNG" and list(image.size) == size and image.mode in ("RGB", "RGBA"), "not native RGB PNG")
            image.load()
            if image.mode == "RGBA":
                require(image.getchannel("A").getextrema() == (255, 255), "non-opaque HMD pixels")
        outputs[eye] = {"path": str(path), "sha256": artifact["sha256"], "bytes": artifact["bytes"]}
    require(planes[0]["publicationGeneration"] == planes[1]["publicationGeneration"]
            and planes[0]["deviceIdentity"] == planes[1]["deviceIdentity"], "eye publication identity mismatch")
    return {"requestId": child["requestId"], "ordinal": child["ordinal"], "acquisition": acquisition,
            "images": outputs, "captureDiagnostics": companions,
            "lightingPreservationEvidence": lighting_evidence(
                effective_reconstruction_settings(acquisition["nrEvidence"]["configuration"]["color"]["settings"]),
                [region for eye in EYES for region in acquisition["nrEvidence"][eye].get("physicalRegions", [])])}


def region_metrics(pixels: Any) -> dict:
    np, _, _, _ = libraries()
    rgb = np.asarray(pixels, dtype=np.float64)[..., :3]
    encoded = rgb @ np.array([0.2126, 0.7152, 0.0722])
    unit = rgb / 255.0
    linear = np.where(unit <= 0.04045, unit / 12.92, ((unit + 0.055) / 1.055) ** 2.4)
    luminance = linear @ np.array([0.2126, 0.7152, 0.0722])
    windows = [encoded[y:encoded.shape[0] - 2 + y, x:encoded.shape[1] - 2 + x] for y in range(3) for x in range(3)]
    local_mean = sum(windows) / 9
    local_variance = sum(window * window for window in windows) / 9 - local_mean * local_mean
    result = {"pixels": int(encoded.size)}
    for index, channel in enumerate("rgb"):
        result[f"mean_{channel}"] = float(rgb[..., index].mean())
        result[f"median_{channel}"] = float(np.median(rgb[..., index]))
    for percentile in (5, 50, 95):
        result[f"luma_p{percentile:02}"] = float(np.percentile(encoded, percentile))
        result[f"luminance_p{percentile:02}"] = float(np.percentile(luminance, percentile))
    result.update(luma_mean=float(encoded.mean()), luminance_mean=float(luminance.mean()),
                  near_black_fraction=float((encoded <= 5).mean()), near_white_fraction=float((encoded >= 250).mean()),
                  clipped_black_fraction=float((rgb == 0).any(axis=2).mean()),
                  clipped_white_fraction=float((rgb == 255).any(axis=2).mean()),
                  saturated_fraction=float((np.ptp(rgb, axis=2) >= 250).mean()),
                  regional_contrast=float(encoded.std()),
                  local_contrast=float(np.sqrt(np.maximum(local_variance, 0)).mean()),
                  edge_rms=float(np.sqrt((np.square(np.diff(encoded, axis=0)).mean()
                                          + np.square(np.diff(encoded, axis=1)).mean()) / 2)))
    return result


def compare_pixels(pixels: Any, reference: Any) -> dict:
    np, _, _, _ = libraries()
    left = np.asarray(pixels, dtype=np.float64)[..., :3]
    right = np.asarray(reference, dtype=np.float64)[..., :3]
    require(left.shape == right.shape, "unaligned region extents")
    measured = region_metrics(left)
    baseline = region_metrics(right)
    result = {"delta_" + key: measured[key] - value for key, value in baseline.items() if key != "pixels"}
    result["absolute_rgb_error"] = float(np.abs(left - right).mean())
    for index, channel in enumerate("rgb"):
        result["median_signed_" + channel] = float(np.median(left[..., index] - right[..., index]))
    mismatches = eligible = 0
    for axis in (0, 1):
        gradient = np.diff(left @ np.array([0.2126, 0.7152, 0.0722]), axis=axis)
        source_gradient = np.diff(right @ np.array([0.2126, 0.7152, 0.0722]), axis=axis)
        signs = np.where(np.abs(gradient) >= 2, np.sign(gradient), 0)
        source_signs = np.where(np.abs(source_gradient) >= 2, np.sign(source_gradient), 0)
        mask = (signs != 0) | (source_signs != 0)
        eligible += int(mask.sum())
        mismatches += int((mask & (signs != source_signs)).sum())
    result["gradient_sign_mismatch"] = mismatches / eligible if eligible else 0.0
    result["gradient_sign_samples"] = eligible
    return {**measured, **result}


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="") as stream:
        if rows:
            columns = list(dict.fromkeys(key for row in rows for key in row))
            writer = csv.DictWriter(stream, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)


@lru_cache(maxsize=4)
def original_pixels(path: str) -> Any:
    np, Image, _, _ = libraries()
    with Image.open(path) as image:
        return np.array(image, dtype=np.uint8)[..., :3]


def crop_pixels(path: str, rect: list[int]) -> Any:
    x, y, w, h = rect
    return original_pixels(path)[y:y + h, x:x + w]


def annotated_comparison(candidate: Any, reference: Any, path: Path, label: str) -> None:
    np, Image, ImageDraw, _ = libraries()
    height, width = candidate.shape[:2]
    canvas = Image.new("RGB", (width * 3, height + 42), (24, 24, 24))
    delta = np.clip(128 + (candidate.astype(float) - reference.astype(float)) * 2, 0, 255).astype(np.uint8)
    for index, pixels in enumerate((reference, candidate, delta)):
        canvas.paste(Image.fromarray(pixels), (index * width, 42))
    draw = ImageDraw.Draw(canvas)
    draw.text((3, 2), label + " | source / sample / signed difference", fill="white")
    draw.text((3, 18), "Difference: 128 = zero; fixed gain 2 code/code; clipped for display", fill="white")
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def import_campaign(index_path: Path, output: Path) -> tuple[dict, dict, list, list]:
    index = read_json(index_path)
    plan_path = resolve(index_path.parent, index["planPath"])
    require(digest(plan_path) == index["planSha256"], "plan changed after capture scheduling")
    plan = read_json(plan_path)
    require(plan["schema"] == VERSION and plan.get("performanceCampaign") is False, "wrong campaign contract")
    policy = plan["regionPolicy"]
    check_regions(policy)
    require(value_digest(policy) == plan["regionPolicySha256"], "region policy changed")
    require(index["sceneFingerprint"] == plan["sceneFingerprint"] and index["fixedSettings"] == plan["fixedSettings"], "scene or fixed graphics settings changed")
    require(index.get("producer") and index.get("devbench") and index.get("sessionId"), "CSX/DevBench/session identity required")
    preserve(index_path, output / "private" / "campaign.json")
    preserve(plan_path, output / "private" / "plan.json")
    host_provenance = preserve_devbench_identity(index["devbench"], index_path.parent, output)
    save_json(output / "private" / "host-provenance.json", host_provenance)
    schedule = {entry["scheduleOrdinal"]: entry for entry in plan["schedule"]}
    sequences, exclusions, seen = [], [], set()
    for entry in index["sequences"]:
        number = entry["scheduleOrdinal"]
        require(number in schedule and number not in seen, "unknown/duplicate acquisition schedule ordinal")
        seen.add(number)
        scheduled = schedule[number]
        candidate = plan["candidateMapping"][scheduled["candidateId"]]
        directory = output / "private" / "captures" / f"S{number:04}"
        save_json(directory / "import.json", entry)
        if entry.get("notRunReason"):
            exclusions.append({"scheduleOrdinal": number, "reason": entry["notRunReason"], "kind": "not_run"})
            continue
        pairs = []
        try:
            manifest_source = resolve(index_path.parent, entry["manifest"]["path"])
            if manifest_source.is_file():
                preserve(manifest_source, directory / "manifest.json")
            manifest_path = committed(entry["manifest"], index_path.parent)
            manifest = read_json(manifest_path)
            applied_upscaling = copy.deepcopy(entry["expected"]["configuration"]["upscaling"])
            applied_upscaling.pop("neuralRenderingEnabled", None)
            require(applied_upscaling == index["fixedSettings"], "unrelated graphics/ROI/upscaler settings changed")
            require(manifest["state"] == "final", "partial manifest")
            require(manifest.get("contract", {}).get("name") == "csx.screenshot"
                    and manifest["contract"].get("major") == 1, "unknown screenshot manifest contract")
            require(bool(manifest.get("requestId")), "missing sequence request identity")
            require(manifest["producer"] == index["producer"] and manifest["sessionId"] == index["sessionId"], "producer/session changed")
            require(datetime.fromisoformat(manifest["acceptedUtc"].replace("Z", "+00:00")) >= datetime.fromisoformat(plan["createdUtc"]), "capture predates frozen plan")
            require(manifest["counts"]["inFlight"] == 0, "sequence still active")
            require(manifest["counts"]["requested"] >= scheduled["minimumPairs"], "short requested sequence")
            check_capture_descriptor(manifest["requested"])
            check_capture_descriptor(manifest["effective"])
            children = manifest["children"]
            ordinals = [child["ordinal"] for child in children]
            require(len(set(ordinals)) == len(ordinals), "duplicate child ordinal")
            require(len({child["requestId"] for child in children}) == len(children), "duplicate child request identity")
            require(len(children) == manifest["counts"]["scheduled"]
                    and set(ordinals) == set(range(1, len(children) + 1)), "scheduled child ordinals unaccounted")
            diagnostics = entry.get("captureDiagnostics")
            if diagnostics is not None:
                diagnostics_path = recorded_file(entry["captureDiagnosticsArtifact"], index_path.parent)
                preserve(diagnostics_path, directory / "capture-diagnostics.json")
                require(read_json(diagnostics_path) == diagnostics, "exposure companion differs from committed sidecar")
            for child in children:
                # Retain even decodable but ineligible images before classifying them.
                for artifact_number, artifact in enumerate(child.get("artifacts", [])):
                    source = resolve(manifest_path.parent, artifact.get("path", ""))
                    if source.is_file():
                        preserve(source, directory / f"P{child['ordinal']:04}-A{artifact_number:02}{source.suffix}")
                try:
                    pair = check_pair(child, manifest_path.parent, entry["expected"], candidate, policy,
                                      capture_diagnostics=diagnostics, sequence=manifest)
                    check_camera(pair["acquisition"], plan["fixedScene"])
                    require(not pairs or pair["acquisition"]["monotonicTimestampUs"] > pairs[-1]["acquisition"]["monotonicTimestampUs"], "non-increasing acquisition timestamp")
                    require(not pairs or pair["acquisition"]["compositorCycle"] != pairs[-1]["acquisition"]["compositorCycle"], "duplicate submitted cycle")
                    pairs.append(pair)
                except (EvidenceError, KeyError, TypeError, OSError, ValueError) as error:
                    exclusions.append({"scheduleOrdinal": number, "ordinal": child.get("ordinal"), "requestId": child.get("requestId"), "reason": str(error), "kind": "capture"})
            for ordinal in range(len(children) + 1, manifest["counts"]["requested"] + 1):
                exclusions.append({"scheduleOrdinal": number, "ordinal": ordinal, "reason": "sequence terminated before scheduling", "kind": "not_scheduled"})
            require(len(pairs) >= scheduled["minimumPairs"], "insufficient accepted pairs; sequence retained but excluded")
            require(not entry.get("recording", {}).get("limitReached", True)
                    and entry["recording"].get("unrecordedTailMs") == 0, "recording coverage incomplete")
            recordings = entry["recording"].get("artifacts", [])
            require(bool(recordings), "preserved recording artifact missing")
            for recording_index, descriptor in enumerate(recordings):
                recording_path = recorded_file(descriptor, index_path.parent)
                preserved = directory / f"recording-{recording_index:02}.json"
                preserve(recording_path, preserved)
                scene = recording_scene(read_json(preserved), plan)
                require(scene["qualified"], scene["reason"])
            require(entry.get("motionEvidence") and entry["motionEvidence"].get("available") is True, "exact pose/scene evidence unavailable")
            require(entry["motionEvidence"].get("fixedSceneFingerprint") == plan["sceneFingerprint"], "scene/camera identity drift")
            sequences.append({**scheduled, "pairs": pairs, "expected": entry["expected"], "motionEvidence": entry["motionEvidence"],
                              "captureDiagnostics": diagnostics})
        except (EvidenceError, KeyError, TypeError, OSError, ValueError) as error:
            exclusions.append({"scheduleOrdinal": number, "reason": str(error), "kind": "sequence"})
    for number in sorted(set(schedule) - seen):
        exclusions.append({"scheduleOrdinal": number, "reason": "no capture receipt", "kind": "not_run"})
    ordered = sorted(sequences, key=lambda sequence: sequence["scheduleOrdinal"])
    for previous, sequence in zip(ordered, ordered[1:]):
        require(sequence["pairs"][0]["acquisition"]["monotonicTimestampUs"] > previous["pairs"][-1]["acquisition"]["monotonicTimestampUs"],
                "actual acquisition order differs from frozen randomized schedule")
    return index, plan, sequences, exclusions


def prepare_anonymous(sequences: list, policy: dict, output: Path, seed: int) -> None:
    _, Image, _, _ = libraries()
    shuffled = list(range(len(sequences)))
    random.Random(seed ^ 0xA16709).shuffle(shuffled)
    for index, sequence in enumerate(sequences):
        sequence["anonymousId"] = f"Q{shuffled[index] + 1:04}"
        for pair in sequence["pairs"]:
            for eye in EYES:
                name = f"{sequence['anonymousId']}-P{pair['ordinal']:04}-{eye}.png"
                descriptor = preserve(Path(pair["images"][eye]["path"]), output / "review" / "originals" / name)
                require(descriptor["sha256"] == pair["images"][eye]["sha256"], "source changed while making anonymous copy")
                pair["images"][eye]["anonymousPath"] = descriptor["path"]
                pair["images"][eye]["imageId"] = name.removesuffix(".png")
        # Crops are fixed native pixels, selected at the predeclared ordinal.
        representative = sequence["pairs"][len(sequence["pairs"]) // 2]
        sequence["crops"] = []
        for eye in EYES:
            for region in policy["eyes"][eye]["regions"]:
                pixels = crop_pixels(representative["images"][eye]["anonymousPath"], region["rect"])
                name = f"{sequence['anonymousId']}-{eye}-{region['id']}.png"
                path = output / "review" / "crops" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                Image.fromarray(pixels).save(path)
                sequence["crops"].append({"imageId": name.removesuffix(".png"), "path": str(path.resolve()), "eye": eye,
                                          "region": region["id"], "ordinal": representative["ordinal"]})


def measure_sequences(sequences: list, policy: dict, output: Path) -> tuple[list, list, list, list]:
    np, _, _, _ = libraries()
    initial = {s["repetition"]: s for s in sequences if s["block"] == "unchanged_baseline"}
    require(set(initial) == {1, 2, 3}, "three accepted unchanged baseline sequences must precede candidates")
    first_candidate = min((s["scheduleOrdinal"] for s in sequences if s["role"] == "candidate"), default=math.inf)
    require(all(s["scheduleOrdinal"] < first_candidate for s in initial.values()), "candidate captured before baseline repeats")
    rows, temporal, stereo = [], [], []
    for sequence in sequences:
        reference = initial[1 if sequence["role"] == "baseline" else sequence["repetition"]]
        references = {pair["ordinal"]: pair for pair in reference["pairs"]}
        prior = {}
        for pair in sequence["pairs"]:
            for eye in EYES:
                for region in policy["eyes"][eye]["regions"]:
                    if pair["ordinal"] not in references:
                        continue
                    ref_pair = references[pair["ordinal"]]
                    pixels = crop_pixels(pair["images"][eye]["anonymousPath"], region["rect"])
                    ref_pixels = crop_pixels(ref_pair["images"][eye]["anonymousPath"], region["rect"])
                    measured = compare_pixels(pixels, ref_pixels)
                    row = {"scheduleOrdinal": sequence["scheduleOrdinal"], "candidateId": sequence["candidateId"],
                           "anonymousId": sequence["anonymousId"], "repetition": sequence["repetition"],
                           "direction": sequence["direction"], "role": sequence["role"], "ordinal": pair["ordinal"],
                           "eye": eye, "region": region["id"], "regionClass": region["class"],
                           "referenceAnonymousId": reference["anonymousId"], "requestId": pair["requestId"],
                           "acquiredTimestampUs": pair["acquisition"]["monotonicTimestampUs"],
                           "engineFrame": pair["acquisition"]["engineFrame"], **measured}
                    row["motionConfounded"] = measured["gradient_sign_mismatch"] > policy["motionGradientMismatchLimit"]
                    row["confoundReason"] = "fixed-region gradient directions disagree; no alignment attempted" if row["motionConfounded"] else ""
                    rows.append(row)
                    key = (eye, region["id"])
                    if key in prior:
                        previous = prior[key]
                        temporal.append({key: row[key] for key in ("scheduleOrdinal", "candidateId", "ordinal", "eye", "region")}
                                        | {"spacingUs": row["acquiredTimestampUs"] - previous["acquiredTimestampUs"],
                                           "spacingFrames": row["engineFrame"] - previous["engineFrame"],
                                           "contiguousOrdinal": row["ordinal"] == previous["ordinal"] + 1,
                                           "motionConfounded": row["motionConfounded"] or previous["motionConfounded"],
                                           **{"step_" + metric: row[metric] - previous[metric]
                                              for metric in ("mean_r", "mean_g", "mean_b", "luma_mean", "edge_rms")}})
                    prior[key] = row
                    if pair is sequence["pairs"][len(sequence["pairs"]) // 2]:
                        annotated_comparison(pixels, ref_pixels,
                                             output / "private" / "annotations" / f"{sequence['anonymousId']}-{eye}-{region['id']}.png",
                                             f"{sequence['anonymousId']} {eye} {region['id']}")
    indexed = {(r["scheduleOrdinal"], r["ordinal"], r["region"], r["eye"]): r for r in rows}
    for row in rows:
        if row["eye"] != "left":
            continue
        right = indexed.get((row["scheduleOrdinal"], row["ordinal"], row["region"], "right"))
        if right:
            stereo.append({key: row[key] for key in ("scheduleOrdinal", "candidateId", "ordinal", "region")}
                          | {"motionConfounded": row["motionConfounded"] or right["motionConfounded"],
                             **{"left_minus_right_" + metric: row[metric] - right[metric]
                                for metric in ("delta_mean_r", "delta_mean_g", "delta_mean_b", "delta_luma_mean", "delta_edge_rms")}})
    grouped = defaultdict(list)
    for row in rows:
        if not row["motionConfounded"]:
            grouped[(row["scheduleOrdinal"], row["eye"], row["region"])].append(row)
    repeats = []
    delta_metrics = [key for key in rows[0] if key.startswith("delta_")] if rows else []
    for (number, eye, region), samples in grouped.items():
        repeats.append({"scheduleOrdinal": number, "candidateId": samples[0]["candidateId"],
                        "repetition": samples[0]["repetition"], "direction": samples[0]["direction"],
                        "role": samples[0]["role"], "eye": eye, "region": region, "frames": len(samples),
                        **{key: float(np.mean([sample[key] for sample in samples])) for key in delta_metrics}})
    envelope = defaultdict(dict)
    for eye in EYES:
        for region in policy["eyes"][eye]["regions"]:
            baseline = [row for row in repeats if row["role"] == "baseline" and row["eye"] == eye and row["region"] == region["id"]]
            for metric in delta_metrics:
                values = [row[metric] for row in baseline]
                if values:
                    envelope[eye][region["id"] + ":" + metric] = {"sequenceCount": len(values), "min": min(values),
                        "max": max(values), "std": float(np.std(values)), "maxAbsolute": max(abs(v) for v in values)}
    for row in repeats:
        for metric in delta_metrics:
            bound = envelope[row["eye"]].get(row["region"] + ":" + metric)
            row[metric + "_baselineMaxAbs"] = bound["maxAbsolute"] if bound else None
            row[metric + "_exceedsBaseline"] = abs(row[metric]) > bound["maxAbsolute"] if bound else None
    save_json(output / "private" / "baseline-repeatability.json", {"unitOfReplication": "sequence", "envelopes": envelope,
              "warning": "Frames are dependent samples. Motion/exposure/animation may explain variation; no causal colour verdict follows."})
    return rows, temporal, stereo, repeats


def public_sequence(sequence: dict) -> dict:
    return {"sequenceId": sequence["anonymousId"], "frames": [
        {"ordinal": pair["ordinal"], "elapsedUs": pair["acquisition"]["monotonicTimestampUs"] - sequence["pairs"][0]["acquisition"]["monotonicTimestampUs"],
         "images": [{"eye": eye, "imageId": pair["images"][eye]["imageId"], "path": pair["images"][eye]["anonymousPath"]} for eye in EYES]}
        for pair in sequence["pairs"]], "crops": sequence["crops"]}


def exposure_measurements(sequences: list) -> list[dict]:
    rows = []
    for sequence in sequences:
        previous = {}
        for pair in sequence["pairs"]:
            evidence = pair["acquisition"]["nrEvidence"]
            samples = {"engine": evidence.get("engineExposure", {})}
            samples.update({eye: evidence[eye]["exposure"] for eye in EYES})
            for scope, frozen in samples.items():
                row = {"scheduleOrdinal": sequence["scheduleOrdinal"], "candidateId": sequence["candidateId"],
                       "ordinal": pair["ordinal"], "requestId": pair["requestId"], "scope": scope,
                       "acquiredTimestampUs": pair["acquisition"]["monotonicTimestampUs"],
                       "producerFrame": frozen.get("frame"), "epoch": frozen.get("epoch"), "sequence": frozen.get("sequence"),
                       "available": False, "capturedRatio": None, "ratioStep": None, "frameGammaExponent": None, "reason": ""}
                try:
                    measured = resolved_exposure(frozen, pair.get("captureDiagnostics") or sequence.get("captureDiagnostics"))
                    row.update(available=True, frameGammaExponent=measured.get("frameGammaExponent"))
                    values = measured.get("rawValues", [])
                    if len(values) >= 3 and type(values[2]) in (int, float) and math.isfinite(values[2]):
                        row["capturedRatio"] = values[2]
                        if scope in previous:
                            row["ratioStep"] = values[2] - previous[scope]
                        previous[scope] = values[2]
                    else:
                        row["reason"] = "exact valid binding contains no numeric captured ratio"
                except (EvidenceError, KeyError, TypeError) as error:
                    row["reason"] = str(error)
                rows.append(row)
    return rows


def make_reviews(plan: dict, sequences: list, output: Path) -> list:
    mappings = plan["candidateMapping"]
    raw = next(key for key, value in mappings.items() if value["condition"] == "raw")
    eligible = [key for key, value in mappings.items() if value["condition"] not in ("nr_off", "raw", "display_only_source")]
    pairs = [(raw, key) for key in eligible]
    pairs += [(key, value["hiddenCandidateId"]) for key, value in mappings.items() if "hiddenCandidateId" in value]
    comparisons = [(pair, direction) for pair in pairs for direction in ("forward", "reverse")]
    by_candidate = defaultdict(list)
    for sequence in sequences:
        by_candidate[sequence["candidateId"]].append(sequence)
    references = [s for s in sequences if s["block"] == "unchanged_baseline"]
    template = (HERE / "hmd-review.prompt.md").read_text(encoding="utf-8-sig")
    schema = output / "review" / "hmd-review.schema.json"
    preserve(HERE / "hmd-review.schema.json", schema)
    requests = []
    generator = random.Random(plan["seed"] ^ 0x4B2019)
    generator.shuffle(comparisons)
    for comparison_index, (pair, direction) in enumerate(comparisons, 1):
        if generator.randrange(2):
            pair = tuple(reversed(pair))
        comparison_id = f"V{comparison_index:03}"
        passes = []
        for presentation in (1, 2):
            first, second = pair if presentation == 1 else tuple(reversed(pair))
            batches = []
            for replicate in (1, 2, 3):
                groups = [[s for s in by_candidate[key] if s["repetition"] == replicate
                           and s["role"] == "candidate" and s["direction"] == direction][:1] for key in (first, second)]
                if not all(groups) or len(references) != 3:
                    continue
                # Review one complete sequence per phase/direction, never isolated thumbnails.
                review_references = [s for s in references if s["repetition"] in (replicate, replicate % 3 + 1)]
                payload = {"comparisonId": comparison_id, "presentationPass": presentation, "replicate": replicate,
                           "first": [public_sequence(s) for s in groups[0]], "second": [public_sequence(s) for s in groups[1]],
                           "ownEyeSourceReferences": [public_sequence(s) for s in review_references],
                           "regions": plan["regionPolicy"]["eyes"], "normalization": "none",
                           "temporalSamplingLimitation": SAMPLING_LIMITATION}
                image_list = []
                for sequence in groups[0] + groups[1] + review_references:
                    for frame in public_sequence(sequence)["frames"]:
                        image_list.extend(image["path"] for image in frame["images"])
                    image_list.extend(crop["path"] for crop in sequence["crops"])
                image_list = list(dict.fromkeys(image_list))
                prompt = template + "\n\nAnonymous request:\n" + json.dumps(payload, indent=2)
                stem = f"{comparison_id}-pass{presentation}-rep{replicate}"
                batches.append({"replicate": replicate, "promptText": prompt, "images": image_list,
                                "outputSchemaPath": str(schema.resolve()),
                                "responsePath": str((output / "review" / "responses" / (stem + ".json")).resolve()),
                                "eventsPath": str((output / "review" / "responses" / (stem + ".jsonl")).resolve())})
            passes.append({"presentationPass": presentation, "batches": batches})
        ready = all(len(value["batches"]) == 3 for value in passes)
        request = {"comparisonId": comparison_id, "ready": ready, "workingDirectory": str((output / "review").resolve()), "passes": passes}
        save_json(output / "review" / "requests" / (comparison_id + ".json"), request)
        requests.append({"comparisonId": comparison_id, "firstCandidateId": pair[0], "secondCandidateId": pair[1],
                         "direction": direction, "ready": ready})
    save_json(output / "private" / "review-mapping.json", requests)
    (output / "review" / "responses").mkdir(parents=True, exist_ok=True)
    return requests


def analyse(index_path: Path, output: Path) -> dict:
    require(not output.exists(), "analysis output already exists; use a new evidence directory")
    output.mkdir(parents=True)
    index, plan, sequences, exclusions = import_campaign(index_path, output)
    save_json(output / "private" / "exclusions.json", exclusions)
    require(sequences, "no accepted sequences; exclusions and originals retained")
    prepare_anonymous(sequences, plan["regionPolicy"], output, plan["seed"])
    rows, temporal, stereo, repeats = measure_sequences(sequences, plan["regionPolicy"], output)
    exposures = exposure_measurements(sequences)
    actual_spacings = [row["spacingUs"] for row in temporal]
    for name, data in (("regions", rows), ("temporal", temporal), ("stereo", stereo), ("sequence-repeatability", repeats), ("exposure", exposures)):
        write_csv(output / "private" / (name + ".csv"), data)
        save_json(output / "private" / (name + ".json"), data)
    requests = make_reviews(plan, sequences, output)
    manifest = {"schema": VERSION, "createdUtc": utc(), "campaignSha256": digest(index_path),
                "planSha256": index["planSha256"], "regionPolicySha256": plan["regionPolicySha256"],
                "sourceSha256": digest(Path(__file__)), "versions": {name: importlib.metadata.version(name) for name in ("numpy", "Pillow", "jsonschema")},
                "sequencesAccepted": len(sequences), "captureExclusions": len(exclusions),
                "motionConfoundedRegions": sum(row["motionConfounded"] for row in rows),
                "unavailableExposureSamples": sum(not row["available"] for row in exposures),
                "reviewRequests": requests, "assessmentStatus": "awaiting_blinded_image_review",
                "runtimeQualificationComplete": False,
                "frameRateFlickerQualified": False,
                "temporalSampling": {"requestedIntervalMs": plan.get("intervalMs", 500),
                    "observedSpacingUsMin": min(actual_spacings) if actual_spacings else None,
                    "observedSpacingUsMax": max(actual_spacings) if actual_spacings else None,
                    "limitation": SAMPLING_LIMITATION},
                "hostProvenance": read_json(output / "private" / "host-provenance.json"),
                "metrics": {"rgb": "8-bit sRGB code values", "luma": "0.2126R+0.7152G+0.0722B, encoded code values",
                            "luminance": "fixed IEC sRGB decode then same coefficients, relative 0..1",
                            "nearBlack": "encoded luma <=5", "nearWhite": "encoded luma >=250",
                            "contrast": "mean3x3 encoded-luma RMS contrast; regional standard deviation retained separately",
                            "motionScreen": "gradient presence/sign disagreement at >=2 code values; conservative confound screen, no registration",
                            "edges": "RMS horizontal/vertical encoded-luma adjacent differences",
                            "differenceImages": "128+2*(candidate-reference), identical fixed clipping",
                            "baseline": "sequence means; separate frames retained; no RGB scalar ranking"},
                "untestedConditions": plan.get("untestedConditions", []) + ["physical HMD panel/lens output", "NVIDIA colour contract", SAMPLING_LIMITATION,
                    "exposure recovery under a deliberate illumination challenge unless documented in campaign evidence"]}
    save_json(output / "private" / "analysis.json", manifest)
    save_json(output / "private" / "anonymous-sequence-mapping.json", [
        {key: sequence[key] for key in ("anonymousId", "candidateId", "scheduleOrdinal", "repetition", "direction", "block")}
        for sequence in sequences])
    with (output / "assessment.md").open("x", encoding="utf-8") as stream:
        stream.write(f"Imported {len(sequences)} eligible sequences; retained {len(exclusions)} capture/sequence exclusions.\n\n")
        stream.write("Regional, temporal and stereo measurements are diagnostics pending blinded review of original pixels. "
                     "No colour-quality winner, neural-detail verdict or production default has been selected. "
                     "See private/exclusions.json and private/analysis.json for coverage and provenance.\n")
    return manifest


def repeatability_support(output: Path, first: str, second: str) -> dict:
    """Require signed effects beyond unchanged-baseline variation in both orders.

    Edge energy is only corroboration of a visible detail difference. It never
    establishes useful reconstruction without the separate original-image review.
    """
    np, _, _, _ = libraries()
    rows = read_json(output / "private" / "sequence-repeatability.json")
    baseline = read_json(output / "private" / "baseline-repeatability.json")["envelopes"]
    sequence_metadata = {row["scheduleOrdinal"]: row for row in rows}
    metrics = {"colourFidelity": ("delta_mean_r", "delta_mean_g", "delta_mean_b", "delta_luma_p05", "delta_luma_p95"),
               "usefulNeuralDetail": ("delta_edge_rms", "delta_local_contrast"),
               "temporalStability": ("step_luma_mean", "step_mean_r", "step_mean_g", "step_mean_b", "step_edge_rms"),
               "stereoConsistency": ("left_minus_right_delta_mean_r", "left_minus_right_delta_mean_g",
                                      "left_minus_right_delta_mean_b", "left_minus_right_delta_luma_mean")}
    result = {}
    for objective, names in metrics.items():
        values = defaultdict(list)
        limits = defaultdict(list)
        if objective in ("colourFidelity", "usefulNeuralDetail"):
            source = rows
        else:
            source = read_json(output / "private" / ("temporal.json" if objective == "temporalStability" else "stereo.json"))
        sequence_values = defaultdict(list)
        for row in source:
            if row.get("motionConfounded") or row.get("contiguousOrdinal") is False or row.get("frames", 12) < 12:
                continue
            metadata = sequence_metadata.get(row["scheduleOrdinal"])
            if not metadata:
                continue
            eye = row.get("eye", "both")
            for metric in names:
                measured = row[metric]
                if objective in ("temporalStability", "stereoConsistency"):
                    measured = abs(measured)
                sequence_values[(row["scheduleOrdinal"], eye, row["region"], metric)].append(measured)
        for (number, eye, region, metric), samples in sequence_values.items():
            metadata = sequence_metadata[number]
            measured = float(np.mean(samples))
            key = (eye, region, metric)
            if metadata["role"] == "baseline":
                limits[key].append(measured)
            else:
                values[(metadata["candidateId"], metadata["repetition"], metadata["direction"], *key)].append(measured)
        evidence = []
        keys = {key[-3:] for key in values}
        for eye, region, metric in sorted(keys):
            if objective in ("colourFidelity", "usefulNeuralDetail"):
                envelope = baseline.get(eye, {}).get(region + ":" + metric)
                threshold = 2 * envelope["maxAbsolute"] if envelope else None
            else:
                samples = limits.get((eye, region, metric), [])
                threshold = 2 * (max(samples) - min(samples)) if len(samples) >= 3 else None
            if threshold is None:
                continue
            effects = []
            for repetition in (1, 2, 3):
                for direction in ("forward", "reverse"):
                    a = values.get((first, repetition, direction, eye, region, metric))
                    b = values.get((second, repetition, direction, eye, region, metric))
                    if a and b:
                        effects.append(float(np.mean(a) - np.mean(b)))
            if len(effects) == 6 and all(abs(value) > max(1e-9, threshold) for value in effects) and (all(value > 0 for value in effects) or all(value < 0 for value in effects)):
                evidence.append({"eye": eye, "region": region, "metric": metric,
                                 "firstMinusSecond": effects, "baselineBound": threshold})
        supported = any(row["eye"] == "both" for row in evidence) if objective == "stereoConsistency" else (
            {row["eye"] for row in evidence} == set(EYES))
        result[objective] = {"supported": supported, "evidence": evidence,
                             "rule": "same signed effect in all three repetitions and both acquisition orders, above twice observed baseline variation"}
    return result


def reconcile_acquisition_orders(decisions: list[dict]) -> None:
    """Opposing visual conclusions across acquisition order stay unresolved."""
    groups = defaultdict(list)
    for decision in decisions:
        groups[tuple(sorted((decision["firstCandidateId"], decision["secondCandidateId"])))].append(decision)
    for group in groups.values():
        reviewed = [decision for decision in group if decision["status"] == "reviewed"]
        complete = len(reviewed) == 2 and {decision.get("direction") for decision in reviewed} == {"forward", "reverse"}
        for objective in OBJECTIVES:
            winners = []
            for decision in reviewed:
                verdict = decision["objectives"][objective]["imageVerdict"]
                winners.append({"first_better": decision["firstCandidateId"], "second_better": decision["secondCandidateId"]}.get(verdict, verdict))
            agreement = complete and len(set(winners)) == 1 and "indeterminate" not in winners
            for decision in reviewed:
                value = decision["objectives"][objective]
                value["acquisitionOrderAgreement"] = agreement
                if not agreement:
                    value["qualifiedVerdict"] = "indeterminate"


def finalise(output: Path, receipts: list[Path]) -> dict:
    _, _, _, jsonschema = libraries()
    require(not (output / "sealed-reviews.json").exists(), "review assessments already sealed")
    analysis = read_json(output / "private" / "analysis.json")
    schema = read_json(output / "review" / "hmd-review.schema.json")
    expected_ids = {item["comparisonId"] for item in analysis["reviewRequests"] if item["ready"]}
    received = {}
    preserved_receipts = []
    for path in receipts:
        receipt_copy = output / "private" / "provider-receipts" / (digest(path) + ".json")
        if receipt_copy.exists():
            require(digest(receipt_copy) == digest(path), "preserved provider receipt changed")
            preserved_receipts.append({"path": str(receipt_copy), "bytes": receipt_copy.stat().st_size, "sha256": digest(receipt_copy)})
        else:
            preserved_receipts.append(preserve(path, receipt_copy))
        receipt = read_json(path)
        require(receipt.get("schema") == "csx-codex-visual-review-execution-v1" and receipt.get("ok") is True
                and receipt.get("provider") == "codex_cli", "fresh image-provider execution failed; receipt retained")
        require(len(receipt["batches"]) == 6, "six swapped replicate assessments required")
        comparison = None
        for batch in receipt["batches"]:
            response_path = Path(batch["responsePath"])
            require(batch.get("ok") is True and digest(response_path) == batch["responseSha256"], "response integrity mismatch")
            response = read_json(response_path)
            try:
                jsonschema.validate(response, schema)
            except jsonschema.ValidationError as error:
                raise EvidenceError("invalid structured review: " + error.message) from error
            comparison = comparison or response["comparisonId"]
            require(comparison == response["comparisonId"] and comparison in expected_ids, "unexpected review comparison")
            request = read_json(output / "review" / "requests" / (comparison + ".json"))
            planned = request["passes"][batch["presentationPass"] - 1]["batches"][batch["replicate"] - 1]
            require(response["presentationPass"] == batch["presentationPass"] and response["replicate"] == batch["replicate"], "review presentation identity mismatch")
            require(Path(planned["responsePath"]).resolve() == response_path.resolve(), "response outside planned batch")
            require(hashlib.sha256(planned["promptText"].encode()).hexdigest() == batch["promptSha256"], "review prompt changed")
            bindings = batch["imageBindings"]
            require({str(Path(item["path"]).resolve()) for item in bindings} == set(planned["images"]), "review image set changed")
            for binding in bindings:
                require(digest(Path(binding["path"])) == binding["sha256"], "review input image changed")
            require(digest(Path(batch["eventsPath"])) == batch["eventsSha256"], "provider event integrity mismatch")
            required_images = {Path(image).stem for image in planned["images"]}
            require(set(response["inspectedImageIds"]) | set(response["missingImageIds"]) == required_images, "review image coverage not accounted")
            require(not (set(response["inspectedImageIds"]) & set(response["missingImageIds"])), "review claims image both inspected and missing")
            for objective in response["objectives"].values():
                require(objective["verdict"] == "indeterminate" or objective["evidence"], "directional/tie verdict lacks image evidence")
                for finding in objective["evidence"]:
                    require(bool(finding["imageIds"]) and set(finding["imageIds"]) <= set(response["inspectedImageIds"]), "uncited image evidence")
            key = (comparison, batch["presentationPass"], batch["replicate"])
            require(key not in received, "duplicate review assessment")
            received[key] = {"response": response, "responseSha256": batch["responseSha256"], "eventsSha256": batch["eventsSha256"]}
    require({key[0] for key in received} == expected_ids and len(received) == 6 * len(expected_ids) and expected_ids,
            "missing complete blinded review comparisons")
    seal = {"schema": VERSION, "sealedUtc": utc(), "unblindingOccurred": False,
            "analysisSha256": digest(output / "private" / "analysis.json"), "providerReceipts": preserved_receipts,
            "assessments": list(received.values())}
    save_json(output / "sealed-reviews.json", seal)
    # Read the private mapping only after every assessment and hash is immutable.
    mapping = read_json(output / "private" / "review-mapping.json")
    candidate_mapping = read_json(output / "private" / "plan.json")["candidateMapping"]
    decisions = []
    for item in mapping:
        if not item["ready"]:
            decisions.append({**item, "status": "untested", "reason": "incomplete capture repetitions"})
            continue
        assessments = [received[(item["comparisonId"], p, r)]["response"] for p in (1, 2) for r in (1, 2, 3)]
        corroboration = repeatability_support(output, item["firstCandidateId"], item["secondCandidateId"])
        objectives = {}
        for objective in OBJECTIVES:
            votes = []
            for response in assessments:
                judgement = response["objectives"][objective]
                vote = judgement["verdict"]
                if response["presentationPass"] == 2:
                    vote = {"first_better": "second_better", "second_better": "first_better"}.get(vote, vote)
                if judgement["confidence"] == "low" or response["missingImageIds"] or response["exclusions"]:
                    vote = "indeterminate"
                votes.append(vote)
            image_verdict = votes[0] if len(set(votes)) == 1 else "indeterminate"
            qualified = image_verdict
            if image_verdict in ("first_better", "second_better") and not corroboration[objective]["supported"]:
                qualified = "indeterminate"
            objectives[objective] = {"imageVerdict": image_verdict, "qualifiedVerdict": qualified,
                                     "repeatability": corroboration[objective]}
            if objective == "temporalStability":
                objectives[objective]["scope"] = "sampled capture cadence only"
        decisions.append({**item, "status": "reviewed", "objectives": objectives,
                          "firstCondition": candidate_mapping[item["firstCandidateId"]]["condition"],
                          "secondCondition": candidate_mapping[item["secondCandidateId"]]["condition"],
                          "observations": [response["imageAssessment"] for response in assessments],
                          "uncertainties": sorted({text for response in assessments for text in response["uncertainties"]})})
    reconcile_acquisition_orders(decisions)
    result = {"schema": VERSION, "sealSha256": digest(output / "sealed-reviews.json"), "unblindedUtc": utc(),
              "comparisons": decisions, "productionDefaultSelected": False,
              "imageReviewComplete": True, "runtimeQualificationComplete": False,
              "frameRateFlickerQualified": False,
              "temporalSampling": analysis.get("temporalSampling", {"limitation": SAMPLING_LIMITATION}),
              "allPlannedConditionsReviewed": all(item["ready"] for item in mapping),
              "captureExclusions": analysis.get("captureExclusions", 0),
              "motionConfoundedRegions": analysis.get("motionConfoundedRegions", 0),
              "untestedConditions": sorted(set(analysis.get("untestedConditions", [])) |
                  {condition for assessment in received.values() for condition in assessment["response"]["untestedConditions"]}),
              "hostProvenance": analysis.get("hostProvenance", {"physicalModulePathVerified": False}),
              "limitation": "Image judgments remain bounded by baseline repeatability, excluded regions and captured scene. They do not prove NVIDIA's colour contract."}
    save_json(output / "final-assessment.json", result)
    with (output / "final-assessment.md").open("x", encoding="utf-8") as stream:
        stream.write("Blinded image assessments were sealed before reading the candidate mapping. "
                     "The four objectives are reported separately; no scalar winner or production default was selected.\n\n")
        stream.write(f"Retained {result['captureExclusions']} capture/sequence exclusions and "
                     f"{result['motionConfoundedRegions']} motion-confounded regional measurements. "
                     "See private/exclusions.json and private/regions.json for every case. "
                     "This image review does not complete runtime/deployment qualification.\n\n")
        for decision in decisions:
            stream.write(f"{decision['comparisonId']}: {decision['firstCandidateId']} ({decision.get('firstCondition', 'unreviewed')}) / "
                         f"{decision['secondCandidateId']} ({decision.get('secondCondition', 'unreviewed')}) — {decision['status']}.\n\n")
            if decision["status"] == "reviewed":
                for objective, verdict in decision["objectives"].items():
                    stream.write(f"- {objective}: {verdict['qualifiedVerdict']} (blinded image assessment: {verdict['imageVerdict']})\n")
                stream.write("\n" + "\n\n".join(decision["observations"]) + "\n\n")
                if decision["uncertainties"]:
                    stream.write("Uncertainties:\n\n" + "\n".join("- " + text for text in decision["uncertainties"]) + "\n\n")
        if result["untestedConditions"]:
            stream.write("Untested conditions:\n\n" + "\n".join("- " + text for text in result["untestedConditions"]) + "\n\n")
        stream.write(result["limitation"] + "\n")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    plan_parser = commands.add_parser("plan", help="freeze private settings/regions/randomized capture schedule")
    plan_parser.add_argument("--spec", type=Path, required=True)
    plan_parser.add_argument("--regions", type=Path, required=True)
    plan_parser.add_argument("--seed", type=int, required=True)
    plan_parser.add_argument("--output", type=Path, required=True)
    analysis_parser = commands.add_parser("analyse", help="strictly import actual committed sequences, measure and prepare reviews")
    analysis_parser.add_argument("--campaign", type=Path, required=True)
    analysis_parser.add_argument("--output", type=Path, required=True)
    final_parser = commands.add_parser("finalise", help="seal completed provider reviews then reveal mapping")
    final_parser.add_argument("--analysis", type=Path, required=True)
    final_parser.add_argument("--provider-receipt", type=Path, action="append", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "plan":
            result = make_plan(read_json(args.spec), read_json(args.regions), args.seed)
            save_json(args.output, result)
            print(json.dumps({"plan": str(args.output.resolve()), "sha256": digest(args.output), "sequences": len(result["schedule"])}))
        elif args.command == "analyse":
            result = analyse(args.campaign.resolve(), args.output.resolve())
            print(json.dumps({key: result[key] for key in ("assessmentStatus", "sequencesAccepted", "captureExclusions")}))
        else:
            result = finalise(args.analysis.resolve(), [path.resolve() for path in args.provider_receipt])
            print(json.dumps({"comparisons": len(result["comparisons"]), "sealSha256": result["sealSha256"]}))
        return 0
    except (EvidenceError, KeyError, TypeError, OSError, ValueError, AttributeError, IndexError) as error:
        failure = {"status": "evidence_rejected", "reason": str(error), "imageQualityVerdict": None}
        destination = getattr(args, "output", None)
        if args.command == "analyse" and destination and destination.is_dir() and not (destination / "failed-analysis.json").exists():
            save_json(destination / "failed-analysis.json", failure)
        print(json.dumps(failure), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
