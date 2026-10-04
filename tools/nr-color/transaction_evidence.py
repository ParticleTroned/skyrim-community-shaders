"""Offline joins for capture-owned NR execution evidence; never polls a runtime."""
from __future__ import annotations

import copy
import math


class TransactionEvidenceError(ValueError):
    """Execution records cannot substantiate their declared producer transaction."""


IDENTITY = ("sourceTransactionId", "publicationSequence", "frame", "sourceWorldFrame", "generation",
            "captureEpoch", "configurationEpoch", "route", "mode", "fovOnly", "logicalEyeCount")
OPTIONAL_CONTEXT_IDENTITY = ("renderscaleFov",)
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


def validate_shared_ownership(region: dict) -> None:
    """Admit one original-coordinate evaluation with independently owned outputs."""
    roi = region["roi"]
    require(roi.get("contextPolicy") == "experimental_shared_original_coordinates"
            and roi.get("coordinateDomain") == "output_crop_local"
            and "ownedOutput" in roi and roi["ownedOutput"] is None
            and roi.get("compactSource") is None, "invalid shared ownership contract (ROI roles)")
    require(isinstance(region.get("nativeLayout"), dict)
            and region.get("characterSelection") is True and region.get("featureUpscaling") is False
            and type(region.get("effectiveReset")) is bool
            and (region.get("evaluationAttempted") is False or region["effectiveReset"] is True)
            and region.get("source", {}).get("mode") == "reduced_resolution",
            "unsupported shared ownership state")
    capacity = roi.get("allocationCapacity")
    require(isinstance(capacity, dict) and all(uint(capacity.get(k), 16384) and capacity[k] > 0
                                              for k in ("width", "height")), "invalid shared ownership capacity")
    full = {"x": 0, "y": 0, "width": capacity["width"], "height": capacity["height"]}

    def contained(rect: object, parent: dict) -> int:
        require(isinstance(rect, dict) and all(uint(rect.get(k), 16384) for k in ("x", "y", "width", "height"))
                and rect["width"] > 0 and rect["height"] > 0
                and rect["x"] >= parent["x"] and rect["y"] >= parent["y"]
                and rect["x"] + rect["width"] <= parent["x"] + parent["width"]
                and rect["y"] + rect["height"] <= parent["y"] + parent["height"],
                "shared ownership rectangle exceeds its domain")
        return rect["width"] * rect["height"]

    context = roi.get("inferenceContext")
    context_pixels = contained(context, full)
    require(context["width"] >= 128 and context["height"] >= 128, "shared context is below the qualified minimum")
    outputs = roi.get("ownedOutputs")
    require(isinstance(outputs, list) and 1 <= len(outputs) <= 8, "invalid shared ownership count")
    owned_pixels = 0
    for index, rect in enumerate(outputs):
        owned_pixels += contained(rect, context)
        for previous in outputs[:index]:
            require(rect["x"] >= previous["x"] + previous["width"] or
                    previous["x"] >= rect["x"] + rect["width"] or
                    rect["y"] >= previous["y"] + previous["height"] or
                    previous["y"] >= rect["y"] + rect["height"], "shared ownership rectangles overlap")
    for name, expected in (("ownedOutputPixels", owned_pixels), ("inferencePixels", context_pixels),
                           ("capacityPixels", capacity["width"] * capacity["height"])):
        require(uint(roi.get(name)) and roi[name] == expected, "shared ownership pixel accounting mismatch: " + name)
    require("temporalEnvelope" in roi and roi["temporalEnvelope"] is None
            and "temporalEnvelopePixels" in roi and roi["temporalEnvelopePixels"] is None,
            "shared ownership cannot claim a temporal envelope")
    hull = {"x": min(r["x"] for r in outputs), "y": min(r["y"] for r in outputs)}
    hull.update(width=max(r["x"] + r["width"] for r in outputs) - hull["x"],
                height=max(r["y"] + r["height"] for r in outputs) - hull["y"])
    support_pixels = contained(roi.get("samplingSupport"), hull)
    require(roi.get("samplingSupportKind") == "conservative_guarded_enclosure"
            and uint(roi.get("samplingSupportEnclosurePixels"))
            and roi["samplingSupportEnclosurePixels"] == support_pixels,
            "invalid shared ownership sampling proof")
    for name in ("nrInput", "nrDepthGuide", "nrMotionGuide", "nrOutput"):
        exact({"capacityGrid": capacity, "work": context}, region.get(name), ("capacityGrid", "work"),
              "shared ownership original-coordinate texture")
    control = region.get("controlMask")
    require(isinstance(control, dict) and type(control.get("capacityPixels")) is int
            and control["capacityPixels"] == 0, "shared ownership has a control mask")


