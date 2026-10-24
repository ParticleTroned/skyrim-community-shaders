"""Same-thread engine segmentation and cadence evidence, computed only offline."""

from bisect import bisect_left
import statistics


def stats(values):
    values = sorted(values)
    if not values:
        return {"count": 0, "mean": None, "p95": None, "max": None}
    return {"count": len(values), "mean": statistics.mean(values),
            "p95": values[int((len(values) - 1) * .95)], "max": values[-1]}


def cadence(rows):
    """Detect cadence wraps corroborated by repeats, without a contaminated baseline."""
    needed = ("systemSeconds", "waitGetPosesCalledMs", "presents", "frame", "epoch")
    if len(rows) < 100 or any(any(r.get(k) is None for k in needed) for r in rows):
        return {"status": "UNAVAILABLE", "reason": "insufficient compositor cadence evidence"}
    pairs = [(a, b) for a, b in zip(rows, rows[1:])
             if b["epoch"] == a["epoch"] and b["frame"] == a["frame"] + 1 and b["systemSeconds"] > a["systemSeconds"]]
    if not pairs:
        return {"status": "UNAVAILABLE", "reason": "no contiguous compositor intervals"}
    period = statistics.median(1000 * (b["systemSeconds"] - a["systemSeconds"]) for a, b in pairs)
    if not 2 <= period <= 100:
        return {"status": "UNAVAILABLE", "reason": "unsupported observed compositor cadence"}
    # A reset of the signed offset following a repeat is a phase-wrap proxy, not CPU work.
    wraps = [{"frame": b["frame"], "systemSeconds": b["systemSeconds"],
              "deltaMs": b["waitGetPosesCalledMs"] - a["waitGetPosesCalledMs"],
              "previousPresents": a["presents"]}
             for a, b in pairs if b["waitGetPosesCalledMs"] - a["waitGetPosesCalledMs"] < -.5 * period]
    supported = [r for r in wraps if r["previousPresents"] > 1]
    gaps = [b["frame"] - a["frame"] for a, b in zip(supported, supported[1:])]
    cv = statistics.pstdev(gaps) / statistics.mean(gaps) if gaps else None
    regular = len(supported) >= 8 and cv is not None and cv <= .5 and max(gaps) <= 120
    seconds = rows[-1]["systemSeconds"] - rows[0]["systemSeconds"]
    return {"status": "MEASURED_CADENCE_PROXY", "combDetected": regular,
            "classification": "REPEATED_PHASE_WRAP" if regular else "IRREGULAR_PHASE_WRAPS" if supported else "NO_REPEATED_PHASE_WRAP",
            "observedCadenceMs": period, "sourceGaps": len(rows) - 1 - len(pairs), "wraps": wraps,
            "repeatCorroboratedWraps": len(supported), "wrapGapCV": cv,
            "extraPresentations": sum(max(0, r["presents"] - 1) for r in rows),
            "wrapsPerSecond": len(supported) / seconds if seconds > 0 else None,
            "meaning": "Signed pose-call phase reset following an extra presentation. Not SteamVR red-bar reconstruction or proof of the delaying function; absence does not exclude mountains."}


