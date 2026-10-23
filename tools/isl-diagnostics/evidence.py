"""Validate ISL sidecars and analyze explicitly named OpenVR offset proxies."""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import statistics

REFLEX = Path(__file__).resolve().parents[1] / "reflex-measurement"
_spec = importlib.util.spec_from_file_location("csx_reflex_evidence", REFLEX / "evidence.py")
raw = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(raw)
require, finite, EvidenceError = raw.require, raw.finite, raw.EvidenceError


def sidecar(pages, raw_pages, expected_build):
    require(bool(pages), "missing ISL sidecar")
    first = pages[0]
    required = {"schema", "producer", "sessionId", "clientId", "captureId", "generation", "boundaries", "condition",
                "threads", "prune", "quality", "sampling", "records", "offset", "total", "nextOffset"}
    require(required <= first.keys(), "missing sidecar fields")
    source = raw_pages[0]
    require(first["schema"] == 1 and first["producer"]["buildId"] == expected_build, "sidecar build/schema mismatch")
    require(first["captureId"] == source["captureId"] and first["sessionId"] == source["recording"]["sessionId"] and
            first["clientId"] == source["recording"]["owner"], "raw/sidecar ownership mismatch")
    b, rb = first["boundaries"], source["boundaries"]
    for left, right in (("startQpc", "applicationAdmissionStartQpc"), ("stopQpc", "applicationAdmissionStopQpc"),
                        ("freezeQpc", "freezeQpc"), ("qpcFrequency", "qpcFrequency")):
        value = b[left]
        if left != "qpcFrequency" and finite(value) and value <= 0:
            value = None
        require(value == rb[right], "raw/sidecar boundary mismatch")
    cursor, rows = 0, []
    for index, page in enumerate(pages):
        require(required <= page.keys(), "missing sidecar page metadata")
        require(page["offset"] == cursor, "sidecar gap/overlap")
        for key in required - {"records", "offset", "nextOffset"}:
            require(page[key] == first[key], f"sidecar frozen {key} changed")
        require(len(page["records"]) <= 256, "sidecar page too large")
        for row in page["records"]:
            require(row["ordinal"] == cursor and {"metric", "generation", "thread", "frame", "caller", "enterQpc", "exitQpc"} <= row.keys(), "invalid sidecar row")
            require(type(row["enterQpc"]) is int and type(row["exitQpc"]) is int and row["enterQpc"] > 0 and row["exitQpc"] >= row["enterQpc"], "invalid sidecar clock")
            rows.append(row)
            cursor += 1
        following = cursor if cursor < first["total"] else None
        require(page["nextOffset"] == following, "invalid sidecar continuation")
        require(index == len(pages) - 1 or following is not None, "extra sidecar page")
    require(cursor == first["total"] and pages[-1]["nextOffset"] is None, "incomplete sidecar export")
    quality = first["quality"]
    loss = {"unfinishedThreadSlots", "overflow", "threadRegistrationLoss", "contention", "uniqueOverflow", "transitionOrBoundaryOverlap"}
    require(loss | {"contaminated"} <= quality.keys(), "missing sidecar loss fields")
    require(all(type(quality[k]) is int and quality[k] >= 0 for k in loss) and type(quality["contaminated"]) is bool, "invalid sidecar quality")
    for key in ("oldGenerationAtAdmission", "unstableInvocationInspection"):
        require(type(quality.get(key)) is int and quality[key] >= 0, "missing transition inspection")
        loss.add(key)
    limits = [k for k in loss - {"uniqueOverflow", "transitionOrBoundaryOverlap"} if quality[k]]
    valid_window = all(finite(b[key]) and b[key] > 0 for key in ("startQpc", "stopQpc", "freezeQpc", "qpcFrequency")) and b["startQpc"] <= b["stopQpc"] <= b["freezeQpc"]
    if not valid_window:
        limits.append("unavailable or invalid capture clock/boundaries")
    if quality["contaminated"]:
        limits.append("outside control or unsafe game state during capture")
    condition = first["condition"]
    if not condition["ready"] or condition["requestedGeneration"] != first["generation"] or condition["effectiveGeneration"] != first["generation"]:
        limits.append("unsettled generation at admission")
    # Boundary-straddling observations remain exported and are excluded from steady analysis.
    steady = [r for r in rows if valid_window and r["generation"] == first["generation"] and b["startQpc"] <= r["enterQpc"] <= r["exitQpc"] <= b["stopQpc"]]
    boundary_overlaps = sum(r['enterQpc'] < b['startQpc'] or r['exitQpc'] > b['stopQpc'] for r in rows) if valid_window else None
    old_generation_calls = quality['transitionOrBoundaryOverlap'] - boundary_overlaps if valid_window else None
    require(old_generation_calls is None or old_generation_calls >= 0, 'boundary records exceed the declared overlap count')
    if old_generation_calls or any(r['generation'] != first['generation'] for r in rows):
        limits.append('old-generation invocations entered the admitted capture')
    counters = {}
    for thread in first["threads"]:
        for metric, values in thread["counters"].items():
            require({"count", "timedSamples", "elapsedQpc", "maxQpc"} <= values.keys() and
                    all(type(v) is int and v >= 0 for v in values.values()), "invalid counter totals")
            total = counters.setdefault(metric, {"count": 0, "timedSamples": 0, "elapsedQpc": 0, "maxQpc": 0})
            for key in ("count", "timedSamples", "elapsedQpc"):
                total[key] += values[key]
            total["maxQpc"] = max(total["maxQpc"], values["maxQpc"])
    return {"schemaValid": True, "paginationComplete": True, "complete": not limits, "limitations": limits,
            "steadyRows": len(steady), "excludedBoundaryOrTransitionRows": len(rows) - len(steady), "counters": counters,
            "oldGenerationInvocationsDuringCapture": old_generation_calls,
            "timedMs": {metric: raw.statistics(1000 * (r["exitQpc"] - r["enterQpc"]) / b["qpcFrequency"]
                                               for r in steady if r["metric"] == metric) for metric in {r["metric"] for r in steady}},
            "uniqueOverflow": quality["uniqueOverflow"], "countsMeaning": "unsampled totals include boundary overlap; sampled durations are not additive whole-frame costs"}


