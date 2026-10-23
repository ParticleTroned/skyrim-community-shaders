"""Validate frozen Reflex journals without turning readiness into efficacy."""

import math


class EvidenceError(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise EvidenceError(reason)


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate(pages, mode, expected_build):
    """Validate every page and stream; retain quality failures as limitations."""
    require(bool(pages), "missing export")
    first = pages[0]
    required = {"captureId", "schema", "offset", "total", "counts", "records", "nextOffset",
                "boundaries", "condition", "quality", "scope", "termination", "workerError",
                "coverage", "recording", "timingDefinitions", "preferencesChanged", "controlCallsDuringCapture"}
    require(required <= first.keys(), "missing export metadata")
    require(first["schema"] == 1, "unsupported capture schema")
    require(first["environment"]["producer"]["buildId"] == expected_build, "producer mismatch")
    counts = first["counts"]
    require(set(counts) == {"application", "native", "compositor", "readback"}, "unknown streams")
    require(all(type(n) is int and n >= 0 for n in counts.values()), "invalid counts")
    require(sum(counts.values()) == first["total"], "count total mismatch")
    cursor = 0
    streams = {name: [] for name in counts}
    for index, page in enumerate(pages):
        require(required <= page.keys(), "missing page metadata")
        for field in required - {"offset", "records", "nextOffset"}:
            require(page[field] == first[field], f"frozen {field} changed during export")
        require(page["offset"] == cursor, "export gap, overlap, or reordered pages")
        require(len(page["records"]) <= 256, "page exceeds registered limit")
        for row in page["records"]:
            require(set(row) == {"stream", "ordinal", "data"}, "malformed record envelope")
            require(row["stream"] in streams, "unknown stream")
            destination = streams[row["stream"]]
            require(row["ordinal"] == len(destination), "missing/duplicated stream ordinal")
            destination.append(row["data"])
        cursor += len(page["records"])
        expected_next = cursor if cursor < first["total"] else None
        require(page["nextOffset"] == expected_next, "invalid export continuation")
        require(index == len(pages) - 1 or expected_next is not None, "extra pages after terminal page")
    require(cursor == first["total"] and pages[-1]["nextOffset"] is None, "incomplete pagination")
    require(all(len(streams[name]) == count for name, count in counts.items()), "stream count mismatch")
    b = first["boundaries"]
    start, stop, freeze = (b[k] for k in ("applicationAdmissionStartQpc", "applicationAdmissionStopQpc", "freezeQpc"))
    frequency = b["qpcFrequency"]
    valid_window = all(finite(v) and v > 0 for v in (start, stop, freeze, frequency)) and start <= stop <= freeze
    require(bool(b["clockRelationship"]), "missing compositor clock limitation")
    limitations = []
    if not valid_window:
        limitations.append("unavailable or invalid capture clock/boundaries")
    if first.get("preferencesChanged"):
        limitations.append("requested preferences changed during acquisition")
    if first.get("controlCallsDuringCapture", 0):
        limitations.append("control/inspection work overlapped acquisition")
    quality = first["quality"]
    counters = {"overflow", "contention", "unfinished", "appGaps", "appDuplicates", "invalidClocks", "invalidFrames",
                "sourceLost", "resets", "invalidCompositor", "sourceFailures", "missingCompletions"}
    require(counters | {"complete", "missingTail", "postrollTimedOut"} <= quality.keys(), "missing quality counters")
    require(all(type(quality[key]) is int and quality[key] >= 0 for key in counters), "invalid quality counters")
    require(all(type(quality[key]) is bool for key in ("complete", "missingTail", "postrollTimedOut")), "invalid quality flags")
    computed_complete = not (any(quality[key] for key in counters) or quality["missingTail"] or quality["postrollTimedOut"])
    require(quality["complete"] == computed_complete, "complete flag conceals quality failures")
    for stream in counts:
        require(first["coverage"][stream]["count"] == counts[stream], "coverage/count mismatch")
    condition = first["condition"]
    require({"requested", "backend", "acceptedCondition", "lastSettingsSubmission", "frameGenerationConstraint", "qpc", "gameHour", "probe"} <= condition.keys(), "missing condition receipt")
    if not condition["probe"]:
        request, backend, native = condition["requested"], condition["backend"], condition["lastSettingsSubmission"]
        require({"enabled", "boost", "capped", "fps", "markers"} <= request.keys(), "missing exact requested preferences")
        if not condition["acceptedCondition"] or condition["frameGenerationConstraint"] or not native:
            limitations.append("condition lacked confirmed unconstrained native acceptance")
        else:
            require({"version", "size", "bLowLatencyMode", "bLowLatencyBoost", "minimumIntervalUs", "bUseMarkersToOptimize", "bUseMinQueueTime", "result", "generation", "enterQpc", "exitQpc"} <= native.keys(), "missing actual pre-window settings arguments")
            expected_mode = 0 if not request["enabled"] else 2 if request["boost"] else 1
            cap = math.floor(1000000 / min(max(request["fps"], 20), 240) + 0.5) if request["capped"] else 0
            if native["result"] != 0 or native["bLowLatencyMode"] != (expected_mode != 0) or native["bLowLatencyBoost"] != (expected_mode == 2) or native["minimumIntervalUs"] != cap:
                limitations.append("submitted settings do not match the requested condition")
            if native["bUseMarkersToOptimize"] or backend.get("accepted") != {"mode": expected_mode, "minimumIntervalUs": cap, "useMarkersToOptimize": False}:
                limitations.append("accepted native options do not match the condition")
            if native["generation"] != backend["generation"] or not finite(native["exitQpc"]) or not finite(condition["qpc"]) or native["exitQpc"] > condition["qpc"] or valid_window and condition["qpc"] > start:
                limitations.append("settings receipt not valid before window on the measured generation")
    require(first["recording"]["mode"] == mode, "recorded observer mode mismatch")
    sequences = set()
    for name in ("application", "native", "readback"):
        for row in streams[name]:
            require({"sequence", "completed", "completionReason"} <= row.keys(), "missing completion identity")
            require(type(row["sequence"]) is int and row["sequence"] > 0 and row["sequence"] not in sequences, "invalid or duplicate sequence")
            sequences.add(row["sequence"])
            require(type(row["completed"]) is bool, "invalid completion flag")
            if not row["completed"]:
                require(bool(row["completionReason"]) and row["exitQpc"] is None, "unfinished operation has a fabricated exit")
                limitations.append(f"{name} completion missing; in flight or completion dropped")
                if name != "application":
                    require(row["result"] is None and row["enterQpc"] is None, "unfinished native operation has a fabricated result/entry")
    native_by_application = {}
    for item in streams["native"]:
        require({"admittedQpc", "admissionSource", "operation", "applicationSequence"} <= item.keys(), "missing native admission fields")
        if item["admissionSource"] == "application_frame":
            native_by_application.setdefault(item["applicationSequence"], []).append(item)
    previous = None
    previous_entry = None
    for row in streams["application"]:
        require({"enterQpc", "exitQpc", "frame", "thread", "exitReason", "updateMs", "intervalMs", "intervalReason", "updateReason", "backend", "backendReason"} <= row.keys(), "missing application fields")
        if valid_window and (not finite(row["enterQpc"]) or not start <= row["enterQpc"] <= stop):
            limitations.append("application admission outside measured window")
        if not finite(row["exitQpc"]) or not finite(row["enterQpc"]) or row["exitQpc"] < row["enterQpc"] or finite(freeze) and row["exitQpc"] > freeze:
            limitations.append("invalid/unfinished application duration")
        if row["frame"] is None or previous is not None and row["frame"] != previous + 1:
            limitations.append("application frame clock missing, duplicated, reset, or gapped")
        previous = row["frame"]
        if not row["completed"]:
            require(row["exitReason"] is None and row["updateMs"] is None, "unfinished application has a fabricated duration/reason")
        if row["exitReason"] != "updated":
            limitations.append("UpdateReflex skipped native processing")
        state = row["backend"]
        if mode == "A":
            require(state is None, "minimal mode recorded native context")
        elif row["exitReason"] == "updated":
            if state is None:
                limitations.append("missing returned backend context")
            else:
                require({"health", "pending", "generation", "attemptsObserved", "settingsAttempted", "sleepAttempted", "sleepSkipReason",
                         "desiredValid", "acceptedValid", "desiredMode", "acceptedMode", "desiredCapUs", "acceptedCapUs", "uptimeMs", "retryAtMs"} <= state.keys(), "missing per-update backend state")
                if state["health"] != 2 or state["pending"] or state["generation"] != condition["backend"]["generation"]:
                    limitations.append("backend not steadily healthy on the acquired generation")
                if not state["attemptsObserved"]:
                    limitations.append("native attempt/skip observation missing")
                else:
                    matching = native_by_application.get(row["sequence"], [])
                    for operation, flag in (("settings", "settingsAttempted"), ("sleep", "sleepAttempted")):
                        if sum(item["operation"] == operation for item in matching) != int(state[flag]):
                            limitations.append("native journal does not cover the update's actual attempts")
        if valid_window and finite(row["enterQpc"]):
            expected_interval = 1000 * (row["enterQpc"] - previous_entry) / frequency if finite(previous_entry) and row["enterQpc"] > previous_entry else None
            require(expected_interval is None and row["intervalMs"] is None or finite(expected_interval) and finite(row["intervalMs"]) and math.isclose(expected_interval, row["intervalMs"], abs_tol=1e-6), "application interval lost or altered")
            if row["completed"] and finite(row["exitQpc"]) and row["exitQpc"] >= row["enterQpc"]:
                require(finite(row["updateMs"]) and math.isclose(row["updateMs"], 1000 * (row["exitQpc"] - row["enterQpc"]) / frequency, abs_tol=1e-6), "whole-update duration altered")
        previous_entry = row["enterQpc"]
        for metric, reason in (("intervalMs", "intervalReason"), ("updateMs", "updateReason")):
            require(row[metric] is None and row[reason] or finite(row[metric]) and row[metric] >= 0, f"invalid {metric}")
    if valid_window and streams["application"]:
        entries = [r["enterQpc"] for r in streams["application"] if finite(r["enterQpc"])]
        intervals = statistics(r["intervalMs"] for r in streams["application"])
        if entries and intervals and max(entries[0] - start, stop - entries[-1]) * 1000 / frequency > max(2 * intervals["p50"], 100):
            limitations.append("large censored boundary gap; frame interval crossing admission boundary is not fully measured")
    for row in streams["native"]:
        require({"admittedQpc", "admissionSource", "enterQpc", "exitQpc", "generation", "device", "frame", "operation", "result", "submitted", "durationMs", "durationReason"} <= row.keys(), "missing native fields")
        if valid_window and (not finite(row["admittedQpc"]) or not start <= row["admittedQpc"] <= stop):
            limitations.append("native admission outside window")
        require(row["operation"] in ("settings", "sleep"), "unknown native operation")
        if not row["completed"]:
            require(row["submitted"] is None and row["durationMs"] is None, "unfinished settings presented as submitted")
        if row["operation"] == "settings" and row["completed"]:
            require({"version", "size", "bLowLatencyMode", "bLowLatencyBoost", "minimumIntervalUs", "bUseMarkersToOptimize", "bUseMinQueueTime"} <= row["submitted"].keys(), "missing actual settings arguments")
            require(row["submitted"]["bUseMarkersToOptimize"] == 0, "marker-driven optimization was enabled")
            original = condition["lastSettingsSubmission"]
            if not condition["probe"] and original and any(row["submitted"][key] != original[key] for key in
                    ("bLowLatencyMode", "bLowLatencyBoost", "minimumIntervalUs", "bUseMarkersToOptimize", "bUseMinQueueTime")):
                limitations.append("effective native settings changed during acquisition")
        require(row["durationMs"] is None and row["durationReason"] or finite(row["durationMs"]) and row["durationMs"] >= 0, "invalid native duration")
        if row["completed"] and row["result"] != 0:
            limitations.append("native operation failure")
        if row["generation"] != condition["backend"]["generation"]:
            limitations.append("native device generation changed during acquisition")
    requested = condition["requested"]
    needs_sleep = requested.get("enabled", False) or requested.get("capped", False)
    if mode != "A" and needs_sleep and not any(r["completed"] and r["operation"] == "sleep" and r["result"] == 0 for r in streams["native"]):
        limitations.append("requested pacing has no completed successful native Sleep evidence")
    previous = None
    for row in streams["compositor"]:
        require({"retrievedQpc", "frame", "epoch", "systemSeconds", "clientMs", "totalGpuMs", "newFrameReadyMs", "newPosesReadyMs", "nullReason", "presents", "dropped", "mispresented"} <= row.keys(), "missing compositor fields")
        if valid_window and (not finite(row["retrievedQpc"]) or not start <= row["retrievedQpc"] <= freeze):
            limitations.append("compositor retrieval outside acquisition/post-roll")
        if row["systemSeconds"] is None or row["clientMs"] is None or row["clientMs"] <= 0:
            limitations.append("unavailable compositor clock/cadence")
        if previous and (row["frame"] != previous["frame"] + 1 or row["epoch"] != previous["epoch"] or
                         not finite(row["systemSeconds"]) or not finite(previous["systemSeconds"]) or
                         row["systemSeconds"] <= previous["systemSeconds"]):
            limitations.append("compositor source discontinuity")
        previous = row
    for row in streams["readback"]:
        require({"result", "version", "size", "generation", "requestedQpc", "enterQpc", "exitQpc", "fields", "unavailableReason", "meaning"} <= row.keys(), "missing readback fields")
        if row["result"] != 0 or row["version"] != row["size"] | (1 << 16) or not finite(row["enterQpc"]) or not finite(row["exitQpc"]) or row["enterQpc"] <= 0 or row["exitQpc"] < row["enterQpc"]:
            limitations.append("native readback unavailable or invalid")
        require((row["fields"] is not None) == (row["result"] == 0), "failed getter exported zero-valued fields")
        if row["generation"] != condition["backend"]["generation"]:
            limitations.append("readback does not match captured device generation")
        if valid_window and (not finite(row["admittedQpc"]) or not start <= row["admittedQpc"] <= stop or finite(row["exitQpc"]) and row["exitQpc"] > freeze):
            limitations.append("readback outside admitted capture/post-roll")
    require(mode in ("A", "B", "C"), "unknown observer mode")
    require(mode != "A" or not streams["native"] and not streams["compositor"] and not streams["readback"], "minimal mode collected extra sources")
    require(mode == "C" or not streams["readback"], "queries contaminated passive acquisition")
    require(first["scope"]["latency"] == "UNSUPPORTED", "unsupported latency conclusion")
    require(first["scope"]["pre12Restoration"].startswith("NOT_MEASURED"), "unsupported Pre12 restoration conclusion")
    if first["workerError"] or first["termination"] != "completed":
        limitations.append("capture did not complete normally")
    if not first["quality"]["complete"]:
        limitations.append("capture quality counters prohibit completeness")
    if not streams["application"]:
        limitations.append("missing application cadence")
    if mode != "A" and not streams["compositor"]:
        limitations.append("missing VR frame-delivery evidence")
    return {"schemaValid": True, "paginationComplete": True, "counts": counts,
            "limitations": sorted(set(limitations)), "complete": not limitations,
            "pacingVerdict": "NOT_ASSESSED", "vrSmoothnessVerdict": "NOT_ASSESSED",
            "latencyVerdict": "UNSUPPORTED", "pre12RestorationVerdict": "NOT_MEASURED"}


def statistics(values):
    """Retain all valid raw observations, including stalls; never subtract costs."""
    values = sorted(v for v in values if finite(v) and v >= 0)
    if not values:
        return None
    return {"count": len(values), "mean": sum(values) / len(values), "max": values[-1],
            "p50": values[math.ceil(len(values) * 0.50) - 1],
            "p95": values[math.ceil(len(values) * 0.95) - 1],
            "p99": values[math.ceil(len(values) * 0.99) - 1]}


def summarize(pages):
    records = [row for page in pages for row in page["records"]]
    app_entries = [r["data"]["enterQpc"] for r in records if r["stream"] == "application" and finite(r["data"]["enterQpc"])]
    boundaries = pages[0]["boundaries"]
    frequency = boundaries["qpcFrequency"]
    def boundary_gap(edge, entry):
        return 1000 * abs(edge - entry) / frequency if finite(edge) and finite(entry) and finite(frequency) and frequency > 0 else None
    return {"boundaryGaps": {"admissionToFirstEntryMs": boundary_gap(boundaries["applicationAdmissionStartQpc"], app_entries[0]) if app_entries else None,
                             "lastEntryToAdmissionCloseMs": boundary_gap(boundaries["applicationAdmissionStopQpc"], app_entries[-1]) if app_entries else None,
                             "meaning": "censored boundary gaps, not completed frame intervals; inspect raw records and unfinished counts"},
            "frameDelivery": {key: statistics(r["data"].get(key) for r in records if r["stream"] == "compositor") for key in ("presents", "dropped", "mispresented", "poseWaitMs", "renderToSubmitMs", "totalGpuMs")},
            "appIntervalMs": statistics(r["data"]["intervalMs"] for r in records if r["stream"] == "application"),
            "wholeUpdateMs": statistics(r["data"]["updateMs"] for r in records if r["stream"] == "application"),
            "nativeCallMs": {op: statistics(r["data"]["durationMs"] for r in records if r["stream"] == "native" and r["data"]["operation"] == op)
                             for op in ("settings", "sleep")},
            "compositorClientMs": statistics(r["data"]["clientMs"] for r in records if r["stream"] == "compositor"),
            "queryMs": statistics(1000 * (r["data"]["exitQpc"] - r["data"]["enterQpc"]) / pages[0]["boundaries"]["qpcFrequency"]
                                  for r in records if r["stream"] == "readback" and finite(r["data"]["enterQpc"]) and finite(r["data"]["exitQpc"])
                                  and finite(pages[0]["boundaries"]["qpcFrequency"]) and pages[0]["boundaries"]["qpcFrequency"] > 0),
            "observerCostsSubtracted": False}


def observer_comparisons(phases, relative_threshold=0.05, absolute_threshold_ms=0.2):
    """Return paired A/B/C costs per repeat without manufacturing efficacy."""
    output = []
    repeats = sorted({p["condition"]["repeat"] for p in phases if p["condition"]["kind"] == "observer"})
    for repeat in repeats:
        matched = {p["condition"]["mode"]: p for p in phases if p["condition"]["kind"] == "observer" and p["condition"]["repeat"] == repeat}
        for baseline, observer in (("A", "B"), ("B", "C")):
            before, after = matched.get(baseline), matched.get(observer)
            if not before or not after or not before["validation"]["complete"] or not after["validation"]["complete"]:
                output.append({"repeat": repeat, "baseline": baseline, "observer": observer,
                               "status": "UNAVAILABLE", "reason": "missing or invalid matched observer condition"})
                continue
            deltas = {}
            for metric in ("appIntervalMs", "wholeUpdateMs", "compositorClientMs"):
                left, right = before["statistics"][metric], after["statistics"][metric]
                deltas[metric] = None if not left or not right else {name: right[name] - left[name] for name in ("mean", "p95", "p99", "max")}
            reference = before["statistics"]["appIntervalMs"]
            threshold = max(absolute_threshold_ms, relative_threshold * reference["p50"]) if reference else None
            cadence_delta = deltas["appIntervalMs"]
            flagged = threshold is not None and cadence_delta is not None and any(cadence_delta[k] > threshold for k in ("mean", "p95", "p99"))
            output.append({"repeat": repeat, "baseline": baseline, "observer": observer, "status": "MEASURED",
                           "baselineMeaning": "A is a minimal observer, not disabled", "deltaMs": deltas,
                           "queryMs": after["statistics"].get("queryMs"), "observerCostsSubtracted": False,
                           "materiality": "POTENTIAL_OBSERVER_IMPACT" if flagged else "NO_THRESHOLD_EXCEEDANCE",
                           "thresholdMs": threshold, "thresholdDefinition": "max(absolute threshold, relative threshold times baseline median application interval); diagnostic flag, not statistical significance",
                           "referencePeriodMs": reference["p50"] if reference else None,
                           "displayFrameBudgetMs": None, "budgetReason": "application cadence reference is measured; headset display budget is not inferred"})
    return output


def functional_comparisons(phases):
    """Test deliberately binding caps, without claiming uncapped or latency benefit."""
    output = []
    for capped in phases:
        condition = capped["condition"]
        if condition["kind"] != "functional" or not condition["capFps"]:
            continue
        baselines = [p for p in phases if p["condition"]["kind"] == "functional" and not p["condition"]["capFps"] and p["condition"]["reflexMode"] == condition["reflexMode"]]
        baseline = baselines[-1] if baselines else None
        row = {"mode": condition["reflexMode"], "capFps": condition["capFps"], "scope": "host cap-response functional check only; no uncapped benefit, VR smoothness or latency verdict"}
        if not baseline or not baseline["validation"]["complete"] or not capped["validation"]["complete"]:
            row.update(status="UNAVAILABLE", reason="missing or invalid matched functional acquisition")
        else:
            before, after = baseline["statistics"]["appIntervalMs"], capped["statistics"]["appIntervalMs"]
            period = 1000 / condition["capFps"]
            row.update(uncapped=before, capped=after, expectedMinimumIntervalMs=period, nativeSleepHostMs=capped["statistics"]["nativeCallMs"]["sleep"])
            if not before or not after or before["p95"] >= 0.95 * period:
                row.update(status="NOT_TESTABLE", reason="uncapped baseline lacks headroom for a binding cap")
            elif after["p50"] < 0.9 * period:
                row.update(status="NO_EXPECTED_CAP_RESPONSE", reason="median cadence remains faster than the accepted cap permits")
            elif after["p50"] > 1.1 * period:
                row.update(status="INCONCLUSIVE", reason="cadence slower than cap; workload or external pacing prevents isolation")
            else:
                row.update(status="CAP_RESPONSE_OBSERVED", reason="valid accepted arguments and cadence match deliberately binding cap within 10 percent")
        output.append(row)
    return output


def mode_comparisons(phases):
    """Preserve within-repeat Off/On/Boost effects without cross-build claims."""
    output = []
    for phase in phases:
        condition = phase["condition"]
        if condition["kind"] != "comparison" or condition["reflexMode"] == "off":
            continue
        baseline = next((p for p in phases if p["condition"]["kind"] == "comparison" and p["condition"]["reflexMode"] == "off" and p["condition"]["capFps"] == condition["capFps"] and p["condition"]["repeat"] == condition["repeat"]), None)
        row = {"repeat": condition["repeat"], "mode": condition["reflexMode"], "capFps": condition["capFps"], "baseline": "Off", "efficacy": "NOT_ASSESSED; interpret matched repeats and observer cost together"}
        if not baseline or not baseline["validation"]["complete"] or not phase["validation"]["complete"]:
            row.update(status="UNAVAILABLE", reason="missing or invalid matched Off baseline")
        else:
            row["status"] = "MEASURED"
            row["deltaMs"] = {}
            for metric in ("appIntervalMs", "wholeUpdateMs", "compositorClientMs"):
                before, after = baseline["statistics"][metric], phase["statistics"][metric]
                row["deltaMs"][metric] = {key: after[key] - before[key] for key in ("mean", "p50", "p95", "p99", "max")} if before and after else None
        output.append(row)
    return output