def analyze(side):
    boundary = side["boundaries"]
    frequency = boundary["qpcFrequency"]
    if not frequency or frequency <= 0:
        return {"status": "UNAVAILABLE", "reason": "missing QPC frequency"}
    scale = 1000 / frequency
    threads = {}
    labels = {(t["thread"], v["id"]): v["name"] for t in side["threads"] for v in t.get("labels", [])}
    for row in side["records"]:
        if (row["generation"] == side["generation"] and
                boundary["startQpc"] <= row["enterQpc"] <= row["exitQpc"] <= boundary["stopQpc"]):
            threads.setdefault(row["thread"], []).append(row)
    frames, inclusive = [], {}
    for thread, rows in threads.items():
        rows.sort(key=lambda r: r["enterQpc"])
        starts = [r["enterQpc"] for r in rows]
        for row in rows:
            name = labels.get((thread, row.get("labelId")), row["metric"])
            item = inclusive.setdefault(name, {"wallMs": [], "cpuMs": [], "cycles": []})
            item["wallMs"].append((row["exitQpc"] - row["enterQpc"]) * scale)
            if row.get("cpu100ns") is not None:
                item["cpuMs"].append(row["cpu100ns"] / 10000)
            if row.get("cycles") is not None:
                item["cycles"].append(row["cycles"])
            if row["metric"] != "EngineUpdate":
                continue
            begin, end = row["enterQpc"], row["exitQpc"]
            children = rows[bisect_left(starts, begin):bisect_left(starts, end)]
            children = [r for r in children if r is not row and r["exitQpc"] <= end]
            def extent(metric):
                candidates = [r for r in children if r["metric"] == metric]
                return (min(r["enterQpc"] for r in candidates), max(r["exitQpc"] for r in candidates)) if candidates else None
            shadow = extent("RetainedShadowRender")
            pose, submit, present = (extent(k) for k in ("PoseCycle", "VRSubmit", "Present"))
            points = [("beforeShadows", begin, shadow[0] if shadow else None),
                      ("shadowSpan", *(shadow or (None, None))),
                      ("shadowToPose", shadow[1] if shadow else None, pose[0] if pose else None),
                      ("pose", *(pose or (None, None))),
                      ("poseToSubmit", pose[1] if pose else None, submit[0] if submit else None),
                      ("submit", *(submit or (None, None))),
                      ("submitToPresent", submit[1] if submit else None, present[0] if present else None),
                      ("present", *(present or (None, None))),
                      ("afterPresent", present[1] if present else None, end)]
            cpu_points = {}
            for measured in [row] + children:
                if measured.get('cpuStart100ns') is not None and measured.get('cpu100ns') is not None:
                    cpu_points[measured['enterQpc']] = measured['cpuStart100ns']
                    cpu_points[measured['exitQpc']] = measured['cpuStart100ns'] + measured['cpu100ns']
            stage_cpu = {name: (cpu_points[b] - cpu_points[a]) / 10000
                         if a in cpu_points and b in cpu_points and b >= a and cpu_points[b] >= cpu_points[a] else None
                         for name, a, b in points}
            stages = {name: (b - a) * scale if a is not None and b is not None and b >= a else None for name, a, b in points}
            # Union, rather than sum, handles nested engine/CSX scopes without double counting.
            coverage, cursor = 0, begin
            for child in children:
                a, b = child["enterQpc"], child["exitQpc"]
                coverage += max(0, b - max(cursor, a))
                cursor = max(cursor, b)
            frames.append({"thread": thread, "enterQpc": begin, "exitQpc": end,
                           "wallMs": (end - begin) * scale, "cpuMs": row["cpu100ns"] / 10000 if row.get("cpu100ns") is not None else None,
                           "cycles": row.get("cycles"), "stagesMs": stages, "stageCpuMs": stage_cpu,
                           "uncoveredMs": (end - begin - coverage) * scale,
                           "containedScopeOrdinals": [r["ordinal"] for r in children]})
    return {"status": "MEASURED" if frames else "NO_COMPLETE_ENGINE_SCOPES", "frames": frames,
            "stageStatisticsMs": {key: stats(f["stagesMs"][key] for f in frames if f["stagesMs"][key] is not None)
                                  for key in frames[0]["stagesMs"]} if frames else {},
            "stageCpuStatisticsMs": {key: stats(f["stageCpuMs"][key] for f in frames if f["stageCpuMs"][key] is not None)
                                     for key in frames[0]["stageCpuMs"]} if frames else {},
            "inclusiveScopes": {name: {key: stats(values) for key, values in item.items()} for name, item in inclusive.items()},
            "contexts": [{"thread": t["thread"], **c} for t in side["threads"] for c in t.get("engineContexts", [])],
            "namedPassTotals": [{"thread": t["thread"], **v} for t in side["threads"] for v in t.get("labels", [])],
            "limitations": ["Wall scopes include waits and nested work; inclusive scopes are not additive.",
                            "CPU time is quantized; cycles are not milliseconds. Prefer aggregate CPU comparisons.",
                            "Named pass rows are sampled; uncovered time includes uninstrumented or unsampled work.",
                            "No direct whole-AI tick hook or exact engine/compositor frame mapping is claimed."]}