def streams(pages, name):
    return [r["data"] for p in pages for r in p["records"] if r["stream"] == name]


def experiment_applied(phase, validation, header, original):
    """Require actual eligible work before interpreting a subtraction."""
    gates = phase['gates']
    counts = validation['counters']
    count = lambda name: counts.get(name, {}).get('count', 0)
    reasons = []
    if gates['observationLevel'] == 'off' and any(gates[key] != 'normal' for key in
            ('luminanceMode', 'rendererMode', 'particleDetectionMode', 'editorOverrideMode')):
        reasons.append('eligible subtraction work cannot be established with observations off')
    if gates['observationLevel'] != 'off':
        if gates['luminanceMode'] != 'normal' and (not count('LuminanceEligible') or not count('LuminanceNative')):
            reasons.append('no eligible native luminance calls observed')
        if gates['rendererMode'] != 'normal' and not count('EligibleInverseSquare'):
            reasons.append('no eligible inverse-square renderer conversions observed')
        if gates['particleDetectionMode'] != 'normal' and not count('ParticleQuery'):
            reasons.append('no particle luminance queries observed')
        if gates['editorOverrideMode'] == 'bypass' and not count('ProcessLight'):
            reasons.append('no light processing observed for editor subtraction')
    if phase.get('shadowCulling') is not None:
        shadow = header['condition']['shadowCulling']
        baseline = original['shadowCulling']
        if not shadow['available'] or not any(baseline['settings'][k] for k in ('sun','torch','actors')):
            reasons.append('shadow culling unavailable or already disabled in reference')
        if any(shadow['settings'][k] for k in ('sun','torch','actors')):
            reasons.append('shadow subtraction was not applied')
    if phase.get('pruneFrames'):
        prune = header['prune']
        if not prune.get('consumed') or not prune.get('delayed') or prune.get('cancelled'):
            reasons.append('armed prune deferral was not exercised in this capture')
        if prune.get('activationQpc') != header['boundaries']['startQpc']:
            reasons.append('prune activation was not aligned to admission')
    return {'valid': not reasons, 'limitations': reasons, 'observer': gates['observationLevel']}


