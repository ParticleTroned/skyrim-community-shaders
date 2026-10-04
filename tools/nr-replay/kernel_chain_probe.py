"""Audit isolated stateless C kernel-chain forwarding and grouping experiments."""

from __future__ import annotations

import argparse
import hashlib
import math
from collections import Counter
from pathlib import Path
import json
import subprocess
import sys

from context_probe import EVIDENCE_ERRORS, _bounds, expanded
from native_handle_probe import assess_bracket
from packed_replay import (TOOL_SOURCES, digest, invoke, native_campaign_lock,
                           read, rectangle, require_idle_game, write)
from packed_report import (_json, _manifest, _rect, _repeat, _texture, finite_tree,
                           require, statistics_summary, uint)


SOURCE_NAMES = ("kernel_chain_probe.py", "native_handle_probe.py", "context_probe.py", *TOOL_SOURCES)
MAX_RETAINED_OUTPUT_BYTES = 256 * 1024 * 1024


def cases(owned, width, height):
    require(isinstance(owned, list) and 1 <= len(owned) <= 4, "kernel chain probe requires one to four regions")
    normalized = [_rect(rect) for rect in owned]
    for index, rect in enumerate(normalized):
        require(min(rect[2:]) >= 128, "kernel chain probe requires extents >=128")
        expanded(list(rect), 0, width, height)
        x, y, w, h = rect
        require(all(x + w <= a or a + c <= x or y + h <= b or b + d <= y
                    for a, b, c, d in normalized[:index]), "owned rectangles overlap")
    return [{"id": name, "kind": "separate", "rects": owned, "kernelChainMode": mode, "batchTimingOnly": True,
             "resultDirectory": "runs/" + name}
            for name, mode in (("baseline-before", "off"), ("forward", "forward"),
                               ("group", "group"), ("baseline-after", "off"))]


def source_contract(manifest, owned, samples):
    require(len(manifest["frames"]) == 1, "kernel chain probe holds exactly one captured frame constant")
    eyes = manifest["frames"][0]["eyes"]
    first = eyes[0]["color"]
    extent = [first["width"], first["height"]]
    require(all([eye[role]["width"], eye[role]["height"]] == extent
                for eye in eyes for role in ("color", "depth", "motion", "output")),
            "kernel chain probe requires equal captured resource grids in every eye")
    require(sum(rect[2] * rect[3] for rect in owned) * len(eyes) * 4 * samples <= MAX_RETAINED_OUTPUT_BYTES,
            "retained output evidence exceeds bounded report memory")
    return extent


def prepare(args):
    _bounds(args.repeats, args.samples, args.warmup, args.seconds)
    source, executable, runtime = (p.resolve(strict=True) for p in (args.manifest, args.replay, args.runtime))
    manifest, content = _manifest(source)
    require(digest(runtime) == manifest["runtime"]["sha256"].lower(), "provider differs from captured input")
    first = manifest["frames"][0]["eyes"][0]["color"]
    selected = cases(args.roi, first["width"], first["height"])
    extent = source_contract(manifest, args.roi, args.samples)
    root = args.output.resolve()
    require(not root.exists() and not args.output.is_symlink(), "preserve existing evidence; choose a new output")
    plan = {"schema": "csx-nr-kernel-chain-probe-v1", "status": "prepared",
            "sourceManifest": {"path": str(source), "sha256": digest(source)}, "sourceContentSha256": content,
            "replayExecutable": {"path": str(executable), "sha256": digest(executable)},
            "runtime": {"path": str(runtime), "sha256": digest(runtime)},
            "toolSources": {name: digest(Path(__file__).parent / name) for name in SOURCE_NAMES},
            "ownedRects": args.roi, "sourceExtent": extent, "cases": selected,
            "repeats": args.repeats, "samples": args.samples, "warmup": args.warmup,
            "secondsPerCase": args.seconds, "maximumBaselineDriftFraction": 0.05,
            "alternateOutputSentinel": True, "batchTimingOnly": True, "productionQualified": False}
    root.mkdir(parents=True, exist_ok=False)
    write(root / "plan.json", plan)
    return plan


