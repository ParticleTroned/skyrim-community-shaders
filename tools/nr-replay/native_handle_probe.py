"""Compare independent C handles with sequential per-eye reuse over identical regions."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from context_probe import EVIDENCE_ERRORS, _bounds, cases_for
from packed_replay import (TOOL_SOURCES, digest, invoke, native_campaign_lock,
                           read, rectangle, require_idle_game, write)
from packed_report import _manifest, _repeat, finite_tree, require, statistics_summary


SOURCE_NAMES = ("native_handle_probe.py", "context_probe.py", *TOOL_SOURCES)


def cases(owned, width, height):
    cases_for(owned, width, height, [0])
    require(all(min(r[2:]) >= 128 for r in owned), "handle reuse requires extents >=128")
    return [{"id": name, "kind": "separate", "rects": owned,
             "nativeHandlePolicy": policy, "resultDirectory": "runs/" + name}
            for name, policy in (("independent-before", "independent"), ("per-eye", "per-eye"),
                                 ("independent-after", "independent"))]


def prepare(args):
    _bounds(args.repeats, args.samples, args.warmup, args.seconds)
    source, executable, runtime = (p.resolve(strict=True) for p in (args.manifest, args.replay, args.runtime))
    manifest, content = _manifest(source)
    require(digest(runtime) == manifest["runtime"]["sha256"].lower(), "runtime differs from captured provider")
    first = manifest["frames"][0]["eyes"][0]["color"]
    root = args.output.resolve()
    require(not root.exists() and not args.output.is_symlink(), "output exists; preserve earlier evidence")
    plan = {"schema": "csx-nr-native-handle-probe-v1", "status": "prepared",
            "sourceManifest": {"path": str(source), "sha256": digest(source)},
            "sourceContentSha256": content,
            "replayExecutable": {"path": str(executable), "sha256": digest(executable)},
            "runtime": {"path": str(runtime), "sha256": digest(runtime)},
            "toolSources": {name: digest(Path(__file__).parent / name) for name in SOURCE_NAMES},
            "ownedRects": args.roi, "sourceExtent": [first["width"], first["height"]],
            "cases": cases(args.roi, first["width"], first["height"]),
            "repeats": args.repeats, "samples": args.samples, "warmup": args.warmup,
            "secondsPerCase": args.seconds, "maximumBaselineDriftFraction": 0.05,
            "alternateOutputSentinel": args.alternate_output_sentinel, "productionQualified": False}
    root.mkdir(parents=True, exist_ok=False)
    write(root / "plan.json", plan)
    return plan


def admit(plan, check_tools=False):
    finite_tree(plan)
    require(plan.get("schema") == "csx-nr-native-handle-probe-v1" and plan.get("status") == "prepared",
            "expected prepared native handle probe")
    _bounds(plan["repeats"], plan["samples"], plan["warmup"], plan["secondsPerCase"])
    require(type(plan.get("alternateOutputSentinel")) is bool and plan.get("maximumBaselineDriftFraction") == 0.05,
            "native handle probe admission policy changed")
    for key in ("sourceManifest", "replayExecutable", "runtime"):
        require(digest(plan[key]["path"]) == plan[key]["sha256"], key + " identity changed")
    manifest, content = _manifest(Path(plan["sourceManifest"]["path"]))
    first = manifest["frames"][0]["eyes"][0]["color"]
    require(content == plan["sourceContentSha256"] and manifest["runtime"]["sha256"].lower() == plan["runtime"]["sha256"],
            "captured input or provider changed")
    require(plan["sourceExtent"] == [first["width"], first["height"]]
            and plan["cases"] == cases(plan["ownedRects"], first["width"], first["height"]), "native handle plan changed")
    require(set(plan["toolSources"]) == set(SOURCE_NAMES), "incomplete tool identity")
    if check_tools:
        require(all(digest(Path(__file__).parent / name) == value for name, value in plan["toolSources"].items()),
                "tool source changed; prepare a new plan")
    return manifest, content


def admitted_repeat(plan, case, root, repeat, manifest, content):
    result = _repeat(case, root / case["resultDirectory"] / f"repeat-{repeat}", manifest,
                     Path(plan["sourceManifest"]["path"]), content, plan["replayExecutable"]["sha256"],
                     [tuple(r) for r in plan["ownedRects"]])
    require(len(result["steady"]) == plan["samples"] and result["checked"]["warmupSamples"] == plan["warmup"]
            and result["alternateOutputSentinel"] is plan["alternateOutputSentinel"],
            "sample count, warmup or sentinel differs from plan")
    return result


def assess_bracket(before, candidate, after, *, batch_timing_only=False):
    """Exact native RGBA and drift gates precede any qualified speed statement."""
    require(type(batch_timing_only) is bool, "invalid batch-only timing policy")
    require(before and candidate and after and len(before) == len(candidate) == len(after), "incomplete bracket")
    reference = before[0]["crops"]
    require(reference and all(reference.values()), "empty native output reference")
    records = {}
    for name, values in (("before", before), ("candidate", candidate), ("after", after)):
        require(all(s["crops"].keys() == reference.keys() and all(len(data) == len(reference[key])
                    for key, data in s["crops"].items()) for s in values), "native output coverage differs")
        require(all(s["nativeGpuMicroseconds"] is None if batch_timing_only else s["nativeGpuMicroseconds"] is not None
                    for s in values), "native GPU timing availability differs from bracket policy")
        records[name] = {"batchGpuMicroseconds": statistics_summary([s["batchGpuMicroseconds"] for s in values]),
                         "evaluationGpuMicroseconds": None if batch_timing_only else statistics_summary([s["nativeGpuMicroseconds"] for s in values]),
                         "exactRgba": all(s["crops"] == reference for s in values)}
    a, b = (records[key]["batchGpuMicroseconds"]["mean"] for key in ("before", "after"))
    require(a > 0 and b > 0, "nonpositive baseline cost")
    drift = abs(a - b) / ((a + b) / 2)
    baseline_equal = records["before"]["exactRgba"] and records["after"]["exactRgba"]
    equivalent = baseline_equal and records["candidate"]["exactRgba"]
    qualified = equivalent and drift <= 0.05
    return {"records": records, "baselineRgbaRepeatable": baseline_equal,
            "strictRgbaEquivalent": equivalent, "baselineDriftFraction": drift,
            "timingQualified": qualified,
            "candidateBatchMeanDeltaPercent":
                (records["candidate"]["batchGpuMicroseconds"]["mean"] / ((a + b) / 2) - 1) * 100 if qualified else None}


def ordered_jobs(plan):
    return [(case, repeat) for repeat in range(plan["repeats"])
            for case in (plan["cases"] if repeat % 2 == 0 else plan["cases"][::-1])]


def validate_journal(plan, root, journal, loaded):
    """Bind each measured bracket to its complete, ordered execution receipts."""
    require(journal.get("schema") == "csx-nr-native-handle-run-v1" and journal.get("status") == "complete",
            "native handle run journal is missing or incomplete")
    require(read(root / "plan.json") == plan and journal.get("planSha256") == digest(root / "plan.json"),
            "native handle journal plan identity differs")
    expected = ordered_jobs(plan)
    jobs = journal.get("jobs")
    require(isinstance(jobs, list) and len(jobs) == len(expected), "native handle journal job count differs")
    for job, (case, repeat) in zip(jobs, expected):
        output = root / case["resultDirectory"] / f"repeat-{repeat}"
        require(isinstance(job, dict) and job.get("case") == case["id"]
                and type(job.get("repeat")) is int and job["repeat"] == repeat
                and job.get("status") == "complete" and job.get("output") == str(output)
                and job.get("resultsSha256") == loaded[(case["id"], repeat)]["resultSha256"],
                "native handle journal order, completion, output path or result hash differs")


def summarize(plan, root):
    report = {"schema": "csx-nr-native-handle-report-v1", "status": "failed", "errors": [],
              "cases": [], "brackets": [], "productionQualified": False,
              "strictRgbaEquivalent": None, "timingQualified": False,
              "timingScope": "native submission GPU time includes reuse barriers; per-evaluation intervals are separate diagnostics",
              "qualityScope": "exact independent-context native RGBA crops only; no temporal or composed-image qualification"}
    try:
        manifest, content = admit(plan)
        loaded = {}
        for case in plan["cases"]:
            entry = {"id": case["id"], "nativeHandlePolicy": case["nativeHandlePolicy"], "repeats": []}
            report["cases"].append(entry)
            for repeat in range(plan["repeats"]):
                try:
                    result = admitted_repeat(plan, case, root, repeat, manifest, content)
                    loaded[(case["id"], repeat)] = result
                    entry["repeats"].append({"repeat": repeat, "status": "complete", "resultsSha256": result["resultSha256"]})
                except EVIDENCE_ERRORS as error:
                    report["errors"].append(f"{case['id']} repeat-{repeat}: {error}")
                    entry["repeats"].append({"repeat": repeat, "status": "failed_or_missing", "reason": str(error)})
        journal = read(root / "run.json") if (root / "run.json").exists() else {}
        report["executionStatus"] = journal.get("status", "journal_missing")
        validate_journal(plan, root, journal, loaded)
        for repeat in range(plan["repeats"]):
            if all((case["id"], repeat) in loaded for case in plan["cases"]):
                bracket = assess_bracket(*(loaded[(case["id"], repeat)]["steady"] for case in plan["cases"]))
                report["brackets"].append({"repeat": repeat, **bracket})
        report["status"] = "complete" if not report["errors"] and journal.get("status") == "complete" else "failed"
        reference = loaded.get(("independent-before", 0), {}).get("steady", [])
        report["baselineRepeatableAcrossProcesses"] = (
            all(sample["crops"] == reference[0]["crops"] for (identity, _), result in loaded.items()
                if identity != "per-eye" for sample in result["steady"])
            if report["status"] == "complete" and reference else None)
        report["strictRgbaEquivalent"] = (all(b["strictRgbaEquivalent"] for b in report["brackets"])
                                           and report["baselineRepeatableAcrossProcesses"] is True
                                           if report["status"] == "complete" else None)
        report["timingQualified"] = (all(b["timingQualified"] for b in report["brackets"])
                                      and report["baselineRepeatableAcrossProcesses"] is True
                                      if report["status"] == "complete" else False)
    except EVIDENCE_ERRORS as error:
        report["errors"].append(str(error))
    return report


def execute(plan, root):
    manifest, content = admit(plan, check_tools=True)
    jobs = ordered_jobs(plan)
    journal_path = root / "run.json"
    require(not journal_path.exists() and all(not (root / c["resultDirectory"] / f"repeat-{r}").exists() for c, r in jobs),
            "existing run preserved; prepare a new probe")
    journal = {"schema": "csx-nr-native-handle-run-v1", "planSha256": digest(root / "plan.json"), "status": "running", "jobs": []}
    write(journal_path, journal)
    try:
        for case, repeat in jobs:
            require_idle_game()
            output = root / case["resultDirectory"] / f"repeat-{repeat}"
            job = {"case": case["id"], "repeat": repeat, "status": "running", "output": str(output)}
            journal["jobs"].append(job)
            write(journal_path, journal)
            print(f"{case['id']} repeat {repeat}", flush=True)
            arguments = [plan["replayExecutable"]["path"], "--manifest", plan["sourceManifest"]["path"],
                         "--runtime", plan["runtime"]["path"], "--output", output,
                         "--rects", json.dumps(case["rects"]), "--native-handle-policy", case["nativeHandlePolicy"],
                         "--samples", plan["samples"], "--warmup", plan["warmup"], "--seconds", plan["secondsPerCase"]]
            if plan["alternateOutputSentinel"]:
                arguments.append("--alternate-output-sentinel")
            invoke(arguments, output, plan["secondsPerCase"])
            result = admitted_repeat(plan, case, root, repeat, manifest, content)
            job.update(status="complete", resultsSha256=result["resultSha256"])
            write(journal_path, journal)
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
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--alternate-output-sentinel", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.report:
        root = args.report.resolve(strict=True).parent
        write(root / "summary.json", summarize(read(args.report), root))
        return
    require(all((args.manifest, args.replay, args.runtime, args.output, args.roi)), "manifest, replay, runtime, output and roi required")
    plan = prepare(args)
    if not args.prepare_only:
        with native_campaign_lock():
            execute(plan, args.output.resolve())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Native handle probe stopped: {error}", file=sys.stderr)
        sys.exit(1)
