"""Offline joins for capture-owned NR execution evidence; never polls a runtime."""
from __future__ import annotations

import copy
import math


class TransactionEvidenceError(ValueError):
    """Execution records cannot substantiate their declared producer transaction."""


IDENTITY = ("sourceTransactionId", "publicationSequence", "frame", "sourceWorldFrame", "generation",
            "captureEpoch", "configurationEpoch", "route", "mode", "fovOnly", "logicalEyeCount")
CONTEXT_IDENTITY = ("sourceTransactionId", "captureEpoch", "configurationEpoch", "mode", "fovOnly", "sourceContext")
REGION_DESCRIPTOR = ("physicalSlot", "logicalSlot", "eye", "region", "regionIdentity", "clusterIdentity",
                     "source", "nrInput", "nrOutput", "nrDepthGuide", "nrMotionGuide", "controlMask",
                     "nrViewport", "motionVectorScale", "characterSelection", "featureUpscaling")
EXECUTION_DESCRIPTOR = ("submissionId", "source", "frame", "sourceWorldFrame", "generation", "colorRevision",
                        "inputEpoch", "route", "legacyInsertionPoint", "logicalEyeCount", "plannedRegionCount",
                        "plannedPhysicalSlotMask", "transportBypass")
CHARACTER_CONTENT_IDENTITY = ("sourceWorldFrame", "eye", "logicalSlot", "generation", "contentSerial",
                              "settingsKey", "captureEpoch", "viewport", "capturedJitterPixels")


def require(valid: bool, message: str) -> None:
    if not valid:
        raise TransactionEvidenceError(message)


def uint(value: object, maximum: int = (1 << 64) - 1) -> bool:
    return type(value) is int and 0 <= value <= maximum


def finite(value: object) -> bool:
    try:
        return type(value) in (int, float) and math.isfinite(value) and value >= 0
    except (OverflowError, TypeError):
        return False


def exact(left: dict, right: dict, fields: tuple[str, ...], label: str) -> None:
    require(isinstance(left, dict) and isinstance(right, dict), label + " object missing")
    for name in fields:
        require(name in left and name in right and type(left[name]) is type(right[name])
                and left[name] == right[name], label + " identity mismatch: " + name)


def optional_exact(left: dict, right: dict, fields: tuple[str, ...], label: str) -> None:
    for field in fields:
        if field in left or field in right:
            exact(left, right, (field,), label)


def exact_timing_handles(left: object, right: object, label: str) -> None:
    """Delayed values may complete, but every retained invocation must stay the same."""
    def collect(value: object, path: tuple = ()) -> dict:
        identities = {}
        if isinstance(value, dict):
            if "captureId" in value:
                require(uint(value["captureId"]), label + " invalid timing captureId")
                identities[path] = value["captureId"]
            for name, item in value.items():
                identities.update(collect(item, path + (name,)))
        elif isinstance(value, list):
            for index, item in enumerate(value):
                identities.update(collect(item, path + (index,)))
        return identities

    require(collect(left) == collect(right), label + " timing captureId mismatch")


def validate_optional_samples(value: object) -> None:
    """An absent duration/support count cannot be encoded as a measured zero."""
    if isinstance(value, float):
        require(math.isfinite(value), "nonfinite execution evidence")
    elif isinstance(value, list):
        for item in value:
            validate_optional_samples(item)
    elif isinstance(value, dict):
        fields = [key for key in ("inclusiveMs", "selfMs", "microseconds", "milliseconds") if key in value]
        if "state" in value and "pixels" in value:
            fields.append("pixels")
        if fields:
            state = value.get("state")
            if state is None and fields == ["microseconds"] and "operation" in value:
                require(isinstance(value["operation"], str) and bool(value["operation"])
                        and finite(value["microseconds"]) and uint(value.get("timeoutMilliseconds"))
                        and uint(value.get("result")) and uint(value.get("error")), "invalid observed wait sample")
            else:
                require(state in ("ready", "complete", "pending", "unavailable", "failed"), "unknown sample availability")
                for field in fields:
                    require(finite(value[field]) if state in ("ready", "complete") else value[field] is None,
                            "sample availability/value mismatch: " + field)
        for item in value.values():
            validate_optional_samples(item)