def scene_match(before, after, compositor, reference=None):
    """Conservative matched-view qualification, not a claim of identical AI/lighting."""
    limits = []
    scene = before.get("scene", {})
    reference = reference or {"scene": scene, "gameHour": before.get("gameHour"), "pose": None}
    for key in ("cellFormId", "weatherFormId", "upscaling", "gamePaused"):
        if key not in scene or scene[key] is None or scene[key] != after.get("scene", {}).get(key) or scene[key] != reference["scene"].get(key):
            limits.append("unmatched or unavailable " + key)
    if scene.get("gamePaused") is not False:
        limits.append("game paused or unknown")
    positions = [value.get("position") for value in (scene, after.get("scene", {}), reference["scene"])]
    if not all(isinstance(p, list) and len(p) == 3 and all(finite(x) for x in p) for p in positions):
        limits.append("player position unavailable")
    elif any(math.dist(positions[0], p) > 8 for p in positions[1:]):
        limits.append("player moved more than 8 world units")
    hours = [before.get("gameHour"), after.get("gameHour")]
    if not all(finite(x) for x in hours):
        limits.append("game time unavailable")
    poses = [row.get("hmdPose", {}) for row in compositor]
    valid_poses = [p["matrix34"] for p in poses if p.get("available") is True and isinstance(p.get("matrix34"), list) and
                   len(p["matrix34"]) == 12 and all(finite(x) for x in p["matrix34"])]
    max_position, max_angle = None, None
    if not poses or len(valid_poses) != len(poses):
        limits.append("HMD pose evidence unavailable or incomplete")
    else:
        anchor = reference.get("pose") or [statistics.median(p[i] for p in valid_poses) for i in range(12)]
        reference = dict(reference, pose=anchor)
        max_position = max(math.dist([p[i] for i in (3, 7, 11)], [anchor[i] for i in (3, 7, 11)]) for p in valid_poses)
        def angle(p):
            dot = sum(p[i] * anchor[i] for i in (2, 6, 10))
            norm = math.sqrt(sum(p[i] ** 2 for i in (2, 6, 10)) * sum(anchor[i] ** 2 for i in (2, 6, 10)))
            return math.degrees(math.acos(max(-1, min(1, dot / norm)))) if norm > 0 else 180
        max_angle = max(angle(p) for p in valid_poses)
        if max_position > 0.10 or max_angle > 5:
            limits.append("HMD viewpoint changed beyond 10 cm / 5 degrees")
    return {"valid": not limits, "limitations": limits, "reference": reference,
            "referenceGameHour": reference.get("gameHour"), "conditionReceiptGameHours": hours,
            "gameTimeQualification": "after receipt follows export; elapsed time includes transfer, not only admission. No time reset or fixed-time equivalence is claimed",
            "maxHmdPositionDeltaMetres": max_position, "maxHmdForwardAngleDegrees": max_angle,
            "meaning": "fixed predeclared matching limits; natural time progression across conditions is retained. Bracketing references are required. Does not prove unchanged AI, dynamic lights or intermediate game position"}


