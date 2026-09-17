"""Verify retained receipts, historical cells, windows and comparison values."""

import csv
from functools import cache
import hashlib
import json
from pathlib import Path
import statistics
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def packed(value):
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    folder = Path(__file__).resolve().parent
    root = folder.parents[2]
    proof = read(folder / "coverage.json")
    csv.field_size_limit(100 * 1024 * 1024)
    cells = {}
    columns = []
    for partition in proof["partitions"]:
        path = folder.parent / partition["ledger"]
        require(digest(path) == partition["sha256"], "Ledger hash differs")
        require(path.stat().st_size == partition["bytes"] < 100 * 1024 * 1024, "Ledger size differs")
        with path.open(encoding="utf-8", newline="") as stream:
            table = list(csv.reader(stream))
        require(table[0][2:] == partition["runColumns"], "Partition columns differ")
        require(len(table) - 1 == proof["metricRows"], "Metric count differs")
        columns.extend(table[0][2:])
        for row in table[1:]:
            require(len(row) == len(table[0]) and all(cell != "" for cell in row), "Incomplete row")
            for name, value in zip(table[0][2:], row[2:]):
                key = (name, row[0])
                require(key not in cells, "Duplicate ledger cell")
                cells[key] = (row[1], value)
                if row[1] == "json":
                    json.loads(value)
    require(columns == proof["runColumns"] and len(set(columns)) == 8, "Campaign columns differ")

    @cache
    def decoded(run, metric):
        return json.loads(cells[run, metric][1])

    with (folder.parent / proof["priorLedger"]).open(encoding="utf-8", newline="") as stream:
        prior = list(csv.reader(stream))
    for run in proof["selectedReferences"]:
        index = prior[0].index(run)
        for row in prior[1:]:
            require(cells[run, row[0]] == (row[1], row[index]), "Historical cell changed")
    for name, expected in proof["historicalLedgers"].items():
        require(digest(folder.parent / name) == expected, "Historical ledger changed")

    for source in proof["sources"]:
        value = decoded(source["run"], source["metric"])
        selector = source.get("selector")
        if selector == "markers":
            value = value["markers"]
        elif selector:
            value = value["records"][selector.removeprefix("records/")]
        require(hashlib.sha256(packed(value).encode()).hexdigest() == source["decodedSha256"], "Receipt reconstruction differs")
        if "--local" in sys.argv:
            require(digest(root / source["path"]) == source["sha256"], "Local source hash differs")

    timing = read(folder / "timing-comparison.json")
    wpr = read(folder / "wpr-comparison.json")
    with (folder / "wpr-metrics.csv").open(encoding="utf-8", newline="") as stream:
        metrics = list(csv.DictReader(stream))
    keyed = {(r["run"], r["save"]): r for r in metrics}
    require(len(keyed) == len(metrics) == 107, "All-run WPR coverage differs")
    complete = ["Current R1", "Current R2", "Current R3"]
    counts = {"windows": 0, "lateProfiles": 0, "schedulerPassed": 0, "schedulerFlagged": 0}
    for label, run in timing["runs"].items():
        if not label.startswith("Current"):
            continue
        name = run["run"]
        core = decoded(name, "wpr_analysis")
        receipts = {key.replace("\\", "/"): value for key, value in decoded(name, "complete_campaign_receipts")["records"].items()}
        raw = decoded(name, "quick_summary")
        require(set(core["states"]) == set(run["validSaves"]), "Window validity differs")
        if label in complete:
            provenance = decoded(name, "provenance")
            require(provenance["verified"] and provenance["source"]["commit"] == proof["sourceCommit"], "Candidate identity differs")
        for save in run["validSaves"]:
            state = core["states"][save]
            summary = run["summaries"][save]
            require(state["quick"] == summary and summary in raw, "Saved timing/health summary differs")
            recorded = receipts["stack-wait/analysis/tail-frame-counts.json"][save]
            require(recorded["windowReconstructionMatchesSavedMeans"] is True, "Raw-window means differ")
            require(recorded["rawFrameCount"] == state["frames"] == int(keyed[label, save]["frames"]), "Frame counts differ")
            window = state["window"]
            require(abs(window["tailEndTraceSeconds"] - window["tailStartTraceSeconds"] - 10) < 0.000001, "Window duration changed")
            require(abs(window["tailStartTraceSeconds"] - window["worldTraceSeconds"] - 50) < 0.000001, "Tail selection changed")
            final = summary["lifecycle"]["finalSample"]
            require(summary["lifecycle"]["finalSuccessful"] and not final["stretchActiveAtStop"] and not final["incompleteStereoAtStop"], "Terminal health differs")
            late = [v for v in summary["lifecycle"]["samples"] if v["sampleSecond"] in (49, 59, 60)]
            require(len(late) == 3, "Late profile receipt missing")
            expected = 0 if int(save) <= 10 else 1
            require(all(v["method"] == "dlss" and v["qualityMode"] == expected and v["valid"] for v in late), "Late mode differs")
            counts["lateProfiles"] += len(late)
            counts["windows"] += 1
            status = state["scheduler"]["coverageStatus"]
            require(keyed[label, save]["coverageStatus"] == status, "Coverage flag hidden")
            counts["schedulerPassed" if status == "PASS" else "schedulerFlagged"] += 1
    require(counts == dict(windows=23, lateProfiles=69, schedulerPassed=22, schedulerFlagged=1), "Campaign coverage differs")
    require(timing["runs"]["Current interrupted"]["summaries"]["13"] is None, "Missing Save 13 imputed")
    require(("Current interrupted", "13") not in keyed, "Missing WPR window imputed")
    for save, fields in timing["comparison"].items():
        for field, result in fields.items():
            values = [result["values"][label] for label in complete]
            require(result["currentThree"]["median"] == statistics.median(values), "Repeat median differs")
            require(result["currentMedianDeltaVsOriginal"] == statistics.median(values) - result["values"]["Original traced baseline"], "Baseline delta differs")
    for save, fields in wpr["comparisons"].items():
        for field, result in fields.items():
            require(result["currentMedian"] == statistics.median(result["values"][label] for label in complete), "WPR median differs")
    require(len(wpr["coverageFlags"]) == 1 and wpr["coverageFlags"][0]["save"] == "12", "Scheduler exception hidden")
    print(json.dumps(dict(status="PASS", **counts, wprWindows=len(keyed), receiptReconstructions=len(proof["sources"]), historicalCellsUnchanged=True)))


if __name__ == "__main__":
    verify()