def texture_area(texture: dict, required: bool) -> int:
    require(isinstance(texture, dict), "texture descriptor missing")
    grid, work = texture.get("capacityGrid"), texture.get("work")
    require(isinstance(grid, dict) and isinstance(work, dict), "texture grid/work missing")
    require(all(uint(grid.get(k), 16384) for k in ("width", "height"))
            and all(uint(work.get(k), 16384) for k in ("x", "y", "width", "height")), "invalid texture geometry")
    area = work["width"] * work["height"]
    require(work["x"] + work["width"] <= grid["width"] and work["y"] + work["height"] <= grid["height"],
            "texture work exceeds capacity")
    require(not required or area > 0, "attempted evaluation has an empty texture")
    require(texture.get("workPixels") == area and type(texture.get("workPixels")) is int, "work pixel accounting mismatch")
    require(type(texture.get("capacityPixels")) is int
            and texture["capacityPixels"] == grid["width"] * grid["height"], "capacity pixel accounting mismatch")
    return area


def validate_characters(evidence: dict) -> None:
    characters = evidence.get("characters", [])
    require(isinstance(characters, list) and len(characters) <= 2, "invalid character evidence collection")
    for eye, character in enumerate(characters):
        require(isinstance(character, dict) and type(character.get("available")) is bool,
                "invalid character evidence availability")
        if not character["available"]:
            continue
        require(eye < evidence["logicalEyeCount"], "character evidence belongs to an inactive eye")
        key = character.get("key")
        exact(evidence, key, ("frame", "sourceWorldFrame", "generation", "captureEpoch"), "character producer")
        require(all(uint(key.get(field)) for field in CHARACTER_CONTENT_IDENTITY[:-2]), "invalid character identity")
        require(key["eye"] == eye and key["logicalSlot"] == eye + (2 if evidence["route"] == "submit" else 0),
                "character eye/route slot mismatch")
        require(type(character.get("reused")) is bool, "missing character reuse state")
        support = character.get("maskSupport", {})
        require(isinstance(support, dict), "invalid character support evidence")
        if "producer" in support:
            producer = support["producer"]
            exact(key, producer, CHARACTER_CONTENT_IDENTITY, "character support producer")
            require(uint(producer.get("frame")), "invalid character support producer frame")
            if character["reused"]:
                require(key["sourceWorldFrame"] <= producer["frame"] <= key["frame"],
                        "reused character support producer is outside its source lifetime")
            else:
                exact(key, producer, ("frame",), "character support producer")


