"""Isolate native C valid-domain effects over unchanged captured textures."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from packed_input import digest as bytes_digest
from packed_replay import (TOOL_SOURCES, digest, invoke, native_campaign_lock,
                           read, rectangle, require_idle_game, write)
from packed_report import (_contains, _difference, _manifest, _rect, _repeat,
                           finite_tree, require, statistics_summary, uint)


SOURCE_NAMES = ("context_probe.py", *TOOL_SOURCES)
ALIGNMENT = 64
HALOS = (0, 32, 64, 128, 256)
EVIDENCE_ERRORS = (OSError, ValueError, KeyError, TypeError, IndexError, AttributeError)


def expanded(rect: list[int], halo: int, width: int, height: int) -> list[int]:
    """Expand in original coordinates and align outward, clipping to the source."""
    x, y, w, h = _rect(rect)
    require(_contains((0, 0, width, height), (x, y, w, h)), "owned rectangle exceeds source")
    require(uint(halo, 16384), "invalid halo")
    left = max(0, x - halo) // ALIGNMENT * ALIGNMENT
    top = max(0, y - halo) // ALIGNMENT * ALIGNMENT
    right = min(width, (x + w + halo + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT)
    bottom = min(height, (y + h + halo + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT)
    return [left, top, right - left, bottom - top]


def cases_for(owned: list[list[int]], width: int, height: int, halos: list[int]) -> list[dict]:
    """Keep ownership fixed while independently changing each native valid domain."""
    require(uint(width, 16384) and width >= 64 and uint(height, 16384) and height >= 64, "invalid source extent")
    require(isinstance(owned, list) and 2 <= len(owned) <= 4, "context probe requires two to four owned rectangles")
    require(isinstance(halos, list) and 1 <= len(halos) <= len(HALOS)
            and len(set(halos)) == len(halos) and all(type(h) is int and h in HALOS for h in halos), "invalid halo set")
    regions = [_rect(rect) for rect in owned]
    minimum = 128 if len(regions) > 2 else 64
    for index, rect in enumerate(regions):
        require(_contains((0, 0, width, height), rect) and min(rect[2:]) >= minimum, "owned rectangle exceeds source or safety minimum")
        x, y, w, h = rect
        require(all(x + w <= a or a + c <= x or y + h <= b or b + d <= y
                    for a, b, c, d in regions[:index]), "owned rectangles overlap")
    indices = list(range(len(owned)))
    left, top = min(r[0] for r in regions), min(r[1] for r in regions)
    hull = [left, top, max(r[0] + r[2] for r in regions) - left,
            max(r[1] + r[3] for r in regions) - top]
    cases = []

    def add(identity, rects, selected, probe, halo=None, alignment=0):
        case = {"id": identity, "kind": "separate" if identity == "tight" else "enclosing",
                "probe": probe, "rects": rects, "ownedIndices": selected, "halo": halo,
                "alignment": alignment, "resultDirectory": f"runs/{identity}"}
        case["caseIdentitySha256"] = bytes_digest(json.dumps(case, sort_keys=True, separators=(",", ":")).encode())
        cases.append(case)

    add("full", [[0, 0, width, height]], indices, "full_reference")
    add("tight", [list(r) for r in regions], indices, "tight_reference")
    add("enclosing-raw", [hull], indices, "enclosing_raw")
    for index, rect in enumerate(regions):
        add(f"individual-raw-r{index}", [list(rect)], [index], "individual_raw")
    for halo in halos:
        for index, rect in enumerate(regions):
            add(f"individual-h{halo}-r{index}", [expanded(rect, halo, width, height)], [index],
                "individual_aligned", halo, ALIGNMENT)
        add(f"enclosing-h{halo}", [expanded(hull, halo, width, height)], indices,
            "enclosing_aligned", halo, ALIGNMENT)
    return cases


def _bounds(repeats, samples, warmup, seconds):
    require(type(repeats) is int and 1 <= repeats <= 5 and type(samples) is int and 1 <= samples <= 64
            and type(warmup) is int and 1 <= warmup <= 32 and type(seconds) is int and 1 <= seconds <= 600,
            "probe bounds: repeats1..5, samples1..64, warmup1..32, seconds1..600")


def prepare_plan(args) -> dict:
    _bounds(args.repeats, args.samples, args.warmup, args.seconds)
    halos = [int(item) for item in args.halos.split(",")]
    source, executable, runtime = (p.resolve(strict=True) for p in (args.manifest, args.replay, args.runtime))
    manifest, content_hash = _manifest(source)
    runtime_hash = digest(runtime)
    require(runtime_hash == manifest["runtime"]["sha256"].lower(), "runtime differs from captured provider")
    first = manifest["frames"][0]["eyes"][0]["color"]
    cases = cases_for(args.roi, first["width"], first["height"], halos)
    root = args.output.resolve()
    require(not root.exists() and not args.output.is_symlink(), "output exists; preserve earlier evidence")
    plan = {"schema": "csx-nr-context-probe-v1", "status": "prepared",
            "sourceManifest": {"path": str(source), "sha256": digest(source)},
            "sourceContentSha256": content_hash,
            "replayExecutable": {"path": str(executable), "sha256": digest(executable)},
            "runtime": {"path": str(runtime), "sha256": runtime_hash},
            "toolSources": {name: digest(Path(__file__).parent / name) for name in SOURCE_NAMES},
            "ownedRects": args.roi, "sourceExtent": [first["width"], first["height"]],
            "halos": halos, "repeats": args.repeats, "samples": args.samples,
            "warmup": args.warmup, "secondsPerCase": args.seconds, "cases": cases,
            "historyPolicy": "static_reset", "inputMutation": False, "productionQualified": False,
            "alignmentMeaning": "existing_CSX_provider_ROI_policy_not_verified_native_CNN_stride",
            "alternateOutputSentinel": getattr(args, "alternate_output_sentinel", False)}
    root.mkdir(parents=True, exist_ok=False)
    write(root / "plan.json", plan)
    return plan


def _admit_plan(plan: dict, check_tools: bool = False):
    finite_tree(plan)
    require(plan.get("schema") == "csx-nr-context-probe-v1" and plan.get("status") == "prepared", "expected a prepared context probe")
    _bounds(plan["repeats"], plan["samples"], plan["warmup"], plan["secondsPerCase"])
    require(type(plan.get("alternateOutputSentinel", False)) is bool, "invalid output sentinel selection")
    for key in ("sourceManifest", "replayExecutable", "runtime"):
        require(digest(plan[key]["path"]) == plan[key]["sha256"], key + " identity changed")
    source = Path(plan["sourceManifest"]["path"])
    manifest, content_hash = _manifest(source)
    require(content_hash == plan["sourceContentSha256"] and plan["runtime"]["sha256"] == manifest["runtime"]["sha256"].lower(),
            "captured input/runtime identity changed")
    first = manifest["frames"][0]["eyes"][0]["color"]
    extent = [first["width"], first["height"]]
    require(plan["sourceExtent"] == extent and plan["cases"] == cases_for(plan["ownedRects"], *extent, plan["halos"]),
            "prepared context geometry changed")
    require(set(plan["toolSources"]) == set(SOURCE_NAMES), "tool source identity set incomplete")
    if check_tools:
        require(all(digest(Path(__file__).parent / name) == expected for name, expected in plan["toolSources"].items()),
                "probe source changed; prepare a fresh plan")
    return manifest, content_hash


def _admit_repeat(plan, case, root, repeat, manifest, content_hash):
    owned = [tuple(plan["ownedRects"][index]) for index in case["ownedIndices"]]
    result = _repeat(case, root / case["resultDirectory"] / f"repeat-{repeat}", manifest,
                     Path(plan["sourceManifest"]["path"]), content_hash, plan["replayExecutable"]["sha256"], owned)
    require(len(result["steady"]) == plan["samples"] and result["checked"]["warmupSamples"] == plan["warmup"],
            "native sample count differs from probe plan")
    require(result["alternateOutputSentinel"] is plan.get("alternateOutputSentinel", False),
            "native output sentinel differs from probe plan")
    return result


def _compare(sample, reference, indices):
    return [{"eye": eye, "ownedIndex": indices[local],
             **_difference(reference["crops"][(eye, indices[local])], data)}
            for (eye, local), data in sample["crops"].items()]


def summarize(plan: dict, root: Path) -> dict:
    """Preserve partial evidence and compare each owned crop to both fresh references."""
    report = {"schema": "csx-nr-context-report-v1", "status": "failed", "errors": [], "cases": [],
              "productionQualified": False, "inputMutation": False, "individualCostSums": [],
              "timingScope": "native GPU evaluation intervals only; preparation, upload, readback and composition excluded",
              "qualityScope": "owned RGB byte comparisons; no perceptual, temporal or stereo qualification"}
    try:
        report["analysisToolSources"] = {name: digest(Path(__file__).parent / name) for name in SOURCE_NAMES}
        manifest, content_hash = _admit_plan(plan)
        loaded = {}
        for case in plan["cases"]:
            entry = {"id": case["id"], "caseIdentitySha256": case["caseIdentitySha256"],
                     "rects": case["rects"], "ownedIndices": case["ownedIndices"], "repeats": [],
                     "comparisonsToFull": [], "comparisonsToTight": []}
            report["cases"].append(entry)
            loaded[case["id"]] = {}
            for repeat in range(plan["repeats"]):
                try:
                    result = _admit_repeat(plan, case, root, repeat, manifest, content_hash)
                    loaded[case["id"]][repeat] = result
                    entry["repeats"].append({"repeat": repeat, "status": "complete", "resultsSha256": result["resultSha256"],
                                             "nativeGpuMicroseconds": statistics_summary([s["nativeGpuMicroseconds"] for s in result["steady"]])})
                except EVIDENCE_ERRORS as error:
                    reason = str(error)
                    entry["repeats"].append({"repeat": repeat, "status": "failed_or_missing", "reason": reason})
                    report["errors"].append(f"{case['id']} repeat-{repeat}: {reason}")
            samples = [s["nativeGpuMicroseconds"] for result in loaded[case["id"]].values() for s in result["steady"]]
            entry["nativeGpuMicroseconds"] = statistics_summary(samples)
            entry["status"] = "complete" if len(loaded[case["id"]]) == plan["repeats"] else "partial" if samples else "unavailable"
        for case, entry in zip(plan["cases"], report["cases"]):
            for repeat, result in loaded[case["id"]].items():
                for key, label in (("full", "Full"), ("tight", "Tight")):
                    baseline = loaded[key].get(repeat)
                    if not baseline:
                        entry["comparisonsTo" + label].append({"repeat": repeat, "status": "unavailable", "reason": "same-repeat reference missing"})
                        continue
                    reference = baseline["steady"][0]
                    for sample in result["steady"]:
                        entry["comparisonsTo" + label].extend(
                            {"repeat": repeat, "iteration": sample["iteration"], "status": "complete", **difference}
                            for difference in _compare(sample, reference, case["ownedIndices"]))
            for label in ("Full", "Tight"):
                comparisons = entry["comparisonsTo" + label]
                entry["exactRgbTo" + label] = (all(c.get("rgbExact") is True for c in comparisons)
                                                    if comparisons and entry["status"] == "complete"
                                                    and all(c.get("status") == "complete" for c in comparisons) else None)
        for name in ("full", "tight"):
            first = loaded[name].get(0)
            differences = []
            if first:
                for repeat, result in loaded[name].items():
                    for sample in result["steady"]:
                        differences.extend({"repeat": repeat, "iteration": sample["iteration"], **d}
                                           for d in _compare(sample, first["steady"][0], list(range(len(plan["ownedRects"])))))
            report[name + "ReferenceRepeatability"] = {"comparisons": differences,
                "exactRgb": all(d["rgbExact"] for d in differences) if len(loaded[name]) == plan["repeats"] and differences else None}
        for halo in [None, *plan["halos"]]:
            identities = [f"individual-raw-r{i}" if halo is None else f"individual-h{halo}-r{i}"
                          for i in range(len(plan["ownedRects"]))]
            group = {"halo": halo, "alignment": 0 if halo is None else ALIGNMENT, "cases": identities,
                     "semantics": "sum_of_separately_measured_case_means_not_one_batched_measurement", "repeats": []}
            for repeat in range(plan["repeats"]):
                if all(repeat in loaded[name] for name in identities):
                    means = [statistics_summary([s["nativeGpuMicroseconds"] for s in loaded[name][repeat]["steady"]])["mean"] for name in identities]
                    group["repeats"].append({"repeat": repeat, "status": "complete", "caseMeanMicroseconds": means,
                                             "sumOfMeansMicroseconds": sum(means)})
                else:
                    group["repeats"].append({"repeat": repeat, "status": "unavailable", "reason": "individual case evidence missing"})
            report["individualCostSums"].append(group)
        journal_path = root / "run.json"
        journal = read(journal_path) if journal_path.exists() else {}
        report["executionStatus"] = journal.get("status", "journal_missing")
        report["status"] = ("failed" if journal.get("status") == "failed" else "partial"
                            if report["errors"] or journal.get("status") != "complete" else "complete")
        if journal.get("reason"):
            report["executionReason"] = journal["reason"]
    except EVIDENCE_ERRORS as error:
        report["errors"].append(str(error))
    return report


def execute(plan: dict, root: Path) -> dict:
    manifest, content_hash = _admit_plan(plan, check_tools=True)
    journal_path = root / "run.json"
    jobs = [(case, repeat) for repeat in range(plan["repeats"])
            for case in (plan["cases"] if repeat % 2 == 0 else plan["cases"][::-1])]
    require(not journal_path.exists() and all(not (root / case["resultDirectory"] / f"repeat-{repeat}").exists()
                                             for case, repeat in jobs), "existing run preserved; prepare a new probe")
    journal = {"schema": "csx-nr-context-run-v1", "planSha256": digest(root / "plan.json"), "status": "running", "jobs": []}
    write(journal_path, journal)
    try:
        for case, repeat in jobs:
            require_idle_game()
            output = root / case["resultDirectory"] / f"repeat-{repeat}"
            job = {"case": case["id"], "repeat": repeat, "status": "running", "output": str(output),
                   "caseIdentitySha256": case["caseIdentitySha256"]}
            journal["jobs"].append(job)
            write(journal_path, journal)
            print(f"{case['id']} repeat {repeat}", flush=True)
            arguments = [plan["replayExecutable"]["path"], "--manifest", plan["sourceManifest"]["path"],
                    "--runtime", plan["runtime"]["path"], "--output", output, "--rects", json.dumps(case["rects"]),
                    "--samples", plan["samples"], "--warmup", plan["warmup"], "--seconds", plan["secondsPerCase"]]
            if plan.get("alternateOutputSentinel", False):
                arguments.append("--alternate-output-sentinel")
            invoke(arguments, output, plan["secondsPerCase"])
            result = _admit_repeat(plan, case, root, repeat, manifest, content_hash)
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
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--roi", action="append", type=rectangle)
    parser.add_argument("--halos", default="0,64,128,256")
    parser.add_argument("--samples", type=int, default=6)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--alternate-output-sentinel", action="store_true",
                        help="Use the native complementary RGBA8 marker; ambiguous footprints still fail")
    parser.add_argument("--report", type=Path, help="Rebuild summary for an existing plan.json without native calls")
    args = parser.parse_args()
    if args.report:
        root = args.report.resolve(strict=True).parent
        write(root / "summary.json", summarize(read(args.report), root))
        return
    require(all((args.manifest, args.replay, args.runtime, args.output, args.roi)), "manifest, replay, runtime, output and roi are required")
    plan = prepare_plan(args)
    if not args.prepare_only:
        with native_campaign_lock():
            execute(plan, args.output.resolve())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Context probe stopped: {error}", file=sys.stderr)
        sys.exit(1)
