"""Verify the portable publication without a game, trace export or build."""
from pathlib import Path
import csv
import hashlib
import json
import re


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DOC = HERE.parent


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def packed(value):
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False)


def verify():
    coverage = read(HERE / "coverage.json")
    cells = {}
    units = {}
    csv.field_size_limit(1024 * 1024 * 1024)
    for partition in coverage["partitions"]:
        path = DOC / partition["file"]
        assert digest(path) == partition["sha256"]
        assert path.stat().st_size == partition["bytes"] < 100 * 1024 * 1024
        with path.open(encoding="utf-8", newline="") as stream:
            rows = csv.reader(stream)
            header = next(rows)
            assert header[2:] == partition["runs"]
            for row in rows:
                units[row[0]] = row[1]
                for column, run in enumerate(header[2:], 2):
                    cells[run, row[0]] = row[column]
    receipts = {}
    for source in coverage["sourceReconstruction"]:
        run = source["run"]
        if run not in receipts:
            receipts[run] = json.loads(cells[run, "complete_publication_receipts"])
        value = receipts[run][source["selector"]]
        assert hashlib.sha256(packed(value).encode()).hexdigest() == source["portableSha256"]
    for entry in coverage["numericAudit"]:
        assert json.loads(cells[entry["run"], entry["metric"]]) == entry["value"]
        assert units[entry["metric"]] == entry["unit"]
    for name, sha256 in coverage["historicalLedgersUnchanged"].items():
        assert digest(DOC / name) == sha256
    for (run, metric), cell in cells.items():
        if run == coverage["reportColumn"] and metric.startswith("publication_report/"):
            path = DOC / metric.removeprefix("publication_report/")
            actual = read(path) if path.suffix == ".json" else path.read_text(encoding="utf-8-sig")
            assert json.loads(cell) == actual, str(path.relative_to(ROOT))
    validation = read(DOC / "matched-pose-comparison-20260917/validation.json")
    for entry in validation["outputs"]:
        relative = entry["path"].replace("${CSX_ROOT}\\", "").replace("${CSX_ROOT}/", "").replace("\\", "/")
        path = ROOT / relative
        assert digest(path) == entry["sha256"] and path.stat().st_size == entry["bytes"]
    reassessment = read(HERE / "scheduler-reassessment.json")
    for cohort in reassessment["cohorts"].values():
        assert all(row["current10msPassed"] == (abs(row["errorMs"]) <= 10) for row in cohort["rows"])
        assert cohort["currentPassed"] == sum(row["current10msPassed"] for row in cohort["rows"])
    print(json.dumps({"completeSources": len(coverage["sourceReconstruction"]),
                      "numericValues": len(coverage["numericAudit"]),
                      "reportOutputs": len(validation["outputs"]), "passed": True}))


if __name__ == "__main__":
    verify()