def validate_envelope(evidence: dict) -> dict[int, dict]:
    require(isinstance(evidence, dict) and type(evidence.get("schemaVersion")) is int
            and evidence["schemaVersion"] == 1, "unsupported execution evidence schema")
    require(all(uint(evidence.get(k)) for k in
                ("sourceTransactionId", "publicationSequence", "frame", "sourceWorldFrame", "generation",
                 "captureEpoch", "configurationEpoch")), "missing producer identity")
    require(evidence["sourceTransactionId"] > 0 and evidence["captureEpoch"] > 0, "unbound producer identity")
    require(evidence.get("route") in ("main", "submit") and evidence.get("mode") in
            ("full_resolution", "foveated", "reduced_resolution") and type(evidence.get("fovOnly")) is bool,
            "invalid rendering route/mode")
    require(type(evidence.get("logicalEyeCount")) is int and evidence["logicalEyeCount"] in (1, 2), "invalid eye count")
    require(isinstance(evidence.get("sourceContext"), str) and bool(evidence["sourceContext"]), "missing source context")
    incomplete = evidence.get("rendererEvidenceIncomplete", False)
    require(type(incomplete) is bool, "invalid renderer evidence availability")
    for field in ("executionEvidenceFailures", "sourceEvidenceFailures", "droppedStageCount", "evidenceFailureCount"):
        if field in evidence:
            require(uint(evidence[field]), "invalid evidence failure count: " + field)
            require(incomplete or evidence[field] == 0, "evidence failures are not marked unavailable")
    executions = evidence.get("executions")
    require(isinstance(executions, list) and len(executions) <= 8, "invalid execution collection")
    by_id, attempted_eyes, succeeded_eyes = {}, set(), set()
    route_index = 0 if evidence["route"] == "main" else 1
    for execution in executions:
        require(isinstance(execution, dict) and uint(execution.get("submissionId"))
                and execution["submissionId"] > 0 and execution["submissionId"] not in by_id, "duplicate/missing submission identity")
        by_id[execution["submissionId"]] = execution
        if "evidenceFailed" in execution:
            require(type(execution["evidenceFailed"]) is bool, "invalid execution evidence failure state")
        exact(evidence, execution, ("frame", "sourceWorldFrame", "generation", "route"), "execution producer")
        exact(evidence, execution.get("source"), CONTEXT_IDENTITY, "execution context")
        require(type(execution.get("logicalEyeCount")) is int and 1 <= execution["logicalEyeCount"] <= evidence["logicalEyeCount"],
                "execution eye count exceeds transaction")
        regions = execution.get("regions")
        require(isinstance(regions, list) and len(regions) <= 4 and type(execution.get("plannedRegionCount")) is int
                and execution.get("plannedRegionCount") == len(regions), "planned region membership mismatch")
        slots, count, pixels, attempted_mask, succeeded_mask, committed_mask = set(), 0, 0, 0, 0, 0
        for region in regions:
            require(isinstance(region, dict), "physical region descriptor missing")
            exact(evidence, region.get("source"), CONTEXT_IDENTITY, "physical region context")
            slot, logical, eye = region.get("physicalSlot"), region.get("logicalSlot"), region.get("eye")
            require(uint(slot, 7) and slot not in slots and uint(logical, 3) and logical == slot % 4
                    and logical // 2 == route_index and uint(eye, 1) and eye == logical % 2
                    and eye < evidence["logicalEyeCount"] and region.get("region") == slot // 4, "invalid physical eye/slot membership")
            slots.add(slot)
            for flag in ("evaluationAttempted", "evaluationSucceeded", "privateOutputCommitted"):
                require(type(region.get(flag)) is bool, "missing physical region outcome")
            attempted, succeeded = region["evaluationAttempted"], region["evaluationSucceeded"]
            require(not succeeded or attempted, "evaluation succeeded without an attempt")
            for name in ("nrInput", "nrDepthGuide", "nrMotionGuide"):
                texture_area(region.get(name), attempted)
            area = texture_area(region.get("nrOutput"), attempted)
            if attempted:
                count += 1
                pixels += area
                attempted_mask |= 1 << slot
                attempted_eyes.add(eye)
            if succeeded:
                succeeded_mask |= 1 << slot
                succeeded_eyes.add(eye)
            if region["privateOutputCommitted"]:
                committed_mask |= 1 << slot
        require(execution.get("plannedPhysicalSlotMask") == sum(1 << s for s in slots), "planned slot mask mismatch")
        require(type(execution.get("actualEvaluationCount")) is int and execution["actualEvaluationCount"] == count,
                "actual evaluation count does not equal physical attempts")
        require(type(execution.get("activeEvaluationPixels")) is int
                and execution["activeEvaluationPixels"] == pixels, "actual evaluation pixels do not equal attempted regions")
        require(execution.get("attemptedPhysicalSlotMask") == attempted_mask
                and execution.get("succeededPhysicalSlotMask") == succeeded_mask
                and execution.get("privateCommittedPhysicalSlotMask") == committed_mask, "physical outcome mask mismatch")
    outcomes = evidence.get("workOutcome")
    require(isinstance(outcomes, list) and evidence["logicalEyeCount"] <= len(outcomes) <= 2, "missing per-eye work outcomes")
    for eye, outcome in enumerate(outcomes[:evidence["logicalEyeCount"]]):
        require(outcome in ("NoWork", "unavailable", "failed", "successful"), "unknown work outcome")
        require(outcome != "NoWork" or eye not in attempted_eyes, "NoWork eye has actual inference")
        require(outcome != "successful" or eye in succeeded_eyes or incomplete,
                "successful eye has no successful inference")
    boundary = evidence.get("producerBoundary")
    require(isinstance(boundary, dict) and boundary.get("outcome") in
            ("NoWork", "unavailable", "failed", "fallback", "successful"), "missing producer boundary outcome")
    for stage in evidence.get("sourceStages", []):
        require(isinstance(stage, dict), "invalid source stage")
        exact(evidence, stage, ("frame", "sourceWorldFrame", "generation"), "source stage")
        require(stage.get("matchesProducer") is True, "source stage does not match producer")
    validate_characters(evidence)
    validate_optional_samples(evidence)
    return by_id


def join_execution_evidence(acquisition: dict, diagnostics: dict | None = None) -> dict | None:
    """Join immutable acquisition and optional delayed companion; old schemas remain untouched."""
    frozen = acquisition.get("executionEvidence")
    delayed = diagnostics.get("executionEvidence") if isinstance(diagnostics, dict) else None
    if frozen is None:
        require(delayed is None, "delayed execution has no frozen acquisition descriptor")
        return None
    frozen_ids = validate_envelope(frozen)
    for field in ("sourceTransactionId", "publicationSequence", "captureEpoch", "configurationEpoch", "generation", "route"):
        if field in acquisition:
            exact(acquisition, frozen, (field,), "acquisition")
    for eye in ("left", "right")[:frozen["logicalEyeCount"]]:
        if eye in acquisition:
            exact(acquisition[eye], frozen, ("frame", "sourceWorldFrame"), "acquisition eye")
            for execution in frozen_ids.values():
                exact(acquisition[eye], execution, ("colorRevision", "inputEpoch"), "acquisition colour")
    for execution in frozen_ids.values():
        if "configurationFingerprint" in execution:
            exact(acquisition, execution, ("configurationFingerprint",), "execution configuration")
    companion_state = "absent"
    selected = frozen
    if delayed is not None:
        require(isinstance(delayed, dict), "invalid delayed execution companion")
        if delayed.get("available") is False:
            require(bool(delayed.get("reason")) and not delayed.get("executions"), "unavailable companion contains executions")
            companion_state = "unavailable"
        else:
            exact(frozen, delayed, IDENTITY + ("sourceContext",), "delayed producer")
            require(delayed.get("finalized") is True, "delayed companion is not finalized")
            delayed_ids = validate_envelope(delayed)
            for field in ("sourceContexts", "workOutcome", "producerBoundary", "droppedStageCount", "dlssDispatches",
                          "rendererEvidenceIncomplete", "executionEvidenceFailures", "sourceEvidenceFailures",
                          "evidenceFailureCount", "retainedExecutionCount"):
                if field in frozen or field in delayed:
                    exact(frozen, delayed, (field,), "delayed frozen producer")
            require(frozen_ids.keys() == delayed_ids.keys(), "delayed submission membership changed")
            for identity, execution in frozen_ids.items():
                other = delayed_ids[identity]
                exact(execution, other, EXECUTION_DESCRIPTOR, "delayed execution descriptor")
                optional_exact(execution, other, ("colourExposureConfiguration", "configurationFingerprint"),
                               "delayed colour configuration")
                exact_timing_handles(execution.get("timing"), other.get("timing"), "delayed execution")
                delayed_regions = {r["physicalSlot"]: r for r in other["regions"]}
                for region in execution["regions"]:
                    current = delayed_regions.get(region["physicalSlot"])
                    exact(region, current, REGION_DESCRIPTOR, "delayed physical descriptor")
                    optional_exact(region, current, ("depthSourceFormat", "depthViewFormat"), "delayed depth formats")
                    exact_timing_handles(region.get("timing"), current.get("timing"), "delayed physical region")
            for field in ("sourceStages", "characters"):
                original_items, delayed_items = frozen.get(field, []), delayed.get(field, [])
                require(isinstance(original_items, list) and isinstance(delayed_items, list)
                        and len(original_items) == len(delayed_items), "delayed " + field + " membership mismatch")
                for original, current in zip(original_items, delayed_items):
                    keys = ("name", "eye", "frame", "sourceWorldFrame", "generation", "dirtyDispatchPixels", "copiedLogicalBytes") if field == "sourceStages" else ("available",)
                    exact(original, current, keys, "delayed " + field)
                    exact_timing_handles(original, current, "delayed " + field)
                    if field == "characters" and original.get("available") is True:
                        exact(original, current, ("key", "outcome", "prepared", "requiresEvaluation", "reused", "computeSubrect", "regions"), "delayed character contents")
                        optional_exact(original.get("maskSupport", {}), current.get("maskSupport", {}),
                                       ("producer",), "delayed character support")
            selected, companion_state = delayed, "joined"
    incomplete = selected.get("rendererEvidenceIncomplete", False)
    failed = any(execution.get("evidenceFailed", False) for execution in selected["executions"])
    return {"available": not incomplete and not failed,
            "reason": "renderer_evidence_incomplete" if incomplete else "execution_evidence_failed" if failed else "",
            "executionEvidence": copy.deepcopy(selected), "companionState": companion_state,
            "companionReason": delayed.get("reason", "") if companion_state == "unavailable" else ""}
