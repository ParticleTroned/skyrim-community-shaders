"""Verify the committed culling evidence without local traces or game access."""

import csv
import hashlib
import json
from pathlib import Path
import re


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify():
    folder = Path(__file__).resolve().parent
    proof = json.loads((folder / "coverage.json").read_text(encoding="utf-8"))
    ledger = folder.parent / proof["ledger"]
    require(hashlib.sha256(ledger.read_bytes()).hexdigest() == proof["ledgerSha256"], "Ledger hash differs")
    csv.field_size_limit(ledger.stat().st_size)
    with ledger.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        columns = reader.fieldnames[2:]
        entries = list(reader)
    require(len(columns) == 8 and len(set(columns)) == 8, "Run columns differ")
    require(len(entries) == proof["metricRows"], "Metric row count differs")
    rows = {r["metric"]: r for r in entries}
    require(len(rows) == len(entries), "Duplicate metric rows")
    for row in entries:
        require(None not in row and all(row[c] != "" for c in columns), "Incomplete row")
        if row["unit"] == "json":
            for column in columns:
                json.loads(row[column])
    with (folder / "wpr-metrics.csv").open(encoding="utf-8", newline="") as stream:
        wpr_rows = list(csv.DictReader(stream))
    with (folder / "tail-cadence.csv").open(encoding="utf-8", newline="") as stream:
        cadence_rows = list(csv.DictReader(stream))
    require(len(wpr_rows) == len(cadence_rows) == 48, "Expected 48 WPR and cadence windows")
    wpr = {(r["run"], r["save"]): r for r in wpr_rows}
    cadence = {(r["run"], r["save"]): r for r in cadence_rows}
    require(len(wpr) == len(cadence) == 48, "Duplicate window identity")
    checked = 0
    for column in columns:
        core = json.loads(rows["wpr_analysis"][column])
        quick = json.loads(rows["quick_summary"][column])
        provenance = json.loads(rows["provenance"][column])
        require(provenance["verified"] is True, "Unverified build")
        require(provenance["source"]["commit"] == "503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc", "Different tested source")
        require(quick == [core["states"][s]["quick"] for s in proof["executionOrder"]], "Summary reconstruction differs")
        for save, summary in zip(proof["executionOrder"], quick):
            require(re.match(r"Save(\d+)_", summary["name"])[1].zfill(2) == save, "Positional save-label mismatch")
            require(summary["lifecycle"]["finalSuccessful"] is True, "Final lifecycle differs")
            key = (core["label"], save)
            require(int(wpr[key]["frames"]) == int(cadence[key]["frames"]) == core["states"][save]["frames"], "Frame counts differ")
            for metric, csv_name, cadence_name in [("cpuTailMeanMs", "cpuMs", "cpuMeanMs"), ("gpuTailMeanMs", "gpuMs", "gpuMeanMs")]:
                require(float(wpr[key][csv_name]) == float(cadence[key][cadence_name]) == summary[metric], "Mean differs")
            for field, value in summary.items():
                metric = f"save_{save}_{field}"
                if metric not in rows:
                    continue
                cell = rows[metric][column]
                if value is None:
                    require(cell == "not reached (saved null)", "Null substituted")
                elif isinstance(value, (int, float)) and not isinstance(value, bool):
                    require(float(cell) == value, "Scalar timing differs")
            for field, value in summary["renderSettling"].items():
                metric = f"save_{save}_renderSettling_{field}"
                if metric in rows:
                    require(float(rows[metric][column]) == value, "Render timing differs")
            checked += 1
    for name, expected in proof["historicalLedgers"].items():
        require(hashlib.sha256((folder.parent / name).read_bytes()).hexdigest() == expected, "Historical ledger changed")
    print(f"PASS: {len(columns)} runs, {checked} save windows, {len(entries)} metric rows; summaries, scalar timings, CSV means and historical ledgers match.")


if __name__ == "__main__":
    verify()