def admit(plan, check_tools=False):
    finite_tree(plan)
    require(plan.get("schema") == "csx-nr-kernel-chain-probe-v1" and plan.get("status") == "prepared",
            "expected prepared kernel chain probe")
    _bounds(plan["repeats"], plan["samples"], plan["warmup"], plan["secondsPerCase"])
    require(plan.get("maximumBaselineDriftFraction") == 0.05 and plan.get("alternateOutputSentinel") is True
            and plan.get("productionQualified") is False and plan.get("batchTimingOnly") is True,
            "kernel chain admission policy changed")
    for key in ("sourceManifest", "replayExecutable", "runtime"):
        require(digest(plan[key]["path"]) == plan[key]["sha256"], key + " identity changed")
    manifest, content = _manifest(Path(plan["sourceManifest"]["path"]))
    require(content == plan["sourceContentSha256"] and manifest["runtime"]["sha256"].lower() == plan["runtime"]["sha256"],
            "captured input or provider changed")
    extent = source_contract(manifest, plan["ownedRects"], plan["samples"])
    require(plan["sourceExtent"] == extent and plan["cases"] == cases(plan["ownedRects"], *extent),
            "kernel chain geometry or execution plan changed")
    require(set(plan["toolSources"]) == set(SOURCE_NAMES), "incomplete tool source identity")
    if check_tools:
        require(all(digest(Path(__file__).parent / name) == value for name, value in plan["toolSources"].items()),
                "tool source changed; prepare a new plan")
    return manifest, content


