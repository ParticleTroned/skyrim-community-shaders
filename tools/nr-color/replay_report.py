#!/usr/bin/env python3
"""Validate and summarize native NR replay receipts without inventing samples."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

from transaction_evidence import TransactionEvidenceError, finite, require, uint


SCHEMA = "csx-nr-replay-results-v1"
HISTORIES = {"static_reset", "continuous", "cold_create"}
AXES = {"width", "height", "offset", "aspect_ratio", "call_count", "capacity", "mask_occupancy", "history", "alignment", "minimum_shape"}
FOOTPRINT_FIELDS = ("modifiedInsidePixels", "modifiedOutsidePixels", "unchangedInsidePixels", "nonfiniteInsidePixels")
IDENTITY_FIELDS = ("sourceGuideAlignment", "colorConfiguration", "tuning", "sourceFrameIndices",
                   "featureUpscaling", "useAutoMask", "controlMaskPassed", "logicalEyeCount")
# Two-sided Student t quantiles for 1..30 degrees of freedom.
T95 = (12.7062, 4.3027, 3.1824, 2.7764, 2.5706, 2.4469, 2.3646, 2.3060,
       2.2622, 2.2281, 2.2010, 2.1788, 2.1604, 2.1448, 2.1314, 2.1199,
       2.1098, 2.1009, 2.0930, 2.0860, 2.0796, 2.0739, 2.0687, 2.0639,
       2.0595, 2.0555, 2.0518, 2.0484, 2.0452, 2.0423)


def finite_tree(value: object) -> None:
    if isinstance(value, float):
        require(math.isfinite(value), "nonfinite value in replay receipt")
    elif isinstance(value, dict):
        for child in value.values():
            finite_tree(child)
    elif isinstance(value, list):
        for child in value:
            finite_tree(child)


def dimensions(value: object, label: str) -> list[int]:
    require(isinstance(value, list) and len(value) == 2
            and all(uint(v, 16384) and v > 0 for v in value), "invalid " + label)
    return value


def statistics_summary(values: list[float]) -> dict:
    """The interval describes sampled timing variability, not temporal quality."""
    n = len(values)
    if not n:
        return {"n": 0, "mean": None, "median": None, "sampleStdev": None,
                "minimum": None, "maximum": None, "mean95Interval": None,
                "intervalReason": "no_qualifying_samples"}
    mean = statistics.mean(values)
    stdev = statistics.stdev(values) if n > 1 else None
    interval = None
    if stdev is not None:
        # Conservatively retain the df=30 quantile for larger samples.
        margin = T95[min(n - 2, len(T95) - 1)] * stdev / math.sqrt(n)
        interval = [mean - margin, mean + margin]
    return {"n": n, "mean": mean, "median": statistics.median(values), "sampleStdev": stdev,
            "minimum": min(values), "maximum": max(values), "mean95Interval": interval,
            "intervalReason": "descriptive_student_t_independent_stationary_samples_assumed"
            if interval else "at_least_two_samples_required"}


def duration(value: object) -> bool:
    return finite(value) and value <= (1 << 64) - 1


def sha256(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdefABCDEF" for c in value)


def rectangle(value: object, grids: list[list[int]], label: str) -> None:
    require(isinstance(value, dict) and all(uint(value.get(k), 16384)
            for k in ("baseX", "baseY", "width", "height"))
            and value["width"] > 0 and value["height"] > 0, "invalid " + label)
    for width, height in grids:
        require(value["baseX"] + value["width"] <= width
                and value["baseY"] + value["height"] <= height, label + " exceeds capacity")


def checked_case(case: dict) -> dict:
    require(isinstance(case, dict) and isinstance(case.get("id"), str) and bool(case["id"]), "case id missing")
    require(case.get("history") in HISTORIES, "invalid history mode: " + case["id"])
    require(case.get("route") in {"A", "B", "C"} and uint(case.get("mode"), 2)
            and case["route"] == "ABC"[case["mode"]], "captured route/mode mismatch")
    require(isinstance(case.get("axis"), str) and bool(case["axis"]), "experiment axis missing")
    require(case.get("pairGroup") is None or isinstance(case["pairGroup"], str), "invalid paired experiment identity")
    require(case.get("status") in {"complete", "failed", "unavailable"}, "invalid case status")
    require(type(case.get("temporalSequence", False)) is bool, "invalid temporal sequence policy")
    source_indices = case.get("sourceFrameIndices")
    require(isinstance(source_indices, list) and (source_indices or case["status"] != "complete") and len(source_indices) <= 8192
            and all(uint(index) for index in source_indices), "invalid source frame sequence")
    require(uint(case.get("warmupIterations"), 4096) and uint(case.get("requestedSamples"), 4096)
            and case["requestedSamples"] > 0, "invalid sample budget")
    capacity = dimensions(case.get("creationExtent"), "feature creation extent")
    resources = case.get("resourceExtents")
    require(isinstance(resources, dict), "resource extents missing")
    for name in ("color", "depth", "motion", "output"):
        dimensions(resources.get(name), name + " resource extent")
    regions = case.get("evaluatedRects")
    require(isinstance(regions, list) and 1 <= len(regions) <= 8, "invalid evaluated rectangle count")
    require(uint(case.get("evaluationsPerSample"), 8) and case["evaluationsPerSample"] == len(regions),
            "declared call count does not match evaluated rectangles")
    area = 0
    for region in regions:
        rectangle(region, [capacity, resources["color"], resources["output"]], "evaluated output rectangle")
        area += region["width"] * region["height"]
    guides, source_regions = case.get("evaluatedGuideRects"), case.get("evaluatedSourceRects")
    for name, collection, grids in (("evaluated guide rectangle", guides, [resources["depth"], resources["motion"]]),
                                    ("evaluated source rectangle", source_regions, [[16384, 16384]])):
        require(isinstance(collection, list) and len(collection) == len(regions), "invalid " + name + " collection")
        for item in collection:
            rectangle(item, grids, name)
    require(resources["depth"] == resources["motion"], "depth and motion guide grids differ")
    for output_rect, guide in zip(regions, guides):
        for axis, (origin, length) in enumerate((("baseX", "width"), ("baseY", "height"))):
            source_extent, guide_extent = resources["output"][axis], resources["depth"][axis]
            start = output_rect[origin] * guide_extent // source_extent
            end = ((output_rect[origin] + output_rect[length]) * guide_extent + source_extent - 1) // source_extent
            require(guide[origin] == start and guide[length] == end - start, "guide rectangle does not match outward native mapping")
    require(all(source["width"] == region["width"] and source["height"] == region["height"]
                for source, region in zip(source_regions, regions)), "source density changed during replay")
    for flag in ("featureUpscaling", "useAutoMask", "controlMaskPassed", "characterSelection"):
        require(type(case.get(flag)) is bool, "missing feature contract: " + flag)
    require(uint(case.get("logicalEyeCount"), 2) and case["logicalEyeCount"] > 0, "invalid logical eye count")
    samples = case.get("samples")
    require(isinstance(samples, list) and len(samples) <= 8192, "invalid raw samples")
    iterations, accepted, excluded = set(), [], []
    memory = {field: [] for field in ("localUsageBefore", "localUsageAfter", "nonlocalUsageBefore", "nonlocalUsageAfter")}
    warmups, measured = 0, 0
    case_reasons = []
    prior_iteration = None
    for sample in samples:
        require(isinstance(sample, dict) and uint(sample.get("iteration")) and sample["iteration"] not in iterations,
                "duplicate or invalid sample iteration")
        iterations.add(sample["iteration"])
        if prior_iteration is not None and sample["iteration"] <= prior_iteration:
            case_reasons.append("sample_order_not_monotonic")
        first_sample = prior_iteration is None
        prior_iteration = sample["iteration"]
        require(all(type(sample.get(k)) is bool for k in ("warmup", "success", "reset")), "invalid sample flags")
        require(uint(sample.get("evaluationCount"), 8) and uint(sample.get("createdFeatureCount"), 8),
                "invalid observed call/create count")
        if sample["warmup"] and measured:
            case_reasons.append("warmup_observed_after_measurement")
        warmups += sample["warmup"]
        measured += not sample["warmup"]
        reasons = []
        if sample["warmup"]:
            reasons.append("warmup")
        if not sample["success"]:
            reasons.append("evaluation_failed: " + str(sample.get("reason") or "no_reason_recorded"))
        if case.get("transportBypass") is not False or case.get("applyModelEdit") is not True:
            reasons.append("non_bypassed_model_edit_not_proven")
        if sample["evaluationCount"] != case["evaluationsPerSample"]:
            reasons.append("actual_evaluation_count_mismatch")
        if case["history"] in {"static_reset", "cold_create"} and not sample["reset"]:
            reasons.append("reset_policy_not_observed")
        if case["history"] == "cold_create" and sample["createdFeatureCount"] != case["evaluationsPerSample"]:
            reasons.append("cold_creation_not_observed")
        if case["history"] == "continuous":
            if sample["reset"] != first_sample:
                reasons.append("continuous_history_reset_mismatch")
            if not first_sample and sample["createdFeatureCount"]:
                reasons.append("continuous_history_recreated")
        if sample["warmup"] and any(reason != "warmup" for reason in reasons):
            case_reasons.append("warmup_provider_state_failed")
        gpu = sample.get("gpuMicroseconds")
        per_call = sample.get("evaluationGpuMicroseconds")
        if not duration(gpu) or not isinstance(per_call, list) or len(per_call) != case["evaluationsPerSample"] or not all(duration(v) for v in per_call):
            reasons.append("gpu_timing_unavailable_or_incomplete")
        edit_pixels, edit = sample.get("nonzeroEditPixels"), sample.get("maximumAbsEdit")
        if not uint(edit_pixels) or edit_pixels <= 0 or not finite(edit) or edit <= 0:
            reasons.append("nonzero_nr_edit_not_observed")
        footprints = sample.get("providerFootprint")
        if not isinstance(footprints, list) or len(footprints) != len(regions):
            reasons.append("provider_footprint_unavailable")
        else:
            for region, footprint in zip(regions, footprints):
                require(isinstance(footprint, dict) and all(uint(footprint.get(field)) for field in FOOTPRINT_FIELDS),
                        "invalid provider footprint counters")
                rectangle_pixels = region["width"] * region["height"]
                require(footprint["modifiedInsidePixels"] + footprint["unchangedInsidePixels"] == rectangle_pixels
                        and footprint["nonfiniteInsidePixels"] <= rectangle_pixels
                        and footprint["modifiedOutsidePixels"] <= resources["output"][0] * resources["output"][1] - rectangle_pixels,
                        "provider footprint exceeds declared resource or work")
                if footprint["unchangedInsidePixels"] or footprint["nonfiniteInsidePixels"]:
                    reasons.append("provider_left_unwritten_or_nonfinite_pixels")
        if case["history"] == "continuous" and any(reason != "warmup" for reason in reasons):
            case_reasons.append("continuous_history_interrupted")
        for field in ("createCpuMicroseconds", "evalCpuMicroseconds", "elapsedCpuMicroseconds"):
            require(sample.get(field) is None or duration(sample[field]), "invalid CPU duration: " + field)
        observed_memory = sample.get("memory", {})
        require(isinstance(observed_memory, dict), "invalid memory sample")
        for field in memory:
            value = observed_memory.get(field)
            require(value is None or uint(value), "invalid memory bytes: " + field)
            if value is not None:
                memory[field].append(value)
        if reasons:
            excluded.append({"iteration": sample["iteration"], "reasons": reasons})
        else:
            accepted.append(sample)
    if warmups != case["warmupIterations"] or measured != case["requestedSamples"]:
        case_reasons.append("observed_sample_counts_differ_from_request")
    if case.get("status") in {"failed", "unavailable"}:
        case_reasons.append(str(case.get("reason") or case["status"]))
    if case["controlMaskPassed"]:
        case_reasons.append("manual_control_mask_contract_not_established")
    if case.get("characterSelection", False) and not case["useAutoMask"]:
        case_reasons.append("character_native_auto_mask_disabled")
    if case_reasons:
        case_reasons = list(dict.fromkeys(case_reasons))
        excluded.extend({"iteration": s["iteration"], "reasons": case_reasons} for s in accepted)
        accepted = []
    logical_bytes = case.get("logicalResourceBytes")
    require(logical_bytes is None or uint(logical_bytes), "invalid logical resource bytes")
    return {"id": case["id"], "route": case["route"], "axis": case["axis"], "history": case["history"],
            "sequenceKind": "temporal_sequence" if case.get("temporalSequence") or case["history"] == "continuous" else "frozen_static",
            "status": "measured" if accepted else "unavailable", "reasons": case_reasons,
            "requestedSamples": case["requestedSamples"], "rawSamples": len(samples),
            "warmupSamples": warmups, "acceptedIterations": [s["iteration"] for s in accepted],
            "excludedSamples": excluded, "creationExtent": capacity, "resourceExtents": resources,
            "evaluatedRects": regions, "evaluationsPerSample": len(regions), "evaluatedPixelsPerSample": area,
            "evaluatedGuideRects": guides, "evaluatedSourceRects": source_regions,
            "evaluatedAreaMeaning": "native API subrect request; private provider passes may use creation capacity",
            "providerFootprint": [{field: statistics_summary([sample["providerFootprint"][i][field] for sample in accepted])
                                   for field in FOOTPRINT_FIELDS} for i in range(len(regions))],
            "gpuMicroseconds": statistics_summary([s["gpuMicroseconds"] for s in accepted]),
            "batchTimingMeaning": "D3D12 NR batch includes cold creation; individual evaluation intervals exclude creation",
            "perCallGpuMicroseconds": [statistics_summary([s["evaluationGpuMicroseconds"][i] for s in accepted])
                                       for i in range(len(regions))],
            "logicalResourceBytes": logical_bytes,
            "memory": {k: statistics_summary(v) for k, v in memory.items()},
            "memoryMeaning": "bytes; all recorded samples including warmup/failures; DXGI process adapter usage, not provider-only allocations",
            "observedResetCount": sum(sample["reset"] for sample in samples),
            "observedCreatedFeatureCount": sum(sample["createdFeatureCount"] for sample in samples),
            "qualityAssessment": "not_established_by_throughput_samples"}


def comparison(left: dict, right: dict, summaries: dict) -> dict:
    reasons = []
    for field in ("route", "mode", "axis", "warmupIterations", *IDENTITY_FIELDS):
        if field not in left or field not in right or left[field] != right[field]:
            reasons.append("unmatched_or_missing_" + field)
    if left.get("sourceContentSha256") != right.get("sourceContentSha256") or not left.get("sourceContentSha256"):
        reasons.append("source_content_identity_not_matched")
    if left.get("temporalSequence", False) != right.get("temporalSequence", False):
        reasons.append("unmatched_sequence_policy")
    axis = left["axis"]
    a, b = summaries[left["id"]], summaries[right["id"]]
    if axis not in AXES:
        reasons.append("unsupported_comparison_axis")
    if axis != "history" and left["history"] != right["history"]:
        reasons.append("unmatched_history")
    if axis != "capacity" and (left["creationExtent"] != right["creationExtent"] or left["resourceExtents"] != right["resourceExtents"]):
        reasons.append("unmatched_creation_or_resource_capacity")
    if axis in {"mask_occupancy", "history"} and left["evaluatedRects"] != right["evaluatedRects"]:
        reasons.append("unmatched_evaluated_shapes")
    if axis == "capacity":
        if left["evaluatedSourceRects"] != right["evaluatedSourceRects"]:
            reasons.append("unmatched_source_windows")
        for field in ("evaluatedRects", "evaluatedGuideRects"):
            if any(x["width"] != y["width"] or x["height"] != y["height"]
                   for x, y in zip(left[field], right[field])):
                reasons.append("unmatched_" + field + "_shape")
    if axis in {"aspect_ratio", "call_count"}:
        area_a, area_b = a["evaluatedPixelsPerSample"], b["evaluatedPixelsPerSample"]
        tolerance = 0 if axis == "aspect_ratio" else 0.02
        if abs(area_a - area_b) > tolerance * max(area_a, area_b):
            reasons.append("evaluated_area_not_matched")
    if axis == "aspect_ratio" and any(x["baseX"] != y["baseX"] or x["baseY"] != y["baseY"]
                                       for x, y in zip(left["evaluatedRects"], right["evaluatedRects"])):
        reasons.append("aspect_ratio_offset_not_matched")
    if axis == "minimum_shape":
        if any(x["width"] != x["height"] or y["width"] != y["height"]
               or x["baseX"] != y["baseX"] or x["baseY"] != y["baseY"]
               for x, y in zip(left["evaluatedRects"], right["evaluatedRects"])):
            reasons.append("minimum_square_shape_or_offset_not_matched")
    if axis != "call_count" and left["evaluationsPerSample"] != right["evaluationsPerSample"]:
        reasons.append("unmatched_call_count")
    if axis in {"width", "height", "offset", "alignment"}:
        invariant = {"width": ("height", "baseX", "baseY"), "height": ("width", "baseX", "baseY"),
                     "offset": ("width", "height"), "alignment": ("width", "height")}[axis]
        if any(any(x[k] != y[k] for k in invariant) for x, y in zip(left["evaluatedRects"], right["evaluatedRects"])):
            reasons.append("geometry_changed_outside_axis")
    if left.get("temporalSequence") or right.get("temporalSequence") or left["history"] == "continuous" or right["history"] == "continuous":
        contexts = [left.get("contextIds"), right.get("contextIds")]
        valid_contexts = all(isinstance(ids, list) and len(ids) == case["evaluationsPerSample"]
                             and all(isinstance(identity, str) and bool(identity) for identity in ids)
                             and len(set(ids)) == len(ids) for case, ids in zip((left, right), contexts))
        if not valid_contexts or set(contexts[0]).intersection(contexts[1]):
            reasons.append("independent_contexts_not_proven")
        if (not left.get("initializationFingerprint")
                or left.get("initializationFingerprint") != right.get("initializationFingerprint")):
            reasons.append("equal_initialization_not_proven")
    if a["status"] != "measured" or b["status"] != "measured":
        reasons.append("one_or_both_cases_unmeasured")
    return {"left": left["id"], "right": right["id"], "axis": axis, "comparable": not reasons,
            "reasons": reasons, "qualityAssessment": "requires_separate_output_sequence_assessment",
            "scope": "coupled_square_dimension_probe" if axis == "minimum_shape" else
                     "native_NR_cost_only_not_CSX_GPU_composite_cost" if axis == "mask_occupancy" else "native_NR_cost",
            "meanGpuDeltaMicroseconds": None if reasons else b["gpuMicroseconds"]["mean"] - a["gpuMicroseconds"]["mean"]}


def report(results: dict) -> dict:
    finite_tree(results)
    require(isinstance(results, dict) and results.get("schema") == SCHEMA, "unsupported replay results schema")
    cases = results.get("cases", [])
    require(isinstance(cases, list) and len(cases) <= 256, "invalid replay case collection")
    unavailable = results.get("acquisitionUnavailable")
    if not cases and not unavailable:
        if results.get("status") == "input_validated_no_runtime_measurement":
            unavailable = "input_validated_only_no_runtime_measurement"
        elif results.get("status") == "inspection_complete_no_evaluation":
            unavailable = "runtime_and_sdk_inspected_no_native_evaluation"
        elif results.get("status") in {"failed", "unavailable", "bounded_deadline"} and isinstance(results.get("reason"), str) and results["reason"]:
            unavailable = "native_replay_" + results["status"] + ": " + results["reason"]
    require(cases or bool(unavailable), "no cases and no unavailability reason")
    if cases:
        for name in ("runtime", "adapter"):
            require(isinstance(results.get(name), dict) and bool(results[name]), "missing " + name + " identity")
        require(bool(results.get("captureManifest")), "missing capture manifest identity")
        require(sha256(results.get("captureManifestSha256")) and sha256(results.get("sourceContentSha256"))
                and sha256(results["runtime"].get("sha256")), "missing or invalid source/runtime SHA-256 identity")
        for case in cases:
            require(isinstance(case, dict) and case.get("sourceContentSha256") == results["sourceContentSha256"],
                    "case source content differs from captured bundle")
    summaries = [checked_case(case) for case in cases]
    by_id = {case["id"]: case for case in summaries}
    require(len(by_id) == len(summaries), "duplicate case identity")
    groups = {}
    for case in cases:
        if case.get("pairGroup"):
            groups.setdefault(case["pairGroup"], []).append(case)
    comparisons = [comparison(group[0], other, by_id) for group in groups.values() for other in group[1:]]
    counts = sorted({c["evaluationsPerSample"] for c in summaries if c["status"] == "measured"})
    return {"schema": "csx-nr-replay-report-v1", "rawResults": copy.deepcopy(results), "cases": summaries,
            "comparisons": comparisons, "observedSuccessfulCallCounts": counts,
            "instanceCapacityConclusion": "only tested successful configurations; no general NR instance capacity guarantee",
            "uncertainty": "Timing intervals assume independent stationary samples; serial correlation is not estimated. "
                           "They do not establish temporal quality, whole-frame cost, or production defaults.",
            "acquisitionUnavailable": unavailable,
            "measuredCaseCount": sum(c["status"] == "measured" for c in summaries)}


def markdown(value: dict) -> str:
    lines = ["# Native NR replay measurements", "", value["uncertainty"], ""]
    if value.get("acquisitionUnavailable"):
        lines += ["Acquisition unavailable: " + str(value["acquisitionUnavailable"]), ""]
    for history in ("static_reset", "continuous", "cold_create"):
        cases = [case for case in value["cases"] if case["history"] == history]
        if not cases:
            continue
        lines += ["## " + history, "", "| Route / case | Calls / pixels | Creation | n | Mean / median (us) | Stdev / range (us) | 95% mean interval (us) | Status |",
                  "| --- | ---: | --- | ---: | --- | --- | --- | --- |"]
        def number(n):
            return "unavailable" if n is None else f"{n:.3f}"
        def cell(text):
            return str(text).replace("|", "\\|").replace("\r", " ").replace("\n", " ")
        for case in cases:
            stats = case["gpuMicroseconds"]
            interval = stats["mean95Interval"]
            lines.append(f"| {case['route']} / {cell(case['id'])} ({case['sequenceKind']}) | {case['evaluationsPerSample']} / {case['evaluatedPixelsPerSample']} | "
                         f"{case['creationExtent']} | {stats['n']} | {number(stats['mean'])} / {number(stats['median'])} | "
                         f"{number(stats['sampleStdev'])} / {number(stats['minimum'])}–{number(stats['maximum'])} | "
                         f"{number(interval[0]) + '–' + number(interval[1]) if interval else 'unavailable'} | {case['status']} |")
        lines.append("")
        lines += ["| Case | Resource extents | Warmup / observed resets / creations | Logical resource bytes | DXGI local usage after, range (bytes) | Max modified pixels outside API rect, per call | Availability reasons |",
                  "| --- | --- | ---: | ---: | --- | --- | --- |"]
        for case in cases:
            local = case["memory"]["localUsageAfter"]
            reasons = case["reasons"] + sorted({reason for sample in case["excludedSamples"]
                                               for reason in sample["reasons"] if reason != "warmup"})
            lines.append(f"| {cell(case['id'])} | {cell(case['resourceExtents'])} | "
                         f"{case['warmupSamples']} / {case['observedResetCount']} / {case['observedCreatedFeatureCount']} | "
                         f"{case['logicalResourceBytes'] if case['logicalResourceBytes'] is not None else 'unavailable'} | "
                         f"{number(local['minimum'])}–{number(local['maximum'])} | "
                         f"{', '.join(number(f['modifiedOutsidePixels']['maximum']) for f in case['providerFootprint'])} | "
                         f"{cell('; '.join(dict.fromkeys(reasons))) or 'complete'} |")
        lines.append("")
    lines += ["The JSON report retains every raw sample, exclusion reason, evaluated shape, resource dimension, reset and creation observation, and memory sample.",
              "Logical resource bytes and DXGI process adapter usage have different scopes. No provider capacity or temporal-quality guarantee is inferred.", ""]
    lines += ["Area describes API subrects; private provider passes may still use creation capacity. Output sentinel footprints observe writes, not private inference operations.", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="New output directory; never overwrites prior evidence")
    args = parser.parse_args()
    try:
        payload = args.results.read_bytes()
        result = report(json.loads(payload.decode("utf-8-sig")))
        result["sourceReceipt"] = {"path": str(args.results.resolve()), "sha256": hashlib.sha256(payload).hexdigest(), "bytes": len(payload)}
        args.output.mkdir(parents=True, exist_ok=False)
        (args.output / "report.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        (args.output / "report.md").write_text(markdown(result), encoding="utf-8")
    except (OSError, ValueError, TransactionEvidenceError) as error:
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps({"report": str(args.output.resolve()), "measuredCases": result["measuredCaseCount"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