def validate_native_layout(region: dict) -> None:
    """A frozen layout must agree with the physical descriptors in its own record."""
    roi = region.get("roi")
    if isinstance(roi, dict) and ("ownedOutputs" in roi or ("ownedOutput" in roi and roi["ownedOutput"] is None)
                                 or roi.get("contextPolicy") == "experimental_shared_original_coordinates"):
        validate_shared_ownership(region)
    layout = region.get("nativeLayout")
    if layout is None:
        return
    contract = {"coordinateDomain": "resource_local_texels",
                "inputInitializationContract": "valid_rectangles_before_evaluation",
                "nativeReadableFootprint": None, "motionSourceUnits": "full_input_normalized",
                "motionConsumerUnits": "native_guide_pixels", "motionConversion": "native_parameter_scale_once"}
    exact(contract, layout, tuple(contract), "native layout contract")
    for role, name in (("color", "nrInput"), ("depth", "nrDepthGuide"), ("motion", "nrMotionGuide"),
                       ("output", "nrOutput"), ("controlMask", "controlMask")):
        texture = region.get(name)
        if role == "controlMask":
            texture_area(texture, False)
            if texture["capacityPixels"] == 0:
                require(role in layout and layout[role] is None, "native layout has an absent control mask")
                continue
        image = layout.get(role)
        expected = {"backingExtent": texture["capacityGrid"], "validRect": texture["work"]}
        exact(expected, image, tuple(expected), "native layout " + role)
        require(all(uint(image["backingExtent"].get(k), 16384) for k in ("width", "height"))
                and all(uint(image["validRect"].get(k), 16384) for k in ("x", "y", "width", "height")),
                "native layout geometry must use integer texels")
    expected = {"creationInputExtent": region["nrDepthGuide"]["capacityGrid"],
                "creationOutputExtent": region["nrOutput"]["capacityGrid"],
                "motionVectorScale": region.get("motionVectorScale"), "featureUpscaling": region.get("featureUpscaling")}
    exact(expected, layout, tuple(expected), "native layout creation/motion")
    require(all(uint(layout[field].get(k), 16384) for field in ("creationInputExtent", "creationOutputExtent")
                for k in ("width", "height")), "native layout creation extents must use integer texels")
    scale = layout["motionVectorScale"]
    require(isinstance(scale, list) and len(scale) == 2 and all(finite(v) and v > 0 for v in scale)
            and type(layout["featureUpscaling"]) is bool, "native layout motion or upscaling is invalid")
    viewport = region.get("nrViewport")
    require(isinstance(viewport, dict) and isinstance(viewport.get("fullInput"), dict),
            "native layout has no full-input motion domain")
    require(all(uint(viewport["fullInput"].get(k), 16384) for k in ("width", "height"))
            and scale == [viewport["fullInput"][k] for k in ("width", "height")],
            "native layout motion conversion differs from its full-input domain")
    roi = region.get("roi", {})
    require(isinstance(roi, dict), "invalid native layout ROI object")
    if roi.get("compactSource") is not None or roi.get("coordinateDomain") == "compact_storage_local":
        source = roi.get("compactSource")
        require(isinstance(source, dict) and all(uint(source.get(k), 16384) for k in ("x", "y", "width", "height")),
                "invalid compact source origin")
        require(roi.get("coordinateDomain") == "compact_storage_local" and layout["controlMask"] is None
                and layout["featureUpscaling"] is False and region.get("effectiveReset") is True
                and region.get("source", {}).get("mode") == "reduced_resolution", "unsupported compact state")
        extent = {k: source[k] for k in ("width", "height")}
        full = {"x": 0, "y": 0, **extent}
        require(extent["width"] > 0 and extent["height"] > 0, "empty compact bucket")
        for role in ("color", "depth", "motion", "output"):
            exact({"backingExtent": extent, "validRect": full}, layout[role], ("backingExtent", "validRect"),
                  "compact full-bucket initialization")
        exact({"allocationCapacity": extent, "inferenceContext": full, "temporalEnvelope": None}, roi,
              ("allocationCapacity", "inferenceContext", "temporalEnvelope"), "compact ROI capacity")
        output = viewport.get("output", {})
        full_output = viewport.get("fullOutput", {})
        require(isinstance(output, dict) and isinstance(full_output, dict)
                and all(uint(full_output.get(k), 16384) for k in ("width", "height"))
                and all(uint(output.get(k), 16384) for k in ("left", "top", "right", "bottom"))
                and output["right"] <= full_output["width"] and output["bottom"] <= full_output["height"]
                and source["x"] + source["width"] <= output["right"] - output["left"]
                and source["y"] + source["height"] <= output["bottom"] - output["top"], "compact source exceeds original crop")
        parent = full
        for name in ("ownedOutput", "samplingSupport"):
            rect = roi.get(name)
            require(isinstance(rect, dict) and all(uint(rect.get(k), 16384) for k in full)
                    and rect["width"] > 0 and rect["height"] > 0
                    and rect["x"] >= parent["x"] and rect["y"] >= parent["y"]
                    and rect["x"] + rect["width"] <= parent["x"] + parent["width"]
                    and rect["y"] + rect["height"] <= parent["y"] + parent["height"], "invalid compact ownership/support")
            parent = rect


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
        if "sourceCaptureSerial" in key:
            require(uint(key["sourceCaptureSerial"]), "invalid character source capture")
            source = character.get("sourceCapture")
            if isinstance(source, dict) and source.get("available") is True:
                exact(key, source, ("sourceWorldFrame", "captureEpoch"), "character source capture")
                require(uint(source.get("boundsCaptureSerial"))
                        and source["boundsCaptureSerial"] == key["sourceCaptureSerial"],
                        "character source capture serial mismatch")
        if "emptyProof" in character:
            require(type(character.get("prepared")) is bool and type(character.get("requiresEvaluation")) is bool,
                    "invalid character preparation/evaluation state")
            proof = character["emptyProof"]
            require(proof in ("none", "cpu_selection", "gpu_category_superset", "diagnostic_zero"),
                    "invalid character empty proof")
            if character.get("prepared"):
                require((proof == "none") == character.get("requiresEvaluation"),
                        "character empty proof contradicts evaluation")
                if proof != "none":
                    require(uint(key.get("sourceCaptureSerial")) and key["sourceCaptureSerial"] > 0,
                            "character empty proof has no source capture")
        support = character.get("maskSupport", {})
        require(isinstance(support, dict), "invalid character support evidence")
        if "producer" in support:
            producer = support["producer"]
            exact(key, producer, CHARACTER_CONTENT_IDENTITY, "character support producer")
            optional_exact(key, producer, ("sourceCaptureSerial",), "character support source capture")
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
    if "renderscaleFov" in evidence:
        require(type(evidence["renderscaleFov"]) is bool, "invalid renderscale FOV setting")
    require(type(evidence.get("logicalEyeCount")) is int and evidence["logicalEyeCount"] in (1, 2), "invalid eye count")
    require(isinstance(evidence.get("sourceContext"), str) and bool(evidence["sourceContext"]), "missing source context")
    incomplete = evidence.get("rendererEvidenceIncomplete", False)
    require(type(incomplete) is bool, "invalid renderer evidence availability")
    for field in ("executionEvidenceFailures", "sourceEvidenceFailures", "droppedStageCount", "evidenceFailureCount"):
        if field in evidence:
            require(uint(evidence[field]), "invalid evidence failure count: " + field)
            require(incomplete or evidence[field] == 0, "evidence failures are not marked unavailable")
    executions = evidence.get("executions")
    require(isinstance(executions, list) and len(executions) <= 32, "invalid execution collection")
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
        optional_exact(evidence, execution.get("source"), OPTIONAL_CONTEXT_IDENTITY, "execution context")
        require(type(execution.get("logicalEyeCount")) is int and 1 <= execution["logicalEyeCount"] <= evidence["logicalEyeCount"],
                "execution eye count exceeds transaction")
        regions = execution.get("regions")
        require(isinstance(regions, list) and len(regions) <= 16 and type(execution.get("plannedRegionCount")) is int
                and execution.get("plannedRegionCount") == len(regions), "planned region membership mismatch")
        slots, count, pixels, attempted_mask, succeeded_mask, committed_mask = set(), 0, 0, 0, 0, 0
        for region in regions:
            require(isinstance(region, dict), "physical region descriptor missing")
            exact(evidence, region.get("source"), CONTEXT_IDENTITY, "physical region context")
            optional_exact(evidence, region.get("source"), OPTIONAL_CONTEXT_IDENTITY, "physical region context")
            slot, logical, eye = region.get("physicalSlot"), region.get("logicalSlot"), region.get("eye")
            require(uint(slot, 31) and slot not in slots and uint(logical, 3) and logical == slot % 4
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
            validate_native_layout(region)
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
            optional_exact(frozen, delayed, OPTIONAL_CONTEXT_IDENTITY, "delayed route configuration")
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
                optional_exact(execution, other, ("measuredPlan",), "delayed measured plan")
                optional_exact(execution, other, ("sharedContext",), "delayed shared context")
                optional_exact(execution, other, ("colourExposureConfiguration", "configurationFingerprint"),
                               "delayed colour configuration")
                exact_timing_handles(execution.get("timing"), other.get("timing"), "delayed execution")
                delayed_regions = {r["physicalSlot"]: r for r in other["regions"]}
                for region in execution["regions"]:
                    current = delayed_regions.get(region["physicalSlot"])
                    exact(region, current, REGION_DESCRIPTOR, "delayed physical descriptor")
                    optional_exact(region, current, ("roi",), "delayed ROI roles")
                    optional_exact(region, current, ("nativeLayout",), "delayed native layout")
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
                        optional_exact(original, current, ("roi",), "delayed character ROI roles")
                        optional_exact(original, current, ("emptyProof",), "delayed character empty proof")
                        optional_exact(original.get("maskSupport", {}), current.get("maskSupport", {}),
                                       ("producer",), "delayed character support")
            selected, companion_state = delayed, "joined"
    incomplete = selected.get("rendererEvidenceIncomplete", False)
    failed = any(execution.get("evidenceFailed", False) for execution in selected["executions"])
    return {"available": not incomplete and not failed,
            "reason": "renderer_evidence_incomplete" if incomplete else "execution_evidence_failed" if failed else "",
            "executionEvidence": copy.deepcopy(selected), "companionState": companion_state,
            "companionReason": delayed.get("reason", "") if companion_state == "unavailable" else ""}
