"""Audit frozen C packed-region replays; native timings never qualify production."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import sys

from packed_input import BUNDLE_BUDGET, Texture, crop_bytes, load_texture

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "nr-color"))
from replay_report import checked_case, finite_tree, sha256, statistics_summary
from transaction_evidence import require, uint


def _hash(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _path(root: Path, value: str, contained: bool = False) -> Path:
    require(isinstance(value, str) and bool(value), "missing evidence path")
    result = (root / value).resolve()
    require(not contained or result.is_relative_to(root.resolve()), "evidence path escapes its directory")
    return result


def _json(path: Path) -> dict:
    maximum = 32 * 1024 * 1024
    require(path.stat().st_size <= maximum, "JSON evidence exceeds bounded report budget")
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum, "JSON evidence exceeds bounded report budget")
    value = json.loads(data.decode("utf-8-sig"))
    require(isinstance(value, dict), "JSON evidence must be an object")
    finite_tree(value)
    return value


def _rect(value: object) -> tuple[int, int, int, int]:
    if isinstance(value, dict):
        value = [value.get("baseX", value.get("x")), value.get("baseY", value.get("y")),
                 value.get("width"), value.get("height")]
    require(isinstance(value, (list, tuple)) and len(value) == 4
            and all(uint(v, 16384) for v in value) and value[2] > 0 and value[3] > 0,
            "invalid rectangle")
    return tuple(value)


def _contains(outer: tuple, inner: tuple) -> bool:
    x, y, w, h = outer
    a, b, c, d = inner
    return x <= a and y <= b and a + c <= x + w and b + d <= y + h


def _crop(data: bytes, width: int, height: int, rect: tuple) -> bytes:
    require(_contains((0, 0, width, height), rect), "owned crop exceeds retained output")
    return crop_bytes(Texture(width, height, 28, width * 4, data), rect)


def _texture(root: Path, record: dict, expected_format: int, maximum: int = BUNDLE_BUDGET) -> bytes:
    require(record.get("format") == expected_format, "unsupported or mismatched texture format")
    width, height = record.get("width"), record.get("height")
    require(uint(width, 16384) and width > 0 and uint(height, 16384) and height > 0,
            "invalid packed texture dimensions")
    return load_texture(record, root, min(maximum, width * height * 4)).data


def _manifest(path: Path) -> tuple[dict, str]:
    manifest = _json(path)
    require(manifest.get("schema") == "csx-nr-replay-input-v1" and manifest.get("complete") is True
            and manifest.get("state") == "complete", "incomplete replay input manifest")
    frames = manifest.get("frames")
    require(isinstance(frames, list) and 1 <= len(frames) <= 32, "input frames missing")
    hashes = []
    remaining = BUNDLE_BUDGET
    for frame in frames:
        require(frame.get("mode") == 2, "packed prototype requires stateless C")
        eyes = frame.get("eyes")
        require(isinstance(eyes, list) and 1 <= len(eyes) <= 2, "input eyes missing")
        for eye in eyes:
            require(eye.get("featureUpscaling") is False, "packed prototype rejects upscaling guides")
            for role, format_ in (("color", 28), ("depth", 41), ("motion", 34), ("output", 28)):
                remaining -= len(_texture(path.parent, eye[role], format_, remaining))
                require((eye[role]["width"], eye[role]["height"]) ==
                        (eye["color"]["width"], eye["color"]["height"]), "packed prototype requires equal guide grids")
                hashes.append(eye[role]["sha256"].lower())
    return manifest, hashlib.sha256("".join(hashes).encode("ascii")).hexdigest()


def _tiles(case: dict, owned: list[tuple]) -> list[dict]:
    tiles = case.get("tiles")
    require(isinstance(tiles, list) and len(tiles) == len(owned), "atlas tiles missing")
    by_source = {}
    for tile in tiles:
        rect = _rect(tile.get("sourceRect"))
        require(rect in owned and rect not in by_source, "atlas source mapping differs from ownership")
        require(tile.get("sourceIndex", owned.index(rect)) == owned.index(rect), "atlas source index differs")
        by_source[rect] = tile
    return [by_source[r] for r in owned]


def _input_mapping(source_path: Path, source: dict, case_path: Path, captured: dict,
                   case: dict, owned: list[tuple]) -> None:
    first, prepared = source["frames"][0], captured["frames"][0]
    require(len(first["eyes"]) == len(prepared["eyes"]), "prepared eye count differs")
    for key in ("mode", "tuning", "colorConfiguration", "sourceWorldFrame"):
        require(first.get(key) == prepared.get(key), "prepared source contract differs: " + key)
    tiles = _tiles(case, owned) if case["kind"] == "atlas" else []
    for original, packed in zip(first["eyes"], prepared["eyes"]):
        require(original.get("motionVectorScale") == packed.get("motionVectorScale"),
                "packed motion scale changed")
        for role, format_ in (("color", 28), ("depth", 41), ("motion", 34)):
            a, b = original[role], packed[role]
            aa, bb = _texture(source_path.parent, a, format_), _texture(case_path.parent, b, format_)
            if case["kind"] != "atlas":
                require(a["width"] == b["width"] and a["height"] == b["height"] and aa == bb,
                        "reference input differs from immutable source")
            else:
                for rect, tile in zip(owned, tiles):
                    mapped = _rect(tile.get("atlasOwnedRect"))
                    require(mapped[2:] == rect[2:], "atlas changed owned pixel density")
                    require(_crop(aa, a["width"], a["height"], rect) ==
                            _crop(bb, b["width"], b["height"], mapped), "atlas owned input pixels changed")


def _capture_off(value: object) -> bool:
    if value == "not_requested_no_external_capture_tool_attached":
        return True
    return (isinstance(value, dict) and value.get("requested") is False
            and value.get("timingsInstrumented") is False and value.get("state") == "not_requested")


def _handle_sample(sample, slots, mask, barriers, manifest):
    """Bind resource evaluations to the admitted stateless native-handle plan."""
    calls = sample.get("runtimeCalls")
    require(isinstance(calls, list) and len(calls) == len(slots)
            and [c.get("nativeHandleSlot") for c in calls] == slots
            and sample.get("residentNativeHandleMask") == mask
            and sample.get("nativeHandleReuseBarriers") == barriers
            and sample.get("reset") is True and sample.get("success") is True,
            "native handle routing, residency or barriers differ")
    first = sample.get("iteration") == 0
    require(sample.get("createdFeatureCount") == (len(set(slots)) if first else 0),
            "native handle creation count differs")
    seen = set()
    for call, slot in zip(calls, slots):
        created = first and slot not in seen
        require(call.get("createAttempted") is created and call.get("createSucceeded") is created,
                "native handle creation calls differ")
        seen.add(slot)
    expected = [(eye, role, entry[role]["sha256"].lower())
                for eye, entry in enumerate(manifest["frames"][0]["eyes"])
                for role in ("color", "depth", "motion")]
    proofs = sample.get("immutableInputChecks")
    require(isinstance(proofs, list) and len(proofs) == len(expected)
            and [(p.get("eye"), p.get("resource"), p.get("sha256")) for p in proofs] == expected,
            "native handle immutable input proof differs")


def _repeat(case: dict, directory: Path, manifest: dict, manifest_path: Path,
            content_hash: str, executable_hash: str, owned: list[tuple]) -> dict:
    raw = _json(directory / "results.json")
    alternate_sentinel = raw.get("alternateOutputSentinel", False)
    require(type(alternate_sentinel) is bool, "invalid native output sentinel selection")
    require(raw.get("schema") == "csx-nr-replay-results-v1" and raw.get("status") == "complete"
            and raw.get("sessionClosed") is True, "replay failed, incomplete, or session not closed")
    require(_capture_off(raw.get("gpuCapture")), "capture instrumentation present or unavailable")
    require(raw.get("captureKernelModules", False) is False,
            "kernel module capture instrumentation cannot qualify timing")
    require(raw.get("kernelPairMode", "") == "",
            "kernel pair qualification cannot qualify production timing")
    require(raw.get("modelReplacementRequested", False) is False,
            "model replacement qualification cannot qualify production timing")
    chain = raw.get("kernelChainExperiment", {})
    require(isinstance(chain, dict) and "identityCapture" not in chain,
            "kernel module capture instrumentation cannot qualify timing")
    require(raw.get("buildIdentity", {}).get("executableSha256", "").lower() == executable_hash,
            "replay executable identity mismatch")
    require(raw.get("captureManifestSha256", "").lower() == _hash(manifest_path)
            and raw.get("sourceContentSha256") == content_hash, "replay input identity mismatch")
    expected_runtime = manifest.get("runtime", {}).get("sha256")
    require(sha256(expected_runtime) and raw.get("runtime", {}).get("sha256", "").lower() == expected_runtime.lower(),
            "native runtime identity mismatch")
    require(isinstance(raw.get("cases"), list) and len(raw["cases"]) == 1, "expected exactly one custom replay case")
    value = raw["cases"][0]
    require(not any("kernelCommandCapture" in sample or "kernelCommandDetails" in sample
                    for sample in value.get("samples", []) if isinstance(sample, dict)),
            "kernel module capture instrumentation cannot qualify timing")
    batch_only = case.get("batchTimingOnly", False)
    require(type(batch_only) is bool and raw.get("batchTimingOnly", False) is batch_only
            and value.get("batchTimingOnly", False) is batch_only, "batch-only timing differs from planned experiment")
    checked = checked_case(value, allow_batch_timing_only=batch_only)
    require(value.get("mode") == 2 and value.get("history") == "static_reset"
            and value.get("temporalSequence") is False, "replay is not frozen stateless C")
    require(value.get("sourceContentSha256") == content_hash, "case input identity mismatch")
    for key in ("tuning", "colorConfiguration"):
        require(value.get(key) == manifest["frames"][0].get(key), "executed source contract differs: " + key)
    require(value.get("sourceFrameIndices") and set(value["sourceFrameIndices"]) == {0},
            "replay did not hold frame zero constant")
    eyes = len(manifest["frames"][0]["eyes"])
    rects = [_rect(r) for r in case["rects"]]
    require([_rect(r) for r in value["evaluatedRects"]] == rects * eyes
            and value.get("logicalEyeCount") == eyes, "executed rectangles/eyes differ from campaign")
    require(checked["status"] == "measured" and len(checked["acceptedIterations"]) == value["requestedSamples"],
            "steady samples incomplete: " + json.dumps(checked["excludedSamples"]))
    require(value["warmupIterations"] > 0, "steady evaluation needs a warmup")
    expected_slots = [region * 4 + eye for eye in range(eyes) for region in range(len(rects))]
    handle_policy = case.get("nativeHandlePolicy", "independent")
    require(handle_policy in {"independent", "per-eye"}
            and value.get("nativeHandlePolicy", "independent") == handle_policy,
            "native handle policy differs from planned experiment")
    handle_experiment = "nativeHandlePolicy" in case
    require(value.get("axis") != "native_handle_reuse" or handle_experiment,
            "native handle experiment not declared by the plan")
    handle_slots = [eye for eye in range(eyes) for _ in rects] if handle_policy == "per-eye" else expected_slots
    handle_mask = sum(1 << slot for slot in set(handle_slots))
    barriers = eyes * (len(rects) - 1) if handle_policy == "per-eye" else 0
    if handle_experiment:
        require(value.get("axis") == "native_handle_reuse" and value.get("sharedInputs") is True
                and value.get("nativeHandleSlots") == handle_slots and value.get("nativeHandleMask") == handle_mask
                and value.get("nativeHandleCount") == len(set(handle_slots))
                and value.get("nativeHandleReuseBarriers") == barriers
                and value.get("nativeHandleBarrierPolicy") == "global_uav_between_reused_handle_evaluations",
                "native handle execution plan differs")
        require(2 <= len(rects) <= 4 and all(min(r[2:]) >= 128 for r in rects),
                "native handle experiment exceeds admitted geometry")
        source_eye = manifest["frames"][0]["eyes"][0]
        expected_extents = {role: [source_eye[role]["width"], source_eye[role]["height"]]
                            for role in ("color", "depth", "motion", "output")}
        require(value.get("creationExtent") == expected_extents["output"]
                and value.get("resourceExtents") == expected_extents
                and all([eye[role]["width"], eye[role]["height"]] == expected_extents[role]
                        for eye in manifest["frames"][0]["eyes"] for role in expected_extents)
                and [_rect(r) for r in value.get("evaluatedSourceRects", [])] == rects * eyes
                and [_rect(r) for r in value.get("evaluatedGuideRects", [])] == rects * eyes,
                "native handle creation capacity, source or guide domain differs")
    accepted = set(checked["acceptedIterations"])
    tiles = _tiles(case, owned) if case["kind"] == "atlas" else []
    steady = []
    for sample in value["samples"]:
        if handle_experiment:
            _handle_sample(sample, handle_slots, handle_mask, barriers, manifest)
        if handle_experiment or sample["iteration"] in accepted:
            footprints = sample.get("providerFootprint")
            require(isinstance(footprints, list) and len(footprints) == len(expected_slots)
                    and all(p.get("modifiedOutsidePixels") == 0 and p.get("unchangedInsidePixels") == 0
                            and p.get("nonfiniteInsidePixels") == 0 for p in footprints),
                    "provider output footprint is incomplete, nonfinite or wrote outside evaluation")
            require(all(p.get("alternateBytePattern", False) is alternate_sentinel for p in footprints),
                    "native output sentinel footprint differs from result")
        if sample["iteration"] not in accepted and not handle_experiment:
            continue
        calls = sample.get("runtimeCalls")
        require((sample.get("createdFeatureCount") == 0 or handle_experiment and sample["warmup"])
                and isinstance(calls, list)
                and [c.get("slot") for c in calls] == expected_slots
                and all((c.get("createAttempted") is False or handle_experiment and sample["warmup"])
                        and c.get("evaluationAttempted") is True
                        and c.get("evaluationSucceeded") is True for c in calls), "steady native creation or calls differ")
        require(sample.get("evaluatedPixels") == checked["evaluatedPixelsPerSample"], "sample evaluated pixel count differs")
        require(sample["gpuMicroseconds"] > 0 and (batch_only or all(v > 0 for v in sample["evaluationGpuMicroseconds"])),
                "nonpositive GPU sample")
        require(sample["nonzeroEditPixels"] > 0 and sample["maximumAbsEdit"] > 0, "nonzero native edit missing")
        outputs = sample.get("outputFiles")
        require(isinstance(outputs, list) and len(outputs) == len(expected_slots)
                and sorted(o.get("slot") for o in outputs) == sorted(expected_slots), "output slots missing or duplicated")
        output_by_slot = {o["slot"]: o for o in outputs}
        cropped = {}
        for eye in range(eyes):
            for region, source_rect in enumerate(owned):
                index = region if case["kind"] == "separate" else 0
                slot = index * 4 + eye
                output = output_by_slot[slot]
                evaluated = rects[index]
                require(output.get("scope") == "evaluated_rectangle"
                        and (output.get("width"), output.get("height")) == evaluated[2:], "output crop contract differs")
                data = _texture(directory, output, 28)
                mapped = _rect(tiles[region]["atlasOwnedRect"]) if case["kind"] == "atlas" else source_rect
                require(_contains(evaluated, mapped), "evaluation does not cover owned output")
                cropped[(eye, region)] = _crop(data, output["width"], output["height"],
                                               (mapped[0] - evaluated[0], mapped[1] - evaluated[1], *mapped[2:]))
        if sample["iteration"] in accepted:
            steady.append({"iteration": sample["iteration"], "crops": cropped,
                           "nativeGpuMicroseconds": None if batch_only else sum(sample["evaluationGpuMicroseconds"]),
                           "perEvaluationGpuMicroseconds": sample["evaluationGpuMicroseconds"],
                           "batchGpuMicroseconds": sample["gpuMicroseconds"]})
    return {"steady": steady, "checked": checked, "runtime": raw["runtime"],
            "alternateOutputSentinel": alternate_sentinel,
            "buildIdentity": raw["buildIdentity"], "resultSha256": _hash(directory / "results.json")}


def _difference(left: bytes, right: bytes) -> dict:
    require(len(left) == len(right) and len(left) > 0 and len(left) % 4 == 0, "owned comparison byte count differs")
    histogram = [0] * 256
    changed, alpha = 0, 0
    for i in range(0, len(left), 4):
        errors = [abs(left[i + c] - right[i + c]) for c in range(3)]
        changed += any(errors)
        alpha += left[i + 3] != right[i + 3]
        for error in errors:
            histogram[error] += 1
    channels = len(left) // 4 * 3
    rank, total, p99 = math.ceil(channels * .99), 0, 0
    for error, count in enumerate(histogram):
        total += count
        if total >= rank:
            p99 = error
            break
    return {"rgbExact": changed == 0, "pixels": len(left) // 4, "changedRgbPixels": changed,
            "changedAlphaPixels": alpha, "maximumAbsoluteRgb": max(i for i, n in enumerate(histogram) if n) / 255,
            "meanAbsoluteRgb": sum(i * n for i, n in enumerate(histogram)) / channels / 255,
            "rmseRgb": math.sqrt(sum(i * i * n for i, n in enumerate(histogram)) / channels) / 255,
            "p99AbsoluteRgbChannel": p99 / 255}


def summarize(campaign: dict, root: Path) -> dict:
    """Verify all evidence and compare every steady owned crop to its repeat's baseline."""
    report = {"schema": "csx-nr-packed-report-v1", "status": "rejected", "errors": [], "cases": [],
              "strictRgbEquivalent": False, "baselineRepeatable": False, "productionQualified": False,
              "tileOrderComparisons": [], "tileOrderIndependent": None,
              "tileOrderReason": "no_matched_forward_reverse_cases",
              "timingScope": "native evaluation GPU intervals only; CPU prepack, upload, readback and scatter excluded",
              "qualityScope": "strict owned RGB byte equality; alpha reported separately; no perceptual, temporal or stereo qualification"}
    try:
        root = Path(root).resolve()
        finite_tree(campaign)
        repeats = campaign.get("repeats")
        require(uint(repeats, 64) and repeats > 0, "invalid repeat count")
        owned = [_rect(r) for r in campaign["rects"]]
        require(1 <= len(owned) <= 8, "invalid owned region count")
        source_path = _path(root, campaign["sourceManifest"]["path"])
        require(_hash(source_path) == campaign["sourceManifest"]["sha256"].lower(), "source manifest hash mismatch")
        source, _ = _manifest(source_path)
        executable = _path(root, campaign["replayExecutable"]["path"])
        exe_hash = _hash(executable)
        require(exe_hash == campaign["replayExecutable"]["sha256"].lower(), "executable file hash mismatch")
        cases = campaign["cases"]
        require(isinstance(cases, list) and cases and len({c["id"] for c in cases}) == len(cases), "missing/duplicate campaign cases")
        baselines = [c for c in cases if c.get("kind") == "separate"]
        require(len(baselines) == 1, "exactly one separate baseline required")
        require(all(c.get("kind") in {"separate", "enclosing", "atlas"} for c in cases), "unknown case kind")
        require([_rect(r) for r in baselines[0]["rects"]] == owned, "baseline must evaluate the owned regions")
        loaded = {}
        for case in cases:
            record = {"id": case["id"], "kind": case["kind"], "errors": [], "repeats": [], "comparisons": []}
            report["cases"].append(record)
            loaded[case["id"]] = []
            try:
                require(case["kind"] == "separate" or len(case["rects"]) == 1, "fused case must use one inference per eye")
                manifest_path = _path(root, case["manifest"])
                if "manifestSha256" in case:
                    require(_hash(manifest_path) == case["manifestSha256"].lower(), "case manifest hash mismatch")
                manifest, content_hash = _manifest(manifest_path)
                _input_mapping(source_path, source, manifest_path, manifest, case, owned)
                for repeat in range(repeats):
                    try:
                        directory = _path(root, case["resultDirectory"], True) / f"repeat-{repeat}"
                        result = _repeat(case, directory, manifest, manifest_path, content_hash, exe_hash, owned)
                        loaded[case["id"]].append(result)
                        record["repeats"].append({"repeat": repeat, "status": "complete", "resultsSha256": result["resultSha256"],
                                                  "nativeGpuMicroseconds": statistics_summary([s["nativeGpuMicroseconds"] for s in result["steady"]])})
                    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
                        message = f"repeat-{repeat}: {error}"
                        record["errors"].append(message)
                        record["repeats"].append({"repeat": repeat, "status": "rejected", "reason": str(error)})
                        loaded[case["id"]].append(None)
            except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
                record["errors"].append(str(error))
            report["errors"].extend(f"{case['id']}: {e}" for e in record["errors"])
        if report["errors"]:
            return report
        baseline = loaded[baselines[0]["id"]]
        first_reference = baseline[0]["steady"][0]["crops"]
        repeatability = []
        for repeat, result in enumerate(baseline):
            for sample in result["steady"]:
                for key, data in sample["crops"].items():
                    repeatability.append({"repeat": repeat, "iteration": sample["iteration"], "eye": key[0], "region": key[1],
                                          **_difference(first_reference[key], data)})
        report["baselineRepeatability"] = repeatability
        report["baselineRepeatable"] = all(c["rgbExact"] for c in repeatability)
        for record in report["cases"]:
            results = loaded[record["id"]]
            steady = [s for result in results for s in result["steady"]]
            record["nativeGpuMicroseconds"] = statistics_summary([s["nativeGpuMicroseconds"] for s in steady])
            count = results[0]["checked"]["evaluationsPerSample"]
            record["perEvaluationGpuMicroseconds"] = [statistics_summary([s["perEvaluationGpuMicroseconds"][i] for s in steady])
                                                       for i in range(count)]
            record["evaluatedPixelsPerSample"] = results[0]["checked"]["evaluatedPixelsPerSample"]
            record["evaluationsPerSample"] = count
            record["ownedPixelsPerSample"] = sum(r[2] * r[3] for r in owned) * len(source["frames"][0]["eyes"])
            for repeat, result in enumerate(results):
                reference = baseline[repeat]["steady"][0]["crops"]
                for sample in result["steady"]:
                    for key, data in sample["crops"].items():
                        record["comparisons"].append({"repeat": repeat, "iteration": sample["iteration"], "eye": key[0], "region": key[1],
                                                       **_difference(reference[key], data)})
            record["strictRgbEquivalent"] = all(c["rgbExact"] for c in record["comparisons"])
        baseline_median = next(c["nativeGpuMicroseconds"]["median"] for c in report["cases"] if c["kind"] == "separate")
        for record in report["cases"]:
            record["nativeMedianDeltaPercent"] = ((record["nativeGpuMicroseconds"]["median"] / baseline_median - 1) * 100
                                                   if baseline_median else None)
        report["strictRgbEquivalent"] = report["baselineRepeatable"] and all(c["strictRgbEquivalent"] for c in report["cases"])
        forward = [c for c in cases if c["kind"] == "atlas" and c.get("reverse") is False]
        for case in forward:
            reverse = [c for c in cases if c["kind"] == "atlas" and c.get("reverse") is True
                       and "halo" in c and c["halo"] == case.get("halo")]
            require(len(reverse) <= 1, "ambiguous reversed atlas pair")
            if not reverse:
                continue
            other = reverse[0]
            pair = {"forward": case["id"], "reversed": other["id"], "halo": case["halo"], "comparisons": []}
            for repeat, result in enumerate(loaded[other["id"]]):
                reference = loaded[case["id"]][repeat]["steady"][0]["crops"]
                for sample in result["steady"]:
                    for key, data in sample["crops"].items():
                        pair["comparisons"].append({"repeat": repeat, "iteration": sample["iteration"],
                                                    "eye": key[0], "region": key[1], **_difference(reference[key], data)})
            pair["strictRgbEquivalent"] = all(c["rgbExact"] for c in pair["comparisons"])
            report["tileOrderComparisons"].append(pair)
        if report["tileOrderComparisons"]:
            report["tileOrderIndependent"] = all(p["strictRgbEquivalent"] for p in report["tileOrderComparisons"])
            report["tileOrderReason"] = "fixed_input_owned_rgb_comparison_only"
        report["status"] = "complete"
    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
        report["errors"].append(str(error))
    return report
