"""Diagnose C translation invariance with fixed backing; never qualify production."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

from packed_input import (BUNDLE_BUDGET, ROLES, Texture, _finite_texture, _read,
                          crop_bytes, derived_manifest, digest, load_texture, require)
from packed_replay import (digest as file_digest, invoke, native_campaign_lock,
                           read, rectangle, require_idle_game, write)
from packed_report import _difference, _manifest, _repeat, statistics_summary


SOURCES = ("translation_probe.py", "packed_input.py", "packed_replay.py", "packed_report.py",
           "../nr-color/replay_report.py", "../nr-color/transaction_evidence.py")


def roll(texture: Texture, dx: int) -> Texture:
    """Translate whole rows cyclically without filtering or changing pixel bytes."""
    require(type(dx) is int and 0 <= dx < texture.width, "shift exceeds backing")
    cut = texture.row_bytes - dx * (texture.row_bytes // texture.width)
    data = b"".join(row[cut:] + row[:cut] for row in
                    (texture.data[y * texture.row_bytes:(y + 1) * texture.row_bytes]
                     for y in range(texture.height)))
    return Texture(texture.width, texture.height, texture.format, texture.row_bytes, data)


def prepare(args):
    """Create new first-frame fixtures and retain exact original provenance."""
    shifts = [int(value) for value in args.shifts.split(",")]
    require(2 <= len(shifts) <= 9 and shifts[0] == 0 and len(set(shifts)) == len(shifts)
            and all(0 <= dx <= 256 for dx in shifts), "shifts need distinct 0-first values in 0..256")
    require(type(args.repeats) is int and 1 <= args.repeats <= 5, "repeats must be 1..5")
    require(type(args.samples) is int and 1 <= args.samples <= 64
            and type(args.warmup) is int and 1 <= args.warmup <= 32
            and type(args.seconds) is int and 1 <= args.seconds <= 600, "sample/warmup/deadline bounds")
    source = args.manifest.resolve(strict=True)
    original_bytes = _read(source, 8 * 1024 * 1024)
    manifest, _ = _manifest(source)
    require(file_digest(source) == digest(original_bytes), "source manifest changed during admission")
    frame = manifest["frames"][0]
    require(frame["tuning"].get("useAutoMask") is True and frame["tuning"].get("uiCorrection") is False,
            "translation probe requires automatic mask and no UI correction")
    executable, runtime = args.replay.resolve(strict=True), args.runtime.resolve(strict=True)
    require(file_digest(runtime) == manifest["runtime"]["sha256"].lower(), "runtime differs from capture")
    textures, payload = [], 0
    x, y, width, height = args.roi
    require(all(type(v) is int and 0 <= v <= 16384 for v in args.roi)
            and width >= 64 and height >= 64, "ROI must be bounded and at least 64 pixels per side")
    for eye in frame["eyes"]:
        scale = eye["motionVectorScale"]
        require(isinstance(scale, list) and len(scale) == 2
                and all(type(v) in (int, float) and math.isfinite(v) and v > 0 for v in scale), "invalid motion scale")
        resources = {role: load_texture(eye[role], source.parent) for role in ROLES}
        color = resources["color"]
        require(eye["outputSubrect"] == dict(baseX=0, baseY=0, width=color.width, height=color.height),
                "source must retain its full initialized domain")
        require(x + max(shifts) + width <= color.width and y + height <= color.height, "shifted ROI exceeds backing")
        if textures:
            require((color.width, color.height) == (textures[0]["color"].width, textures[0]["color"].height),
                    "stereo backing differs")
        for texture in resources.values():
            _finite_texture(texture)
            payload += len(texture.data)
        textures.append(resources)
    require(payload <= BUNDLE_BUDGET, "derived bundle exceeds budget")
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / "source-manifest.json").write_bytes(original_bytes)
    campaign = {"schema": "csx-nr-translation-probe-v1", "status": "preparing", "cases": [],
                "sourceManifest": {"path": str(source), "sha256": digest(original_bytes)},
                "sourceFrameIndex": 0, "sourceFrameCount": len(manifest["frames"]),
                "sourceWorldFrame": frame["sourceWorldFrame"], "ownedSourceRect": args.roi,
                "sourceResourceSha256": [{r: digest(t[r].data) for r in ROLES} for t in textures],
                "replayExecutable": {"path": str(executable), "sha256": file_digest(executable)},
                "runtime": {"path": str(runtime), "sha256": file_digest(runtime)},
                "toolSources": {name: file_digest(Path(__file__).parent / name) for name in SOURCES},
                "repeats": args.repeats, "samples": args.samples, "warmup": args.warmup,
                "seconds": args.seconds, "productionQualified": False,
                "scope": "frozen_C_translation_phase_diagnosis_not_visual_fix",
                "translation": "whole_grid_cyclic_horizontal_roll_no_resampling",
                "limitation": "cyclic wrap changes distant boundary adjacency; global spatial conditioning is not preserved"}
    try:
        for dx in shifts:
            directory = root / "inputs" / f"dx-{dx}"
            directory.mkdir(parents=True)
            derived = derived_manifest(manifest, payload, "offline_translation_fixture",
                                       "offline_stateless_translation_not_captured_game_execution")
            derived["sourceCapture"] = {"manifestFile": "../../source-manifest.json",
                                        "manifestSha256": digest(original_bytes),
                                        "metadataAppliesTo": "original_capture_only_not_translated_fixture"}
            derived["translationProbe"] = {"dx": dx, "ownedSourceRect": args.roi,
                                            "ownedEvaluationRect": [x + dx, y, width, height]}
            for index, resources in enumerate(textures):
                eye = derived["frames"][0]["eyes"][index]
                for role, original in resources.items():
                    moved = roll(original, dx)
                    require(crop_bytes(original, args.roi) == crop_bytes(moved, [x + dx, y, width, height]),
                            "translation changed owned input bytes")
                    filename = f"eye-{index}-{role}.bin"
                    (directory / filename).write_bytes(moved.data)
                    eye[role] = dict(file=filename, width=moved.width, height=moved.height,
                                     format=moved.format, rowBytes=moved.row_bytes, sha256=digest(moved.data))
                eye["outputSubrect"] = frame["eyes"][index]["outputSubrect"].copy()
            path = directory / "manifest.json"
            write(path, derived)
            campaign["cases"].append({"id": f"dx-{dx}", "kind": "separate", "dx": dx,
                                      "manifest": str(path), "manifestSha256": file_digest(path),
                                      "rects": [[x + dx, y, width, height]]})
        campaign["status"] = "prepared"
    except Exception as error:
        campaign.update(status="failed_preparation", reason=str(error))
        raise
    finally:
        write(root / "campaign.json", campaign)
    return campaign


def execute(campaign, root):
    """Run serial isolated processes; admit every result before launching another."""
    _admit_campaign(campaign, root)
    require(not (root / "run.json").exists(), "existing run is preserved")
    journal = {"status": "running", "jobs": [], "productionQualified": False,
               "campaignSha256": file_digest(root / "campaign.json")}
    summary = {"schema": "csx-nr-translation-report-v1", "status": "running", "cases": [],
               "productionQualified": False, "baselineRepeatable": None, "strictRgbEquivalent": False,
               "scope": campaign["scope"], "limitation": campaign["limitation"]}
    baseline, equivalent = {}, True
    try:
        for repeat in range(campaign["repeats"]):
            for case in campaign["cases"] if repeat % 2 == 0 else campaign["cases"][::-1]:
                require_idle_game()
                for name, expected in campaign["toolSources"].items():
                    require(file_digest(Path(__file__).parent / name) == expected, "tool source changed")
                for identity in (campaign["replayExecutable"], campaign["runtime"], campaign["sourceManifest"]):
                    require(file_digest(identity["path"]) == identity["sha256"], "campaign identity changed")
                path = Path(case["manifest"])
                require(file_digest(path) == case["manifestSha256"], "fixture manifest changed")
                manifest, content_hash = _manifest(path)
                output = root / "runs" / case["id"] / f"repeat-{repeat}"
                job = {"case": case["id"], "repeat": repeat, "output": str(output), "status": "running"}
                journal["jobs"].append(job)
                write(root / "run.json", journal)
                print(f"{case['id']} repeat {repeat}", flush=True)
                invoke([campaign["replayExecutable"]["path"], "--manifest", path, "--output", output,
                        "--runtime", campaign["runtime"]["path"], "--rects", json.dumps(case["rects"]),
                        "--samples", campaign["samples"], "--warmup", campaign["warmup"],
                        "--seconds", campaign["seconds"]], output, campaign["seconds"])
                result = _repeat(case, output, manifest, path, content_hash,
                                 campaign["replayExecutable"]["sha256"], [tuple(case["rects"][0])])
                require(len(result["steady"]) == campaign["samples"]
                        and result["checked"]["warmupSamples"] == campaign["warmup"],
                        "requested sample/warmup count differs")
                comparisons = []
                for sample in result["steady"]:
                    for key, crop in sample["crops"].items():
                        if not baseline.get(key):
                            require(case["dx"] == 0, "baseline must run first")
                            baseline[key] = crop
                        difference = _difference(baseline[key], crop)
                        equivalent &= difference["rgbExact"]
                        if case["dx"] == 0:
                            summary["baselineRepeatable"] = (summary["baselineRepeatable"] is not False
                                                             and difference["rgbExact"])
                        comparisons.append(dict(eye=key[0], region=0, iteration=sample["iteration"],
                                                ownedSourceRect=campaign["ownedSourceRect"], **difference))
                summary["cases"].append({"id": case["id"], "repeat": repeat,
                                         "resultsSha256": result["resultSha256"], "comparisons": comparisons,
                                         "nativeGpuMicroseconds": statistics_summary(
                                             [s["nativeGpuMicroseconds"] for s in result["steady"]])})
                job["status"] = "complete"
        journal["status"] = summary["status"] = "complete"
        summary["strictRgbEquivalent"] = equivalent and summary["baselineRepeatable"]
    except Exception as error:
        journal.update(status="failed", reason=str(error))
        if journal["jobs"] and journal["jobs"][-1]["status"] == "running":
            journal["jobs"][-1].update(status="failed", reason=str(error))
        summary.update(status="failed", reason=str(error), strictRgbEquivalent=False)
        raise
    finally:
        write(root / "run.json", journal)
        write(root / "summary.json", summary)
    return summary


def _admit_campaign(campaign, root):
    """Recheck persisted bounds and generated identities before native execution."""
    require(campaign.get("schema") == "csx-nr-translation-probe-v1"
            and campaign.get("status") == "prepared", "campaign schema/status differs")
    require(campaign.get("productionQualified") is False
            and campaign.get("sourceFrameIndex") == 0, "campaign scope differs")
    for name, maximum in (("repeats", 5), ("samples", 64), ("warmup", 32), ("seconds", 600)):
        require(type(campaign.get(name)) is int and 1 <= campaign[name] <= maximum,
                "invalid campaign bound: " + name)
    require(isinstance(campaign.get("toolSources"), dict)
            and set(campaign["toolSources"]) == set(SOURCES), "tool source set differs")
    owned = campaign.get("ownedSourceRect")
    require(isinstance(owned, list) and len(owned) == 4
            and all(type(v) is int and 0 <= v <= 16384 for v in owned)
            and owned[2] >= 64 and owned[3] >= 64, "invalid campaign ownership")
    cases = campaign.get("cases")
    require(isinstance(cases, list) and 2 <= len(cases) <= 9, "invalid campaign case count")
    shifts = [case.get("dx") for case in cases]
    require(all(type(dx) is int and 0 <= dx <= 256 for dx in shifts)
            and shifts[0] == 0 and len(set(shifts)) == len(shifts), "invalid campaign shifts")
    for case in cases:
        dx = case["dx"]
        require(case.get("id") == f"dx-{dx}" and case.get("kind") == "separate"
                and case.get("rects") == [[owned[0] + dx, *owned[1:]]], "case identity/rectangle differs")
        path = (root / "inputs" / f"dx-{dx}" / "manifest.json").resolve(strict=True)
        require(Path(case["manifest"]).resolve(strict=True) == path, "case manifest location differs")
        require(file_digest(path) == case["manifestSha256"], "fixture manifest changed")
        manifest, _ = _manifest(path)
        require(manifest.get("translationProbe") == dict(dx=dx, ownedSourceRect=owned,
                                                        ownedEvaluationRect=case["rects"][0]),
                "fixture translation contract differs")
        for eye in manifest["frames"][0]["eyes"]:
            width, height = eye["color"]["width"], eye["color"]["height"]
            require(owned[0] + dx + owned[2] <= width and owned[1] + owned[3] <= height,
                    "shifted ROI exceeds backing")
    require(read(root / "campaign.json") == campaign, "persisted campaign differs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "replay", "runtime", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--roi", type=rectangle, default=[192, 512, 128, 128])
    parser.add_argument("--shifts", default="0,1,16,32,64")
    for name, default in (("samples", 4), ("warmup", 3), ("repeats", 2), ("seconds", 120)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.add_argument("--prepare-only", action="store_true", help="CPU-only fixture preparation")
    args = parser.parse_args()
    campaign = prepare(args)
    if not args.prepare_only:
        with native_campaign_lock():
            execute(campaign, args.output.resolve())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"Translation probe stopped: {error}", file=sys.stderr)
        sys.exit(1)