def execution_health(value, directory, manifest, plan):
    """Check warmup as well as timed calls against immutable independent contexts."""
    eyes = manifest["frames"][0]["eyes"]
    rects = [_rect(rect) for rect in plan["ownedRects"]]
    slots = [region * 4 + eye for eye in range(len(eyes)) for region in range(len(rects))]
    extent = plan["sourceExtent"]
    require(value.get("creationExtent") == extent
            and value.get("resourceExtents") == {role: extent for role in ("color", "depth", "motion", "output")}
            and value.get("inputStoragePolicy") == "captured" and value.get("sharedInputs") is True
            and value.get("nativeHandlePolicy") == "independent" and value.get("nativeHandleSlots") == slots
            and value.get("nativeHandleCount") == len(slots)
            and value.get("nativeHandleMask") == sum(1 << slot for slot in slots)
            and value.get("nativeHandleReuseBarriers") == 0 and value.get("evaluationsPerSample") == len(slots)
            and [_rect(r) for r in value.get("evaluatedSourceRects", [])] == rects * len(eyes)
            and [_rect(r) for r in value.get("evaluatedGuideRects", [])] == rects * len(eyes),
            "full-grid independent-context execution differs")
    require(value.get("requestedSamples") == plan["samples"] and value.get("warmupIterations") == plan["warmup"]
            and len(value["samples"]) == plan["samples"] + plan["warmup"], "sample count differs")
    expected_inputs = [(eye, role, entry[role]["sha256"].lower()) for eye, entry in enumerate(eyes)
                       for role in ("color", "depth", "motion")]
    output_hashes = []
    for index, sample in enumerate(value["samples"]):
        require(sample.get("iteration") == index and sample.get("warmup") is (index < plan["warmup"])
                and sample.get("success") is True and sample.get("reset") is True
                and sample.get("evaluationCount") == len(slots)
                and sample.get("createdFeatureCount") == (len(slots) if index == 0 else 0),
                "warmup or measured execution is incomplete")
        calls = sample.get("runtimeCalls")
        require(isinstance(calls, list) and [call.get("slot") for call in calls] == slots
                and [call.get("nativeHandleSlot") for call in calls] == slots
                and all(call.get("evaluationAttempted") is True and call.get("evaluationSucceeded") is True
                        and call.get("createAttempted") is (index == 0) and call.get("createSucceeded") is (index == 0)
                        for call in calls), "native creation or independent handle routing differs")
        checks = sample.get("immutableInputChecks")
        require(isinstance(checks, list)
                and [(p.get("eye"), p.get("resource"), p.get("sha256")) for p in checks] == expected_inputs,
                "immutable color/depth/motion evidence differs")
        footprints = sample.get("providerFootprint")
        require(isinstance(footprints, list) and len(footprints) == len(slots)
                and all(p.get("modifiedOutsidePixels") == 0 and p.get("unchangedInsidePixels") == 0
                        and p.get("nonfiniteInsidePixels") == 0 and p.get("alternateBytePattern") is True for p in footprints),
                "owned output coverage, finiteness or outside-write evidence failed")
        outputs = sample.get("outputFiles")
        require(isinstance(outputs, list) and len(outputs) == len(slots)
                and sorted(o.get("slot") for o in outputs) == sorted(slots), "retained native output slots differ")
        hashes = {}
        for output in outputs:
            slot = output["slot"]
            expected = rects[slot // 4]
            require(output.get("scope") == "evaluated_rectangle"
                    and (output.get("width"), output.get("height")) == expected[2:], "retained output crop differs")
            hashes[str(slot)] = hashlib.sha256(_texture(directory, output, 28)).hexdigest()
        output_hashes.append(hashes)
        require(uint(sample.get("evalCpuMicroseconds"), 10**12), "native CPU evaluation time missing or invalid")
    require(output_hashes and all(value == output_hashes[0] for value in output_hashes),
            "stateless output is not repeatable within the process")
    return output_hashes[0]


def ordered_jobs(plan):
    return [(case, repeat) for repeat in range(plan["repeats"]) for case in plan["cases"]]


def validate_journal(plan, root, journal, loaded):
    require(journal.get("schema") == "csx-nr-kernel-chain-run-v1" and journal.get("status") == "complete",
            "kernel chain journal is incomplete")
    require(read(root / "plan.json") == plan and journal.get("planSha256") == digest(root / "plan.json"),
            "kernel chain journal plan identity differs")
    jobs, expected = journal.get("jobs"), ordered_jobs(plan)
    require(isinstance(jobs, list) and len(jobs) == len(expected), "kernel chain journal job count differs")
    for job, (case, repeat) in zip(jobs, expected):
        require(isinstance(job, dict) and job.get("case") == case["id"]
                and type(job.get("repeat")) is int and job["repeat"] == repeat and job.get("status") == "complete"
                and job.get("output") == str(root / case["resultDirectory"] / f"repeat-{repeat}")
                and job.get("resultsSha256") == loaded[(case["id"], repeat)]["resultSha256"],
                "kernel chain journal order, output or result identity differs")


def number(value, positive=False):
    return type(value) in (int, float) and math.isfinite(value) and (value > 0 if positive else value >= 0)


def chain_sample(receipt, mode, iteration, warmup, eyes, regions):
    """Reconstruct descriptor order, copied parameters and every flush boundary."""
    require(isinstance(receipt, dict) and receipt.get("mode") == mode
            and type(receipt.get("iteration")) is int and receipt["iteration"] == iteration and receipt.get("warmup") is warmup
            and receipt.get("failed") is False and receipt.get("reason") == ""
            and receipt.get("pendingDescriptors") == 0, "kernel chain sample incomplete or failed")
    events = receipt.get("events")
    require(isinstance(events, list) and 1 <= len(events) <= 131072, "kernel chain event stream missing or oversized")
    descriptors, command_lists, pending, submitted = {}, {}, [], []
    observed_calls = submitted_calls = multi_calls = cross_calls = parameter_bytes = 0
    max_descriptors = max_regions = 0
    api_cpu = 0.0
    reasons = Counter()
    for ordinal, event in enumerate(events):
        require(isinstance(event, dict) and type(event.get("ordinal")) is int and event["ordinal"] == ordinal
                and event.get("kind") in {"observed", "submitted", "boundary"}
                and isinstance(event.get("reason"), str) and bool(event["reason"])
                and uint(event.get("commandList"), 2**64 - 1)
                and type(event.get("status")) is int and event["status"] == 0 and number(event.get("apiCpuMicroseconds")),
                "kernel chain event identity, status or timing differs")
        ids, packets = event.get("descriptorIds"), event.get("descriptors")
        require(isinstance(ids, list) and isinstance(packets, list) and len(ids) == len(packets)
                and all(uint(identity, 32767) for identity in ids)
                and [packet.get("descriptorId") for packet in packets] == ids,
                "kernel chain descriptor IDs differ from owned packets")
        kind = event["kind"]
        if kind == "observed":
            require(1 <= len(ids) <= 64 and ids == list(range(len(descriptors), len(descriptors) + len(ids)))
                    and event["reason"] == "provider_launch" and event["commandList"] > 0,
                    "kernel chain observed descriptor order differs")
            require(mode != "forward" or not pending, "forward mode deferred a provider call")
            require(event.get("pendingDescriptorsBefore") == len(pending)
                    and event.get("pendingDescriptorsAfter") == len(pending) + len(ids),
                    "observed pending-descriptor counts differ")
            for identity, packet in zip(ids, packets):
                require(uint(packet.get("eye"), eyes - 1) and uint(packet.get("region"), regions - 1)
                        and uint(packet.get("function"), 2**64 - 1) and packet["function"] > 0
                        and uint(packet.get("originalParamsPointer"), 2**64 - 1) and packet["originalParamsPointer"] > 0
                        and uint(packet.get("dynamicSharedMemoryBytes"), 2**32 - 1)
                        and uint(packet.get("paramSize"), 4096) and packet["paramSize"] > 0,
                        "kernel chain descriptor metadata invalid")
                require(all(isinstance(packet.get(name), list) and len(packet[name]) == 3
                            and all(uint(value, 2**32 - 1) and value > 0 for value in packet[name])
                            for name in ("grid", "block")), "kernel chain dimensions invalid")
                encoded = packet.get("paramsHex")
                require(isinstance(encoded, str) and len(encoded) == packet["paramSize"] * 2,
                        "owned kernel parameter byte count differs")
                parameter = bytes.fromhex(encoded)
                require(len(parameter) == packet["paramSize"]
                        and hashlib.sha256(parameter).hexdigest() == packet.get("paramsSha256"),
                        "owned kernel parameter bytes/hash differ")
                parameter_bytes += len(parameter)
                require(parameter_bytes <= 64 * 1024 * 1024, "kernel parameter capture budget exceeded")
                descriptors[identity], command_lists[identity] = packet, event["commandList"]
            observed_calls += 1
            pending.extend(ids)
        elif kind == "submitted":
            require(ids and ids == pending and len(ids) <= 64
                    and all(packet == descriptors[identity] and command_lists[identity] == event["commandList"]
                            for identity, packet in zip(ids, packets)), "submission reordered or changed owned kernel descriptors")
            require(mode != "forward" or event["reason"] == "forward", "forward mode changed submission boundary")
            require(event.get("pendingDescriptorsBefore") == len(pending) and event.get("pendingDescriptorsAfter") == 0,
                    "submitted pending-descriptor counts differ")
            pending.clear()
            submitted.extend(ids)
            submitted_calls += 1
            multi_calls += len(ids) > 1
            distinct_regions = len({(packet["eye"], packet["region"]) for packet in packets})
            cross_calls += distinct_regions > 1
            max_descriptors, max_regions = max(max_descriptors, len(ids)), max(max_regions, distinct_regions)
            api_cpu += event["apiCpuMicroseconds"]
            reasons[event["reason"]] += 1
        else:
            require(not ids and not pending and event.get("pendingDescriptorsAfter") == 0
                    and uint(event.get("pendingDescriptorsBefore"), 64),
                    "pending kernels crossed a command boundary")
            if event["pendingDescriptorsBefore"]:
                previous = events[ordinal - 1] if ordinal else {}
                require(previous.get("kind") == "submitted" and previous.get("reason") == event["reason"]
                        and len(previous.get("descriptorIds", [])) == event["pendingDescriptorsBefore"],
                        "command boundary lacks matching pre-command flush")
    require(observed_calls > 0 and not pending and submitted == list(range(len(descriptors)))
            and events[-1]["kind"] == "boundary" and events[-1]["reason"] == "command_list_end",
            "kernel chain interception or final retirement incomplete")
    counts = {"observedCalls": observed_calls, "observedDescriptors": len(descriptors),
              "submittedCalls": submitted_calls, "submittedDescriptors": len(submitted),
              "multiDescriptorCalls": multi_calls, "maxSubmittedDescriptors": max_descriptors,
              "crossRegionCalls": cross_calls, "maxRegionsPerSubmission": max_regions,
              "parameterBytes": parameter_bytes}
    require(all(type(receipt.get(key)) is int and receipt[key] == value for key, value in counts.items())
            and receipt.get("flushReasons") == dict(reasons), "kernel chain counters differ from complete event evidence")
    require(mode != "forward" or observed_calls == submitted_calls, "forward call count differs")
    require(number(receipt.get("apiCpuMicroseconds")) and number(receipt.get("hookCpuMicroseconds"))
            and math.isclose(receipt["apiCpuMicroseconds"], api_cpu, rel_tol=1e-9, abs_tol=1e-6),
            "kernel chain CPU timing differs from event evidence")
    return {**counts, "apiCpuMicroseconds": api_cpu, "hookCpuMicroseconds": receipt["hookCpuMicroseconds"],
            "groupingAchieved": mode == "group" and submitted_calls < observed_calls and multi_calls > 0,
            "independentRegionBatchingAchieved": mode == "group" and cross_calls > 0,
            "descriptorPlanSha256": hashlib.sha256(json.dumps(
                [[packet[key] for key in ("eye", "region", "grid", "block", "dynamicSharedMemoryBytes", "paramSize")]
                 for packet in descriptors.values()], separators=(",", ":")).encode("ascii")).hexdigest()}


def require_same_descriptor_plan(forward, grouped):
    keys = ("observedCalls", "observedDescriptors", "parameterBytes", "descriptorPlanSha256")
    require(len(forward) == len(grouped) and all(all(a[key] == b[key] for key in keys)
                                               for a, b in zip(forward, grouped)),
            "forward/group observed descriptor shape or count differs")


def chain_receipts(raw, mode, provider_hash):
    receipt = raw.get("kernelChainExperiment")
    samples = raw["cases"][0]["samples"]
    if mode == "off":
        require(receipt == {"requested": False} and all("kernelChain" not in sample for sample in samples),
                "baseline must have no kernel chain hook")
        return {"mode": "off", "groupingAchieved": False, "independentRegionBatchingAchieved": False}
    require(isinstance(receipt, dict) and receipt.get("requested") is True and receipt.get("mode") == mode
            and receipt.get("providerDiskSha256") == provider_hash and receipt.get("performanceQualified") is False
            and receipt.get("inMemoryState") == "original_provider_cache"
            and receipt.get("groupingScope") == "original_order_between_intercepted_command_boundaries",
            "kernel chain hook identity/state differs")
    transitions = receipt.get("transitions")
    require(isinstance(transitions, list) and len(transitions) == 2, "expected exactly one hook and restoration")
    applied, restored = transitions
    require(applied.get("state") == "applied_own_replay_process_only"
            and applied.get("providerDiskSha256") == provider_hash and applied.get("mode") == mode
            and applied.get("codeSha256") == "0d0543585e6765a87886678efbdb6462f02be2a3b6c26c9454c0e9f304194dc5"
            and applied.get("cacheRva") == 0x1157D08
            and uint(applied.get("originalCachePointer"), 2**64 - 1)
            and uint(applied.get("realLaunchPointer"), 2**64 - 1) and applied["realLaunchPointer"] > 0,
            "kernel chain hook application proof differs")
    require(restored.get("state") == "restored" and restored.get("gpuIdleProven") is True
            and restored.get("originalProtectionRestored") is True
            and restored.get("restoredCodeSha256") == applied["codeSha256"]
            and restored.get("restoredCachePointer") == applied["originalCachePointer"],
            "kernel chain restoration lacks idle/protection/code/cache proof")
    value = raw["cases"][0]
    eyes = value["logicalEyeCount"]
    regions = len(value["evaluatedRects"]) // eyes
    records = [chain_sample(sample.get("kernelChain"), mode, sample["iteration"], sample["warmup"], eyes, regions)
               for sample in samples]
    steady = [record for sample, record in zip(samples, records) if not sample["warmup"]]
    require(steady, "kernel chain measured samples missing")
    return {"mode": mode, "sampleCounters": records,
            "groupingAchieved": all(record["groupingAchieved"] for record in steady),
            "independentRegionBatchingAchieved": all(record["independentRegionBatchingAchieved"] for record in steady)}


def admitted_repeat(plan, case, root, repeat, manifest, content):
    directory = root / case["resultDirectory"] / f"repeat-{repeat}"
    raw = _json(directory / "results.json")
    require(raw.get("providerFloorExperiment") == {"requested": False}, "provider floor experiment must remain disabled")
    result = _repeat(case, directory, manifest, Path(plan["sourceManifest"]["path"]), content,
                     plan["replayExecutable"]["sha256"], [tuple(rect) for rect in plan["ownedRects"]])
    require(result["alternateOutputSentinel"] is True and len(result["steady"]) == plan["samples"],
            "steady output sentinel or sample count differs")
    value = raw["cases"][0]
    result["outputHashes"] = execution_health(value, directory, manifest, plan)
    result["kernelChain"] = chain_receipts(raw, case["kernelChainMode"], plan["runtime"]["sha256"])
    by_iteration = {sample["iteration"]: sample for sample in value["samples"]}
    for sample in result["steady"]:
        raw_sample = by_iteration[sample["iteration"]]
        require(number(raw_sample.get("submissionCpuMicroseconds"), positive=True)
                and raw_sample.get("submissionCpuScope") ==
                "native_evaluation_loop_and_final_chain_flush_excludes_receipt_encoding_gpu_wait_and_readback",
                "submission CPU time or scope missing or invalid")
        sample["nativeCpuMicroseconds"] = raw_sample["evalCpuMicroseconds"]
        sample["submissionCpuMicroseconds"] = raw_sample["submissionCpuMicroseconds"]
    return result


def assess_cpu(before, candidate, after):
    """Submission time includes deferred flush; native evaluation time is diagnostic."""
    record = {}
    for field in ("submissionCpuMicroseconds", "nativeCpuMicroseconds"):
        values = {key: statistics_summary([sample[field] for sample in samples])
                  for key, samples in (("before", before), ("candidate", candidate), ("after", after))}
        a, b = (values[key]["mean"] for key in ("before", "after"))
        drift = abs(a - b) / ((a + b) / 2) if a > 0 and b > 0 else None
        record[field] = {"records": values, "baselineDriftFraction": drift,
                         "timingStable": drift is not None and drift <= 0.05}
    return record


def summarize(plan, root):
    report = {"schema": "csx-nr-kernel-chain-report-v1", "status": "failed", "errors": [],
              "cases": [], "brackets": [], "productionQualified": False,
              "strictRgbaEquivalent": None, "groupingAchieved": False,
              "independentRegionBatchingQualified": False,
              "gpuTimingQualified": False, "cpuTimingQualified": False,
              "timingScope": "submission CPU includes evaluation and deferred flush, excluding GPU wait and readback; only whole-submission GPU intervals are measured, per-evaluation GPU timestamps explicitly disabled",
              "qualityScope": "exact unchanged-context native RGBA crops only; no temporal or composed-image qualification"}
    try:
        manifest, content = admit(plan)
        loaded = {}
        for case in plan["cases"]:
            entry = {"id": case["id"], "mode": case["kernelChainMode"], "repeats": []}
            report["cases"].append(entry)
            for repeat in range(plan["repeats"]):
                try:
                    result = admitted_repeat(plan, case, root, repeat, manifest, content)
                    loaded[(case["id"], repeat)] = result
                    entry["repeats"].append({"repeat": repeat, "status": "complete", "resultsSha256": result["resultSha256"],
                                             "kernelChain": result["kernelChain"]})
                except EVIDENCE_ERRORS as error:
                    report["errors"].append(f"{case['id']} repeat-{repeat}: {error}")
                    entry["repeats"].append({"repeat": repeat, "status": "failed_or_missing", "reason": str(error)})
        journal = read(root / "run.json") if (root / "run.json").exists() else {}
        report["executionStatus"] = journal.get("status", "journal_missing")
        validate_journal(plan, root, journal, loaded)
        first_hashes = loaded[("baseline-before", 0)]["outputHashes"]
        repeatable = all(result["outputHashes"] == first_hashes for (identity, _), result in loaded.items()
                         if identity.startswith("baseline-"))
        report["baselineRepeatableAcrossProcesses"] = repeatable
        for repeat in range(plan["repeats"]):
            require_same_descriptor_plan(loaded[("forward", repeat)]["kernelChain"]["sampleCounters"],
                                         loaded[("group", repeat)]["kernelChain"]["sampleCounters"])
            before, after = (loaded[(key, repeat)]["steady"] for key in ("baseline-before", "baseline-after"))
            for identity in ("forward", "group"):
                candidate = loaded[(identity, repeat)]
                gpu = assess_bracket(before, candidate["steady"], after, batch_timing_only=True)
                cpu = assess_cpu(before, candidate["steady"], after)
                exact = repeatable and candidate["outputHashes"] == first_hashes and gpu["strictRgbaEquivalent"]
                achieved = identity == "group" and candidate["kernelChain"]["groupingAchieved"]
                gpu_qualified = exact and achieved and gpu["timingQualified"]
                cpu_qualified = exact and achieved and cpu["submissionCpuMicroseconds"]["timingStable"]
                gpu["timingQualified"] = gpu_qualified
                if not gpu_qualified:
                    gpu["candidateBatchMeanDeltaPercent"] = None
                submission = cpu["submissionCpuMicroseconds"]["records"]
                cpu_delta = ((submission["candidate"]["mean"] /
                              ((submission["before"]["mean"] + submission["after"]["mean"]) / 2) - 1) * 100
                             if cpu_qualified else None)
                report["brackets"].append({"repeat": repeat, "mode": identity, "gpu": gpu, "cpu": cpu,
                                           "strictRgbaEquivalent": exact, "groupingAchieved": achieved,
                                           "independentRegionBatchingAchieved": candidate["kernelChain"]["independentRegionBatchingAchieved"],
                                           "gpuTimingQualified": gpu_qualified, "cpuTimingQualified": cpu_qualified,
                                           "candidateBatchGpuDeltaPercent": gpu["candidateBatchMeanDeltaPercent"] if gpu_qualified else None,
                                           "candidateSubmissionCpuDeltaPercent": cpu_delta})
        report["strictRgbaEquivalent"] = all(b["strictRgbaEquivalent"] for b in report["brackets"])
        groups = [b for b in report["brackets"] if b["mode"] == "group"]
        report["groupingAchieved"] = all(b["groupingAchieved"] for b in groups)
        report["independentRegionBatchingQualified"] = (report["strictRgbaEquivalent"]
                                                          and all(b["independentRegionBatchingAchieved"] for b in groups))
        report["gpuTimingQualified"] = report["strictRgbaEquivalent"] and all(b["gpuTimingQualified"] for b in groups)
        report["cpuTimingQualified"] = report["strictRgbaEquivalent"] and all(b["cpuTimingQualified"] for b in groups)
        report["status"] = ("output_mismatch" if not report["strictRgbaEquivalent"] else
                            "complete" if report["independentRegionBatchingQualified"] else
                            "grouped_within_region_only" if report["groupingAchieved"] else "not_groupable")
    except EVIDENCE_ERRORS as error:
        report["errors"].append(str(error))
    return report


def execute(plan, root):
    manifest, content = admit(plan, check_tools=True)
    jobs = ordered_jobs(plan)
    journal_path = root / "run.json"
    require(not journal_path.exists() and all(not (root / case["resultDirectory"] / f"repeat-{repeat}").exists()
                                             for case, repeat in jobs), "existing run preserved; prepare a new probe")
    journal = {"schema": "csx-nr-kernel-chain-run-v1", "status": "running",
               "planSha256": digest(root / "plan.json"), "jobs": []}
    write(journal_path, journal)
    reference_hashes = None
    forward_records = {}
    try:
        for case, repeat in jobs:
            require_idle_game()
            admit(plan, check_tools=True)
            output = root / case["resultDirectory"] / f"repeat-{repeat}"
            job = {"case": case["id"], "repeat": repeat, "output": str(output), "status": "running"}
            journal["jobs"].append(job)
            write(journal_path, journal)
            print(f"{case['id']} repeat {repeat}", flush=True)
            arguments = [plan["replayExecutable"]["path"], "--manifest", plan["sourceManifest"]["path"],
                         "--runtime", plan["runtime"]["path"], "--output", output,
                         "--rects", json.dumps(case["rects"]), "--alternate-output-sentinel", "--batch-timing-only",
                         "--samples", plan["samples"], "--warmup", plan["warmup"], "--seconds", plan["secondsPerCase"]]
            if case["kernelChainMode"] != "off":
                arguments += ["--experimental-kernel-chain", case["kernelChainMode"]]
            invoke(arguments, output, plan["secondsPerCase"])
            result = admitted_repeat(plan, case, root, repeat, manifest, content)
            admit(plan, check_tools=True)
            job.update(status="complete", resultsSha256=result["resultSha256"])
            write(journal_path, journal)
            if reference_hashes is None:
                reference_hashes = result["outputHashes"]
            require(result["outputHashes"] == reference_hashes, "exact native RGBA mismatch; stopped further processes")
            if case["kernelChainMode"] == "forward":
                forward_records[repeat] = result["kernelChain"]["sampleCounters"]
            elif case["kernelChainMode"] == "group":
                require_same_descriptor_plan(forward_records[repeat], result["kernelChain"]["sampleCounters"])
        journal["status"] = "complete"
    except Exception as error:
        journal.update(status="failed", reason=str(error))
        if journal["jobs"] and journal["jobs"][-1]["status"] == "running":
            journal["jobs"][-1].update(status="failed", reason=str(error))
        raise
    finally:
        write(journal_path, journal)
        write(root / "summary.json", summarize(plan, root))
    return journal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "replay", "runtime", "output"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--roi", action="append", type=rectangle)
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.report:
        root = args.report.resolve(strict=True).parent
        write(root / "summary.json", summarize(read(args.report), root))
        return
    require(all((args.manifest, args.replay, args.runtime, args.output, args.roi)),
            "manifest, replay, runtime, output and roi required")
    plan = prepare(args)
    if not args.prepare_only:
        with native_campaign_lock():
            execute(plan, args.output.resolve())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Kernel chain probe stopped: {error}", file=sys.stderr)
        sys.exit(1)
