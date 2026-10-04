"""Bounded offline C atlas feasibility campaign; never deploys or controls Skyrim."""

import argparse
from contextlib import contextmanager
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from packed_input import prepare


TOOL_SOURCES = ("packed_input.py", "packed_replay.py", "packed_report.py",
                "canvas_input.py",
                "../nr-color/replay_report.py", "../nr-color/transaction_evidence.py")


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def read(path):
    path = Path(path)
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError(f"Oversized JSON: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def rectangle(text):
    try:
        result = [int(value) for value in text.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("ROI coordinates must be integers") from error
    if len(result) != 4 or any(value < 0 or value > 16384 for value in result) or min(result[2:]) < 64:
        raise argparse.ArgumentTypeError("ROI needs x,y,width,height, extents >=64, coordinates <=16384")
    return result


def invoke(arguments, output, seconds):
    if output.exists():
        raise ValueError(f"Preserve existing evidence: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    # stdout/stderr are outside the native-owned output directory, including on timeout.
    with output.with_suffix(".stdout.txt").open("x", encoding="utf-8") as stdout, \
            output.with_suffix(".stderr.txt").open("x", encoding="utf-8") as stderr:
        completed = subprocess.run([str(arg) for arg in arguments], stdout=stdout, stderr=stderr,
                                   timeout=seconds + 60, check=False)
    if completed.returncode:
        raise RuntimeError(f"Replay failed ({completed.returncode}); inspect {output}")


def prepare_campaign(args):
    if not 1 <= args.repeats <= 5:
        raise ValueError("repeats must be 1..5")
    halos = [int(value) for value in args.halos.split(",")]
    if not halos or len(set(halos)) != len(halos) or any(value not in (0, 32, 64, 128, 256) for value in halos):
        raise ValueError("halos must be distinct values from 0,32,64,128,256")
    source, executable, root = args.manifest.resolve(strict=True), args.replay.resolve(strict=True), args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    campaign = {"schema": "csx-nr-packed-campaign-v1", "status": "preparing",
                "sourceManifest": {"path": str(source), "sha256": digest(source)},
                "replayExecutable": {"path": str(executable), "sha256": digest(executable)},
                "toolSources": {name: digest(Path(__file__).parent / name) for name in TOOL_SOURCES},
                "rects": args.roi, "repeats": args.repeats, "cases": [],
                "scope": "offline_stateless_C_native_cost_and_owned_output_feasibility",
                "productionQualified": False, "gpuPackScatterTimed": False}
    write(root / "campaign.json", campaign)
    try:
        validation = root / "source-validation"
        invoke([executable, "--manifest", source, "--output", validation, "--validate-input",
                "--rects", json.dumps(args.roi)], validation, 30)
        for eye in read(source)["frames"][0]["eyes"]:
            color = eye["color"]
            if color["format"] != 28 or eye["featureUpscaling"] or any(
                    (eye[role]["width"], eye[role]["height"]) != (color["width"], color["height"])
                    for role in ("depth", "motion")):
                raise ValueError("Native atlas campaign currently requires RGBA8 C with equal source/guide grids")
        x, y = min(r[0] for r in args.roi), min(r[1] for r in args.roi)
        hull = [x, y, max(r[0] + r[2] for r in args.roi) - x, max(r[1] + r[3] for r in args.roi) - y]
        for kind, rects in (("separate", args.roi), ("enclosing", [hull])):
            campaign["cases"].append({"id": kind, "kind": kind, "manifest": str(source),
                                      "manifestSha256": digest(source), "rects": rects,
                                      "resultDirectory": f"runs/{kind}"})
        for halo in halos:
            for reverse in (False, True):
                case_id = f"atlas-h{halo}" + ("-reversed" if reverse else "")
                directory = root / "inputs" / case_id
                if args.canvas == "strip":
                    proof = prepare(source, directory, args.roi, halo, reverse)
                else:
                    from canvas_input import prepare_canvas

                    proof = prepare_canvas(source, directory, args.roi, halo, reverse, args.canvas)
                manifest = directory / "manifest.json"
                atlas = read(manifest)["frames"][0]["eyes"][0]["color"]
                rects = [[0, 0, atlas["width"], atlas["height"]]]
                campaign["cases"].append({"id": case_id, "kind": "atlas", "manifest": str(manifest),
                                          "manifestSha256": digest(manifest), "rects": rects,
                                          "tiles": proof["tiles"], "halo": halo, "reverse": reverse,
                                          "canvas": args.canvas,
                                          "packingReceipt": str(directory / "packing.json"),
                                          "resultDirectory": f"runs/{case_id}"})
                validation = root / "validation" / case_id
                invoke([executable, "--manifest", manifest, "--output", validation, "--validate-input",
                        "--rects", json.dumps(rects)], validation, 30)
        campaign["status"] = "prepared"
    except Exception as error:
        campaign.update(status="failed_preparation", reason=str(error))
        raise
    finally:
        write(root / "campaign.json", campaign)


def load_campaign(path):
    campaign = read(path)
    if campaign.get("schema") != "csx-nr-packed-campaign-v1" or campaign.get("status") != "prepared":
        raise ValueError("Expected a successfully prepared immutable campaign")
    repeats, cases = campaign.get("repeats"), campaign.get("cases")
    if type(repeats) is not int or not 1 <= repeats <= 5 or not isinstance(cases, list) or not 2 <= len(cases) <= 12:
        raise ValueError("Campaign repeat/case bounds changed")
    identities, outputs = set(), set()
    for case in cases:
        identity = case.get("id", "")
        if not isinstance(identity, str) or not re.fullmatch(r"separate|enclosing|atlas-h(?:0|32|64|128|256)(?:-reversed)?", identity) \
                or identity in identities:
            raise ValueError("Campaign case identity is unsafe or duplicated")
        identities.add(identity)
        rects = case.get("rects")
        if not isinstance(rects, list) or not 1 <= len(rects) <= 4 or any(
                not isinstance(rect, list) or len(rect) != 4 or any(type(v) is not int for v in rect)
                or any(v < 0 or v > 16384 for v in rect) or min(rect[2:]) < 64 for rect in rects):
            raise ValueError("Campaign rectangles exceed experiment bounds")
        if case.get("kind") != (identity if identity in ("separate", "enclosing") else "atlas"):
            raise ValueError("Campaign case kind differs from identity")
    if not {"separate", "enclosing"}.issubset(identities):
        raise ValueError("Campaign needs both reference cases")
    for identity in (campaign["sourceManifest"], campaign["replayExecutable"]):
        if digest(identity["path"]) != identity["sha256"]:
            raise ValueError("Campaign source/executable identity changed")
    if set(campaign["toolSources"]) != set(TOOL_SOURCES):
        raise ValueError("Campaign tool source identity set is incomplete")
    for name, expected in campaign["toolSources"].items():
        if digest(Path(__file__).parent / name) != expected:
            raise ValueError("Campaign tool source changed; prepare a new campaign")
    for case in campaign["cases"]:
        if digest(case["manifest"]) != case["manifestSha256"]:
            raise ValueError("Prepared input manifest changed")
        directory = (path.parent / case["resultDirectory"]).resolve()
        if not directory.is_relative_to(path.parent.resolve()) or directory in outputs:
            raise ValueError("Result directory escapes campaign or is duplicated")
        outputs.add(directory)
    return campaign


def require_idle_game():
    if os.name != "nt":
        raise RuntimeError("Native replay requires Windows")
    listing = subprocess.run(["tasklist", "/FO", "CSV", "/NH"],
                             capture_output=True, text=True, timeout=15, check=True)
    rows = list(csv.reader(listing.stdout.splitlines()))
    if not rows or any(len(row) < 2 for row in rows):
        raise RuntimeError("Windows process inventory is unavailable; no replay was started")
    names = {row[0].lower() for row in rows}
    if names.intersection({"skyrimvr.exe", "skyrimse.exe", "csx_nr_replay.exe"}):
        raise RuntimeError("Skyrim or another native replay is running; no isolated GPU replay was started")


@contextmanager
def native_campaign_lock():
    if os.name != "nt":
        raise RuntimeError("Native replay requires Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.ReleaseMutex.argtypes = kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    mutex = kernel.CreateMutexW(None, True, "Local\\CSXNRPackedReplayCampaign")
    error = ctypes.get_last_error()
    if not mutex:
        raise OSError(error, "Cannot acquire native campaign ownership")
    owned = error != 183
    try:
        if not owned:
            raise RuntimeError("Another packed replay campaign owns the GPU measurement lane")
        yield
    finally:
        if owned:
            kernel.ReleaseMutex(mutex)
        kernel.CloseHandle(mutex)


def run_campaign(args):
    with native_campaign_lock():
        execute_campaign(args)


def execute_campaign(args):
    from packed_report import summarize

    path = args.campaign.resolve(strict=True)
    campaign, root = load_campaign(path), path.parent
    if not 1 <= args.samples <= 64 or not 1 <= args.warmup <= 32 or not 1 <= args.seconds <= 600:
        raise ValueError("samples1..64, warmup1..32, seconds1..600")
    runtime = args.runtime.resolve(strict=True)
    if digest(runtime) != read(campaign["sourceManifest"]["path"])["runtime"]["sha256"].lower():
        raise ValueError("Runtime identity differs from captured provider")
    executable = campaign["replayExecutable"]["path"]
    if args.command == "capture":
        selected = [case for case in campaign["cases"] if case["id"] == args.case]
        if len(selected) != 1:
            raise ValueError("Capture needs an exact campaign case ID")
        jobs = [(selected[0], 0, root / "traces" / args.case)]
    else:
        # Reverse every other repeat to expose monotonic clock/thermal drift.
        jobs = [(case, repeat, root / case["resultDirectory"] / f"repeat-{repeat}")
                for repeat in range(campaign["repeats"])
                for case in (campaign["cases"] if repeat % 2 == 0 else campaign["cases"][::-1])]
    receipt = root / (f"capture-{args.case}.json" if args.command == "capture" else "run.json")
    if receipt.exists() or any(output.exists() for _, _, output in jobs):
        raise ValueError("Existing run preserved; prepare a new campaign for another run")
    journal = {"schema": "csx-nr-packed-run-v1", "campaignSha256": digest(path),
               "runtimeSha256": digest(runtime), "status": "running", "jobs": []}
    write(receipt, journal)
    try:
        for case, repeat, output in jobs:
            require_idle_game()
            arguments = [executable, "--manifest", case["manifest"], "--output", output,
                         "--runtime", runtime, "--rects", json.dumps(case["rects"]),
                         "--samples", args.samples, "--warmup", args.warmup, "--seconds", args.seconds]
            if args.command == "capture":
                arguments += ["--renderdoc", args.renderdoc.resolve(strict=True)]
            journal["jobs"].append({"case": case["id"], "repeat": repeat, "output": str(output), "status": "running"})
            write(receipt, journal)
            print(f"{case['id']} repeat {repeat}", flush=True)
            invoke(arguments, output, args.seconds)
            native = read(output / "results.json")
            if native.get("status") != "complete" or native.get("sessionClosed") is not True or not native.get("cases") \
                    or any(case.get("status") != "complete" or not case.get("samples")
                           or any(sample.get("success") is not True for sample in case["samples"]) for case in native["cases"]):
                raise RuntimeError(f"Native evidence failed admission: {output}")
            journal["jobs"][-1]["status"] = "complete"
        journal["status"] = "complete"
    except Exception as error:
        journal.update(status="failed", reason=str(error))
        if journal["jobs"] and journal["jobs"][-1]["status"] == "running":
            journal["jobs"][-1].update(status="failed", reason=str(error))
        raise
    finally:
        write(receipt, journal)
        if args.command == "run":
            write(root / "summary.json", summarize(campaign, root))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("prepare")
    create.add_argument("--manifest", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--replay", type=Path, required=True)
    create.add_argument("--roi", type=rectangle, action="append", required=True)
    create.add_argument("--halos", default="0,64,128")
    create.add_argument("--canvas", choices=("strip", "horizontal", "vertical", "diagonal"), default="strip")
    create.add_argument("--repeats", type=int, default=3)
    for command in ("run", "capture", "report"):
        sub = commands.add_parser(command)
        sub.add_argument("--campaign", type=Path, required=True)
        if command != "report":
            sub.add_argument("--runtime", type=Path, required=True)
            sub.add_argument("--samples", type=int, default=8 if command == "run" else 1)
            sub.add_argument("--warmup", type=int, default=3)
            sub.add_argument("--seconds", type=int, default=120)
        if command == "capture":
            sub.add_argument("--case", required=True)
            sub.add_argument("--renderdoc", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare_campaign(args)
    elif args.command == "report":
        from packed_report import summarize

        root = args.campaign.resolve(strict=True).parent
        write(root / "summary.json", summarize(read(args.campaign), root))
    else:
        run_campaign(args)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Packed replay stopped: {error}", file=sys.stderr)
        sys.exit(1)
