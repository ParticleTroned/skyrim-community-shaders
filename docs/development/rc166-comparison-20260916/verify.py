"""Verify the portable RC166 snapshot and unchanged prior comparison cells."""

import csv
import hashlib
import json
from pathlib import Path
import re


def require(condition, message):
    if not condition:
        raise ValueError(message)


def packed(value):
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False)


def verify():
    folder = Path(__file__).resolve().parent
    proof = json.loads((folder / "coverage.json").read_text(encoding="utf-8"))
    ledger = folder.parent / proof["ledger"]
    require(hashlib.sha256(ledger.read_bytes()).hexdigest() == proof["ledgerSha256"], "Ledger hash differs")
    csv.field_size_limit(100 * 1024 * 1024)
    with ledger.open(encoding="utf-8", newline="") as stream:
        table = list(csv.reader(stream))
    columns = table[0][2:]
    require(columns == proof["runColumns"] and len(set(columns)) == 11, "Run columns differ")
    require(len(table) - 1 == proof["metricRows"], "Metric rows differ")
    rows = {r[0]: r for r in table[1:]}
    require(len(rows) == len(table) - 1, "Duplicate metric rows")
    for row in table[1:]:
        require(len(row) == 13 and all(v != "" for v in row), "Incomplete row")
        if row[1] == "json":
            for cell in row[2:]:
                json.loads(cell)
    prior = folder.parent / "vr-render-scale-ledger-0004-investigation.csv"
    with prior.open(encoding="utf-8", newline="") as stream:
        original = list(csv.reader(stream))
    require(table[0][:10] == original[0], "Prior column identity changed")
    for row in original[1:]:
        require(rows[row[0]][:10] == row, "Prior measurement cell changed")
    for name, digest in proof["historicalLedgers"].items():
        require(hashlib.sha256((folder.parent / name).read_bytes()).hexdigest() == digest, "Historical ledger changed")
    for source in proof["sources"]:
        index = columns.index(source["run"]) + 2
        value = json.loads(rows[source["metric"]][index])
        selector = source.get("selector")
        if selector == "markers":
            value = value["markers"]
        elif selector:
            value = value["records"][selector.removeprefix("records/")]
        require(hashlib.sha256(packed(value).encode()).hexdigest() == source["decodedSha256"], "Source reconstruction differs")
    with (folder / "wpr-metrics.csv").open(encoding="utf-8", newline="") as stream:
        wpr_rows = list(csv.DictReader(stream))
    with (folder / "tail-cadence.csv").open(encoding="utf-8", newline="") as stream:
        cadence_rows = list(csv.DictReader(stream))
    wpr = {(r["run"], r["save"]): r for r in wpr_rows}
    cadence = {(r["run"], r["save"]): r for r in cadence_rows}
    require(len(wpr_rows) == len(wpr) == len(cadence_rows) == len(cadence) == 66, "Window coverage differs")
    for index in range(2, 13):
        core = json.loads(rows["wpr_analysis"][index])
        for save, state in core["states"].items():
            key = (core["label"], save)
            require(int(wpr[key]["frames"]) == int(cadence[key]["frames"]) == state["frames"], "Frame count differs")
            for summary_name, wpr_name, cadence_name in [
                ("cpuTailMeanMs", "cpuMs", "cpuMeanMs"),
                ("gpuTailMeanMs", "gpuMs", "gpuMeanMs"),
            ]:
                require(float(wpr[key][wpr_name]) == float(cadence[key][cadence_name]) == state["quick"][summary_name], "CSV mean differs")
            require(wpr[key]["coveragePassed"] == str(state["scheduler"]["coveragePassed"]), "Coverage failure hidden")
    checked = 0
    for index in range(10, 13):
        quick = json.loads(rows["quick_summary"][index])
        core = json.loads(rows["wpr_analysis"][index])
        provenance = json.loads(rows["provenance"][index])
        require(provenance["verified"] is True, "Unverified build")
        require(provenance["source"]["commit"] == "2eef86720e5c6a48e6fa9ad93506a1f2093d0f2f", "Unexpected RC166 source")
        for save, summary in zip(["08", "11", "09", "12", "10", "13"], quick):
            require(re.match(r"Save(\d+)_", summary["name"])[1].zfill(2) == save, "Save order differs")
            require(core["states"][save]["quick"] == summary, "WPR and fpsVR summary differ")
            for field, value in summary.items():
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    metric = f"save_{save}_{field}"
                    if metric in rows:
                        require(float(rows[metric][index]) == value, "Numeric timing differs")
            checked += 1
    require(checked == 18, "Expected eighteen RC166 holds")
    print(f"PASS: 11 runs; 18 new holds; {len(rows)} rows; {len(proof['sources'])} source reconstructions; all prior cells unchanged.")


if __name__ == "__main__":
    verify()