def reference_rules(rows):
    """Freeze the proxy's input and thresholds using the first reference only."""
    require(len(rows) >= 100, "at least 100 reference compositor records required")
    values = [r.get("waitGetPosesCalledMs") for r in rows]
    require(all(finite(x) for x in values), "pose-call offset unavailable")
    deltas = [1000 * (b["systemSeconds"] - a["systemSeconds"]) for a, b in zip(rows, rows[1:])]
    require(all(finite(x) and x > 0 for x in deltas), "reference compositor clock invalid")
    cadence = statistics.median(deltas)
    require(2 <= cadence <= 100, "reference cadence outside supported proxy range")
    baseline = sorted(values)[len(values) // 10]
    return {"name": "OpenVR pose-call-offset excursion proxy v1", "input": "waitGetPosesCalledMs",
            "formula": "raw signed offset minus first-reference tenth percentile; high when above frozen threshold",
            "baselineMs": baseline, "thresholdMs": max(2.0, 0.20 * cadence), "referenceCadenceMs": cadence,
            "cadenceMeaning": "observed compositor source cadence, not an asserted display period",
            "toothMaximumFrames": 4, "mountainMinimumFrames": 8, "combMinimumTeeth": 8,
            "combMaximumGapFrames": 90, "combMaximumGapCV": 0.35, "minimumRecords": 100,
            "classificationMeaning": "diagnostic waveform proxy; NOT SteamVR CPU Late Start and NOT a root-cause verdict"}


def waveform(rows, rules):
    unavailable = {"status": "UNAVAILABLE", "classification": "UNAVAILABLE"}
    if len(rows) < rules["minimumRecords"]:
        return unavailable | {"reason": "insufficient contiguous compositor records"}
    for index, row in enumerate(rows):
        if not finite(row.get(rules["input"])) or not finite(row.get("systemSeconds")):
            return unavailable | {"reason": "missing raw offset/clock"}
        if index and (row["epoch"] != rows[index - 1]["epoch"] or row["frame"] != rows[index - 1]["frame"] + 1 or
                      row["systemSeconds"] <= rows[index - 1]["systemSeconds"]):
            return unavailable | {"reason": "source gap, reset or invalid clock"}
    values = [r[rules["input"]] - rules["baselineMs"] for r in rows]
    intervals = []
    begin = None
    for i in range(len(values) + 1):
        high = i < len(values) and values[i] > rules["thresholdMs"]
        if high and begin is None:
            begin = i
        if not high and begin is not None:
            end = i - 1
            intervals.append({"firstFrame": rows[begin]["frame"], "lastFrame": rows[end]["frame"],
                              "firstSystemSeconds": rows[begin]["systemSeconds"], "lastSystemSeconds": rows[end]["systemSeconds"],
                              "frames": end - begin + 1, "peakMs": max(values[begin:i]), "censored": begin == 0 or i == len(values)})
            begin = None
    teeth = [x for x in intervals if not x["censored"] and x["frames"] <= rules["toothMaximumFrames"]]
    mountains = [x for x in intervals if x["frames"] >= rules["mountainMinimumFrames"]]
    gaps = [b["firstFrame"] - a["firstFrame"] for a, b in zip(teeth, teeth[1:])]
    gap_cv = statistics.pstdev(gaps) / statistics.mean(gaps) if gaps else None
    comb = len(teeth) >= rules["combMinimumTeeth"] and max(gaps, default=math.inf) <= rules["combMaximumGapFrames"] and gap_cv <= rules["combMaximumGapCV"]
    seconds = rows[-1]["systemSeconds"] - rows[0]["systemSeconds"]
    kind = "MIXED_PROXY" if comb and mountains else "COMB_PROXY" if comb else "MOUNTAIN_PROXY" if mountains else "OTHER_EXCURSIONS" if intervals else "NO_EXCURSIONS"
    return {"status": "MEASURED_PROXY", "classification": kind, "combDetected": comb, "mountainsDetected": bool(mountains),
            "toothCount": len(teeth), "mountainCount": len(mountains), "gapCV": gap_cv,
            "toothRatePerSecond": len(teeth) / seconds, "mountainRatePerSecond": len(mountains) / seconds,
            "highFrameFraction": sum(v > rules["thresholdMs"] for v in values) / len(values),
            "offsetExcessMs": raw.statistics(max(0, v) for v in values), "events": intervals, "rules": rules}


def correlate_qpc(raw_pages, side_pages):
    """Prior/current application intervals share QPC with sidecar rows, not OpenVR offsets."""
    frequency = raw_pages[0]["boundaries"]["qpcFrequency"]
    apps = streams(raw_pages, "application")
    intervals = [(a["enterQpc"], b["enterQpc"]) for a, b in zip(apps, apps[1:])
                 if finite(a["enterQpc"]) and finite(b["enterQpc"]) and b["enterQpc"] > a["enterQpc"]]
    if len(intervals) < 10 or not finite(frequency) or frequency <= 0:
        return {"status": "UNAVAILABLE", "reason": "application intervals unavailable"}
    periods = [(b - a) * 1000 / frequency for a, b in intervals]
    threshold = max(statistics.median(periods) * 1.25, statistics.median(periods) + 2)
    rows = [r for page in side_pages for r in page["records"]]
    result = []
    for index, (begin, end) in enumerate(intervals):
        if periods[index] <= threshold:
            continue
        previous = intervals[max(0, index - 1)][0]
        adjacent = [r for r in rows if r["exitQpc"] >= previous and r["enterQpc"] <= end]
        result.append({"beginQpc": begin, "endQpc": end, "priorBeginQpc": previous, "intervalMs": periods[index],
                       "overlappingOrdinals": [r["ordinal"] for r in adjacent],
                       "metrics": sorted({r["metric"] for r in adjacent})})
    return {"status": "MEASURED_QPC_ASSOCIATIONS", "thresholdMs": threshold, "intervals": result,
            "meaning": "same-clock overlap in previous/current application intervals; sampled observations, not causation or exact compositor-frame matching"}


def comparison(phases):
    references = [p for p in phases if p["condition"]["label"] == "R"]
    reproducible = len(references) >= 3 and all(p["valid"] and p["waveform"].get("status") == "MEASURED_PROXY" and p["waveform"].get("combDetected", False) for p in references)
    output = []
    for label in dict.fromkeys(p["condition"]["label"] for p in phases if p["condition"]["label"] != "R"):
        treatments = [p for p in phases if p["condition"]["label"] == label]
        valid = reproducible and len(treatments) >= 2 and all(p["valid"] and p["waveform"].get("status") == "MEASURED_PROXY" for p in treatments)
        reduced = valid and all(not p["waveform"].get("combDetected", True) for p in treatments)
        baseline_rates = [p['waveform'].get('toothRatePerSecond') for p in references]
        treatment_rates = [p['waveform'].get('toothRatePerSecond') for p in treatments]
        quantified = all(finite(x) for x in baseline_rates + treatment_rates)
        baseline_rate = statistics.median(baseline_rates) if quantified else None
        fewer = valid and quantified and all(x < .8 * baseline_rate for x in treatment_rates)
        more = valid and quantified and all(x > 1.2 * baseline_rate for x in treatment_rates)
        outcome = 'invalid or incomplete' if not valid else 'reduced' if reduced or fewer else 'increased' if more else 'unchanged or mixed'
        output.append({"label": label, "status": "REPEATED_PROXY_REDUCTION" if reduced or fewer else "NO_REPEATED_PROXY_REDUCTION" if valid else "INCONCLUSIVE",
                       "outcome": outcome, "referenceToothRatePerSecond": baseline_rate, "treatmentToothRatesPerSecond": treatment_rates,
                       "rateRule": "each repeat must differ from median reference by more than 20%; no average-FPS inference",
                       "referenceCombReproduced": reproducible, "runtimeFixProven": False,
                       "reason": "inspect raw offsets, mountains, settings, scene matching and observer effects; proxy changes identify a candidate, not exact SteamVR late starts"})
    return {"referenceCombReproduced": reproducible, "comparisons": output, "exactSteamVRLateStart": "UNAVAILABLE",
            "referenceStatus": "reproduced" if reproducible else "reference_not_reproduced_under_selected_pacing_or_incomplete",
            "mountains": [{"name": p["name"], "count": p["waveform"].get("mountainCount"), "rate": p["waveform"].get("mountainRatePerSecond")} for p in phases],
            "historicalEvidence": {"storedRadius": "late starts persisted; waveform not quantified", "retentionParticleCulling": "late starts persisted; waveform not quantified",
                                   "radiusWriteSuppression": "reported negative", "fullPre12ISL": "outcome not established", "VRShadowBatch": "outcome not established"}}


def readable_report(phases, failures=(), cleanup=None):
    report = comparison(phases)
    cleanup = cleanup or {}
    restoration = {key: cleanup[key] for key in ('restorationFailure', 'stopOrExportFailure') if key in cleanup}
    restoration['pacing'] = cleanup.get('rawRestoration', {}).get('restoration')
    remaining = cleanup.get('remainingISL', {})
    restoration['isl'] = remaining.get('restoration', remaining)
    explanations = {
        'L': 'Native luminance with normal renderer: investigates substituted luminance and native side effects.',
        'V': 'Ordinary renderer output after normal conversion: investigates renderer workload while preserving engine writes.',
        'C': 'Compute/discard plus native: extra custom AND native work; reduction alone cannot quantify arithmetic cost.',
        'B': 'Native luminance plus ordinary renderer output: a combined-path interaction test.',
        'S': 'Particle scan omitted, shared lock retained.', 'P': 'Particle lock and scan both omitted; native AI wrapper remains.',
        'O': 'Existing sun, torch and actor shadow culling disabled together.', 'E': 'Inactive editor override-call bypass.',
        'D': 'Conversion cost under startup radius suppression and native luminance; compared against matching ordinary output.',
        'X': 'Optional ISL observations off, raw collector still active.', 'T': 'Sampled timing observer compared with counters.',
        'Q': 'One exercised, bounded prune delay; inspect actual due/executed frames.'}
    lines = ['CSX ISL diagnostic evidence report', '', 'Reference: ' + report['referenceStatus'],
             'Exact SteamVR CPU Late Start classification: UNAVAILABLE.',
             'Results below use the named OpenVR pose-call-offset proxy. No runtime fix or root cause is proven.', '',
             'Ranked comparisons (campaign order):']
    for item in report['comparisons']:
        lines += [f"  {item['label']}: {item['outcome']} ({item['status']})", '    ' + explanations.get(item['label'], 'Inspect the complete condition receipt.')]
    lines += ['', 'Separate waveform observations:']
    for phase in phases:
        wave = phase['waveform']
        lines.append(f"  {phase['name']}: valid={phase['valid']}; {wave.get('classification', 'UNAVAILABLE')}; teeth={wave.get('toothCount')}; mountains={wave.get('mountainCount')}; mountain rate/s={wave.get('mountainRatePerSecond')}")
    lines += ['', 'Missing cost channels remain unavailable, not zero cost. Timed queries are sampled; retained GPU histories lack exact frame alignment.',
              'Natural game-time progression and dynamic AI/lighting remain qualifications; inspect before/after receipts and restored references.',
              'Historical negatives: radius-write suppression, stored radius, retention/particle/culling paths. Their waveform changes were not quantified.',
              'Full-Pre12 ISL and VRShadowBatch tester outcomes remain pending unless separately supplied.',
              '', 'Failures: ' + json.dumps(list(failures)), 'Restoration: ' + json.dumps(restoration),
              '', 'Preserve the entire output directory: raw pages, ISL sidecars, transport receipts, conditions, and recovery manifest.']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    args = parser.parse_args()
    campaign = json.loads(args.campaign.read_text(encoding="utf-8"))
    print(json.dumps(comparison(campaign["completed"]), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
