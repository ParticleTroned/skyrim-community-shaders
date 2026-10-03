#!/usr/bin/env python3
"""Audit matched NR cost brackets from immutable screenshot execution receipts.

This offline admission report does not install a runtime cost profile. Native
queue timing and inclusive D3D11 timing remain separate observations.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys

from replay_report import finite_tree, statistics_summary
from transaction_evidence import TransactionEvidenceError, finite, join_execution_evidence, require


def digest(value: object) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def stats(values: list[float]) -> dict:
    result = statistics_summary(values)
    ordered = sorted(values)
    result["p95"] = ordered[(95 * len(ordered) + 99) // 100 - 1] if ordered else None
    return result


def duration(value: dict, field: str, clock: str) -> float | None:
    if value.get("state") not in ("complete", "ready") or value.get("clock") != clock:
        return None
    result = value.get(field)
    require(finite(result), "complete timing has no finite duration")
    return result


def final_plan(execution: dict) -> dict:
    """Keys retain evaluated shape, capacity, guide phase, reset and copy work."""
    regions = []
    for region in execution["regions"]:
        require(region.get("evaluationSucceeded") is True and region.get("privateOutputCommitted") is True,
                "unsuccessful physical evaluation")
        descriptor = {key: copy.deepcopy(region.get(key)) for key in (
            "eye", "physicalSlot", "nrInput", "nrOutput", "nrDepthGuide", "nrMotionGuide", "controlMask",
            "nativeLayout", "effectiveReset", "rebuildReasonFlags", "featureUpscaling",
            "copiedLogicalBytes", "inputTransportOwnerSlot")}
        # Semantic support can move inside an unchanged final provider footprint.
        descriptor["roi"] = {key: copy.deepcopy(region.get("roi", {}).get(key)) for key in (
            "allocationCapacity", "inferenceContext", "ownedOutput", "temporalEnvelope", "contextPolicy", "coordinateDomain")}
        regions.append(descriptor)
    return {"route": execution["route"], "regions": regions,
            "calls": execution["actualEvaluationCount"], "capacityFallback": execution.get("capacityFallback"),
            "transport": execution.get("sourceTransport"), "color": execution.get("colourExposureConfiguration")}


def summarize(manifest: dict) -> dict:
    """Keep unique raw observations, and reject incomplete joins instead of filling zeros."""
    finite_tree(manifest)
    require(manifest.get("state") == "final" and manifest.get("terminalOutcome") == "completed"
            and manifest.get("continuity", {}).get("complete") is True, "sequence is incomplete")
    children = manifest.get("children")
    require(isinstance(children, list) and 0 < len(children) <= 4096, "invalid sequence size")
    count = len(children)
    counts = manifest.get("counts", {})
    require(all(type(counts.get(key)) is int and counts[key] == count
                for key in ("requested", "scheduled", "acquired", "written"))
            and all(type(counts.get(key)) is int and counts[key] == 0
                    for key in ("cancelled", "dropped", "failed", "inFlight")), "partial sequence membership")
    require(manifest.get("effective", {}).get("frameCount") == count
            and manifest["continuity"].get("requested") == count
            and manifest["continuity"].get("acquired") == count, "sequence count disagreement")
    require([child.get("ordinal") for child in children] == list(range(1, count + 1))
            and all(child.get("state") == "completed" for child in children), "incomplete child order/state")
    require(isinstance(manifest.get("producer"), dict) and bool(manifest["producer"].get("buildId")),
            "missing producer build identity")
    records, seen, plans, configurations, cameras = [], set(), {}, {}, {}
    for child in children:
        actual = child["actual"]
        acquisition = actual["acquisition"]["nrEvidence"]
        joined = join_execution_evidence(acquisition, actual.get("captureDiagnostics"))
        require(joined is not None, "missing frozen execution descriptor")
        evidence = joined["executionEvidence"]
        require(not evidence.get("droppedStageCount") and not evidence.get("executionEvidenceFailures")
                and not evidence.get("sourceEvidenceFailures"), "incomplete producer evidence")
        identity = tuple(evidence[key] for key in ("captureEpoch", "sourceTransactionId", "publicationSequence",
                                                   "frame", "sourceWorldFrame", "route"))
        source_identity = (identity[0], identity[1], identity[5])
        require(source_identity not in seen, "duplicate producer transaction")
        require(not records or identity[3] > records[-1]["identity"][3], "nonchronological source frames")
        seen.add(source_identity)
        if not joined["available"] or joined["companionState"] != "joined":
            records.append({"identity": identity, "calls": None, "pixels": None, "nativeMs": None,
                            "wholeNrMs": None, "reason": joined["reason"] or joined["companionReason"] or
                            "missing_finalized_companion"})
            continue
        configuration = copy.deepcopy(acquisition["configuration"])
        configuration["upscaling"].pop("neuralCharacterRegionLimit", None)
        configurations[digest(configuration)] = configuration
        camera = copy.deepcopy(actual["acquisition"].get("cameraEvidence", {}))
        require(camera.get("available") is True, "camera provenance unavailable")
        camera.pop("sourceWorldFrame", None)
        cameras[digest(camera)] = camera
        batches = evidence["executions"]
        require(len(batches) <= 1, "multiple batches require explicit nonoverlap accounting")
        if not batches:
            require(all(outcome == "NoWork" for outcome in evidence["workOutcome"]), "zero calls without NoWork")
            records.append({"identity": identity, "calls": 0, "pixels": 0, "nativeMs": None,
                            "wholeNrMs": None, "reason": "NoWork_not_a_timing_sample"})
            continue
        batch = batches[0]
        require(batch.get("succeeded") is True and not batch.get("transportBypass"), "bypass or failed batch")
        plan = final_plan(batch)
        key = digest(plan)
        plans[key] = plan
        native = duration(batch.get("timing", {}).get("aggregateEvaluationGpu", {}),
                          "microseconds", "d3d12_nr_queue")
        whole = duration(batch.get("timing", {}).get("wholeNrLegacy", {}).get("gpu", {}),
                         "inclusiveMs", "d3d11_context")
        records.append({"identity": identity, "planKey": key, "calls": batch["actualEvaluationCount"],
                        "pixels": batch["activeEvaluationPixels"], "nativeMs": native / 1000 if native is not None else None,
                        "wholeNrMs": whole, "reason": "" if native is not None and whole is not None else "timing_unavailable"})
    reasons = sorted({record["reason"] for record in records if record["reason"]})
    if len(configurations) != 1:
        reasons.append("configuration_changed_in_window")
    if len(cameras) != 1:
        reasons.append("camera_changed_in_window")
    if len(plans) != 1:
        reasons.append("final_geometry_reset_or_transport_changed")
    if len(records) < 30:
        reasons.append("fewer_than_30_unique_observations")
    for metric in ("nativeMs", "wholeNrMs"):
        if any(record[metric] is None for record in records):
            reasons.append(metric + "_incomplete")
    return {"producer": manifest.get("producer"), "records": records, "plans": plans,
            "configurations": configurations, "cameras": cameras, "reasons": reasons,
            "statistics": {metric: stats([r[metric] for r in records if r[metric] is not None])
                           for metric in ("nativeMs", "wholeNrMs")}}


def bracket(before: dict, candidate: dict, after: dict, *, maximum_drift: float = .05) -> dict:
    """A complete A/B/A bracket supports a local delta, never a universal cost fit."""
    require(finite(maximum_drift) and 0 < maximum_drift <= .1, "invalid baseline drift tolerance")
    runs = (before, candidate, after)
    reasons = [f"{index}:{reason}" for index, run in enumerate(runs) for reason in run["reasons"]]
    for key in ("producer", "configurations", "cameras"):
        if any(run[key] != before[key] for run in runs[1:]):
            reasons.append("unmatched_" + key)
    if before["plans"] != after["plans"]:
        reasons.append("baseline_final_plan_not_restored")
    transaction_sets = [{(record["identity"][0], record["identity"][1], record["identity"][5])
                         for record in run["records"]} for run in runs]
    if any(transaction_sets[i] & transaction_sets[j] for i in range(3) for j in range(i)):
        reasons.append("reused_observations_across_bracket")
    if not (before["records"][-1]["identity"][3] < candidate["records"][0]["identity"][3]
            and candidate["records"][-1]["identity"][3] < after["records"][0]["identity"][3]):
        reasons.append("baseline_bracket_not_chronological")
    metrics = {}
    for metric in ("nativeMs", "wholeNrMs"):
        a, b, c = (run["statistics"][metric] for run in runs)
        means = (a["mean"], b["mean"], c["mean"])
        if any(value is None for value in means) or min(means[0], means[2]) <= 0:
            metrics[metric] = {"baselineDriftFraction": None, "meanDeltaMs": None}
            reasons.append(metric + "_unavailable")
            continue
        drift = abs(means[0] - means[2]) / min(means[0], means[2])
        if drift > maximum_drift:
            reasons.append(metric + "_baseline_unstable")
        metrics[metric] = {"baselineDriftFraction": drift,
                           "observedMeanDeltaMs": means[1] - (means[0] + means[2]) / 2}
    for metric in metrics.values():
        metric["meanDeltaMs"] = None if reasons else metric["observedMeanDeltaMs"]
    return {"comparable": not reasons, "reasons": sorted(set(reasons)), "metrics": metrics,
            "maximumBaselineDriftFraction": maximum_drift,
            "automaticProfileAdoption": False,
            "profileRejectionReasons": sorted(set(reasons + [
                "whole_frame_cpu_critical_path_and_residency_not_joined",
                "held_out_prediction_and_transition_cost_not_qualified",
                "exact_backend_runtime_gpu_driver_profile_not_qualified",
                "camera_and_settings_equality_do_not_prove_identical_scene_content"])),
            "scope": "observed_final_plans_only_no_interpolation_or_runtime_policy_change"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", type=Path, nargs=3, metavar="A_B_A")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        raw = [path.read_bytes() for path in args.manifests]
        runs = [summarize(json.loads(data.decode("utf-8-sig"))) for data in raw]
        result = {"schema": "csx-nr-final-plan-cost-bracket-v1", "runs": runs,
                  "bracket": bracket(*runs), "receipts": [
                      {"path": str(path.resolve()), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
                      for path, data in zip(args.manifests, raw)]}
        args.output.mkdir(parents=True, exist_ok=False)
        for index, data in enumerate(raw):
            (args.output / f"raw-{index}.json").write_bytes(data)
        (args.output / "report.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        print(json.dumps(result["bracket"]))
    except (OSError, ValueError, KeyError, TypeError, TransactionEvidenceError) as error:
        print(str(error), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
