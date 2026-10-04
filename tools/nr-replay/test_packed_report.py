"""Synthetic evidence tests; no native provider, GPU, or performance claims."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import packed_report as report


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def rectangle(rect):
    return dict(zip(("baseX", "baseY", "width", "height"), rect))


class PackedReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nr-packed-report-")
        self.root = Path(self.temp.name)
        self.rects = [[0, 0, 2, 2], [6, 2, 2, 2]]
        self.runtime = "a" * 64
        self.exe = self.root / "replay.exe"
        self.exe.write_bytes(b"synthetic executable identity only")
        self.source = self.make_manifest("source", 8, 4)
        self.atlas = self.make_manifest("atlas", 4, 2, packed=True)
        source_path = self.root / "source/manifest.json"
        self.campaign = {"sourceManifest": {"path": str(source_path), "sha256": report._hash(source_path)},
                         "replayExecutable": {"path": str(self.exe), "sha256": report._hash(self.exe)},
                         "rects": self.rects, "repeats": 2, "cases": []}
        for kind, rects, name in (("separate", self.rects, "source"), ("enclosing", [[0, 0, 8, 4]], "source"),
                                  ("atlas", [[0, 0, 4, 2]], "atlas")):
            case = {"id": kind, "kind": kind, "manifest": str(self.root / name / "manifest.json"),
                    "rects": rects, "resultDirectory": "runs/" + kind}
            case["manifestSha256"] = report._hash(Path(case["manifest"]))
            if kind == "atlas":
                case["tiles"] = [{"sourceIndex": i, "sourceRect": r, "atlasOwnedRect": [i * 2, 0, 2, 2]}
                                 for i, r in enumerate(self.rects)]
            self.campaign["cases"].append(case)
            for repeat in range(2):
                self.make_result(case, repeat)

    def tearDown(self):
        self.temp.cleanup()

    def texture(self, directory, name, data, width, height, format_):
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return {"file": name, "sha256": digest(data), "format": format_, "width": width,
                "height": height, "rowBytes": width * 4, "bytes": len(data)}

    def make_manifest(self, name, width, height, packed=False):
        directory = self.root / name
        eyes = []
        for eye in range(2):
            entry = {"featureUpscaling": False, "motionVectorScale": [8.0, 4.0]}
            for role, format_ in (("color", 28), ("depth", 41), ("motion", 34), ("output", 28)):
                if packed:
                    original = self.source["frames"][0]["eyes"][eye][role]
                    data = (self.root / "source" / original["file"]).read_bytes()
                    parts = [report._crop(data, 8, 4, tuple(r)) for r in self.rects]
                    data = b"".join(parts[0][row * 8:(row + 1) * 8] + parts[1][row * 8:(row + 1) * 8]
                                    for row in range(2))
                elif role in ("color", "output"):
                    data = b"".join(bytes([(x + y * 8 + eye + (role == "output")) % 256, 60, 70, 255])
                                    for y in range(height) for x in range(width))
                else:
                    data = bytes(width * height * 4)
                entry[role] = self.texture(directory, f"eye-{eye}-{role}.bin", data, width, height, format_)
            eyes.append(entry)
        manifest = {"schema": "csx-nr-replay-input-v1", "complete": True, "state": "complete",
                    "runtime": {"sha256": self.runtime}, "frames": [{"mode": 2, "sourceWorldFrame": 100,
                    "tuning": {"useAutoMask": True}, "colorConfiguration": {}, "eyes": eyes}]}
        write_json(directory / "manifest.json", manifest)
        return manifest

    def make_result(self, case, repeat):
        directory = self.root / case["resultDirectory"] / f"repeat-{repeat}"
        manifest, content_hash = report._manifest(Path(case["manifest"]))
        eye0 = manifest["frames"][0]["eyes"][0]
        width, height = eye0["color"]["width"], eye0["color"]["height"]
        rects = case["rects"]
        evaluated = [rectangle(r) for r in rects] * 2
        count = len(evaluated)
        value = {"id": "custom-regions", "axis": "packed_region_experiment", "route": "C", "mode": 2,
                 "history": "static_reset", "temporalSequence": False, "status": "complete", "reason": "",
                 "warmupIterations": 1, "requestedSamples": 2, "sourceFrameIndices": [0, 0, 0],
                 "creationExtent": [width, height], "resourceExtents": {r: [width, height] for r in ("color", "depth", "motion", "output")},
                 "evaluatedRects": evaluated, "evaluatedGuideRects": evaluated, "evaluatedSourceRects": evaluated,
                 "evaluationsPerSample": count, "logicalEyeCount": 2, "featureUpscaling": False, "useAutoMask": True,
                 "controlMaskPassed": False, "characterSelection": False, "transportBypass": False, "applyModelEdit": True,
                 "sourceContentSha256": content_hash, "tuning": {"useAutoMask": True}, "colorConfiguration": {}, "samples": []}
        for iteration in range(3):
            outputs, calls, footprints = [], [], []
            for eye in range(2):
                original = manifest["frames"][0]["eyes"][eye]["output"]
                pixels = (Path(case["manifest"]).parent / original["file"]).read_bytes()
                for region, rect in enumerate(rects):
                    slot = region * 4 + eye
                    data = report._crop(pixels, width, height, tuple(rect))
                    output = self.texture(directory, f"sample-{iteration}-slot-{slot}.bin", data, rect[2], rect[3], 28)
                    output.update(slot=slot, scope="evaluated_rectangle")
                    outputs.append(output)
                    calls.append({"slot": slot, "createAttempted": iteration == 0,
                                  "evaluationAttempted": True, "evaluationSucceeded": True})
                    footprints.append({"modifiedInsidePixels": rect[2] * rect[3], "unchangedInsidePixels": 0,
                                       "modifiedOutsidePixels": 0, "nonfiniteInsidePixels": 0})
            value["samples"].append({"iteration": iteration, "warmup": iteration == 0, "success": True, "reset": True,
                                     "evaluationCount": count, "createdFeatureCount": count if iteration == 0 else 0,
                                     "gpuMicroseconds": count * 11, "evaluationGpuMicroseconds": [10] * count,
                                     "nonzeroEditPixels": 1, "maximumAbsEdit": 1 / 255,
                                     "evaluatedPixels": sum(r[2] * r[3] for r in rects) * 2,
                                     "providerFootprint": footprints, "runtimeCalls": calls, "outputFiles": outputs})
        raw = {"schema": "csx-nr-replay-results-v1", "status": "complete", "sessionClosed": True,
               "gpuCapture": {"requested": False, "timingsInstrumented": False, "state": "not_requested"},
               "buildIdentity": {"executableSha256": report._hash(self.exe)}, "captureManifestSha256": report._hash(Path(case["manifest"])),
               "sourceContentSha256": content_hash, "runtime": {"sha256": self.runtime}, "cases": [value]}
        write_json(directory / "results.json", raw)

    def mutate(self, callback, kind="atlas", repeat=0):
        path = self.root / f"runs/{kind}/repeat-{repeat}/results.json"
        raw = json.loads(path.read_text())
        callback(raw)
        write_json(path, raw)

    def result(self):
        return report.summarize(self.campaign, self.root)

    def assert_rejected(self, fragment):
        result = self.result()
        self.assertEqual(result["status"], "rejected", result)
        self.assertIn(fragment, " ".join(result["errors"]))
        self.assertFalse(result["productionQualified"])

    def test_complete_custom_case_mapping_native_cost_and_padding(self):
        result = self.result()
        self.assertEqual(result["status"], "complete", result)
        self.assertTrue(result["strictRgbEquivalent"])
        self.assertTrue(result["baselineRepeatable"])
        self.assertEqual(result["cases"][0]["nativeGpuMicroseconds"]["median"], 40)
        self.assertEqual(result["cases"][1]["evaluatedPixelsPerSample"], 64)
        self.assertEqual(result["cases"][1]["ownedPixelsPerSample"], 16)
        self.assertEqual(result["cases"][2]["nativeMedianDeltaPercent"], -50)
        self.assertIn("scatter excluded", result["timingScope"])

    def handle_fixture(self, policy):
        manifest = self.make_manifest("handle-source", 384, 256)
        path = self.root / "handle-source/manifest.json"
        case = {"id": "handles", "kind": "separate", "manifest": str(path),
                "rects": [[0, 0, 128, 128], [256, 128, 128, 128]],
                "resultDirectory": "runs/handles", "nativeHandlePolicy": policy}
        self.make_result(case, 0)
        directory = self.root / case["resultDirectory"] / "repeat-0"
        raw = json.loads((directory / "results.json").read_text())
        value = raw["cases"][0]
        slots = [0, 0, 1, 1] if policy == "per-eye" else [0, 4, 1, 5]
        mask = sum(1 << slot for slot in set(slots))
        barriers = 2 if policy == "per-eye" else 0
        value.update(axis="native_handle_reuse", sharedInputs=True, nativeHandlePolicy=policy,
                     nativeHandleSlots=slots, nativeHandleMask=mask, nativeHandleCount=len(set(slots)),
                     nativeHandleReuseBarriers=barriers,
                     nativeHandleBarrierPolicy="global_uav_between_reused_handle_evaluations")
        for sample in value["samples"]:
            first = sample["iteration"] == 0
            sample.update(residentNativeHandleMask=mask, nativeHandleReuseBarriers=barriers,
                          createdFeatureCount=len(set(slots)) if first else 0,
                          immutableInputChecks=[{"eye": eye, "resource": role, "sha256": entry[role]["sha256"]}
                                                for eye, entry in enumerate(manifest["frames"][0]["eyes"])
                                                for role in ("color", "depth", "motion")])
            seen = set()
            for call, slot in zip(sample["runtimeCalls"], slots):
                created = first and slot not in seen
                call.update(nativeHandleSlot=slot, createAttempted=created, createSucceeded=created)
                seen.add(slot)
        write_json(directory / "results.json", raw)

        def admitted():
            _, content = report._manifest(path)
            return report._repeat(case, directory, manifest, path, content,
                                  report._hash(self.exe), [tuple(r) for r in case["rects"]])
        return case, raw, directory, admitted

    def test_native_handle_reuse_keeps_four_evaluations_and_exact_crops(self):
        for policy in ("independent", "per-eye"):
            case, raw, directory, admitted = self.handle_fixture(policy)
            result = admitted()
            self.assertEqual(len(result["steady"]), 2)
            self.assertEqual(len(result["steady"][0]["crops"]), 4)
            self.assertEqual(result["steady"][0]["nativeGpuMicroseconds"], 40)
            self.assertEqual(result["steady"][0]["batchGpuMicroseconds"], 44)

    def test_native_handle_admission_rejects_false_routing_and_mutated_inputs(self):
        case, original, directory, admitted = self.handle_fixture("per-eye")
        mutations = (
            lambda v: v.update(nativeHandlePolicy="independent"),
            lambda v: v.update(nativeHandleCount=4),
            lambda v: v["samples"][1].update(residentNativeHandleMask=51),
            lambda v: v["samples"][1]["runtimeCalls"][1].update(nativeHandleSlot=4),
            lambda v: v["samples"][1].update(nativeHandleReuseBarriers=0),
            lambda v: v["samples"][0].update(createdFeatureCount=4),
            lambda v: v["samples"][0]["runtimeCalls"][1].update(createAttempted=True),
            lambda v: v["samples"][1]["immutableInputChecks"][1].update(sha256="0" * 64),
            lambda v: v["samples"][1]["immutableInputChecks"].pop(),
            lambda v: v["samples"][1].update(reset=False),
            lambda v: v["samples"][0].update(success=False),
            lambda v: v.update(creationExtent=[1024, 1024]),
            lambda v: v["resourceExtents"].update(output=[1024, 1024]),
            lambda v: v["evaluatedSourceRects"][0].update(baseX=1),
            lambda v: v["evaluatedGuideRects"][0].update(baseX=1),
            lambda v: v["samples"][0]["providerFootprint"][0].update(modifiedOutsidePixels=1),
            lambda v: v["samples"][0]["providerFootprint"][0].update(alternateBytePattern=True),
            lambda v: v["samples"][0]["runtimeCalls"][0].update(slot=31),
            lambda v: v["samples"][0]["runtimeCalls"][0].update(evaluationSucceeded=False),
            lambda v: v["samples"][0]["outputFiles"][0].update(sha256="0" * 64),
            lambda v: v["samples"][0].update(evaluatedPixels=1),
            lambda v: v["samples"][0].update(nonzeroEditPixels=0),
        )
        for mutation in mutations:
            raw = copy.deepcopy(original)
            mutation(raw["cases"][0])
            write_json(directory / "results.json", raw)
            with self.assertRaises(ValueError):
                admitted()
        write_json(directory / "results.json", original)
        del case["nativeHandlePolicy"]
        with self.assertRaisesRegex(ValueError, "native handle policy"):
            admitted()

    def test_reverse_tile_order_uses_source_index(self):
        self.campaign["cases"][2]["tiles"].reverse()
        self.assertTrue(self.result()["strictRgbEquivalent"])

    def test_direct_tile_order_comparison_does_not_depend_on_baseline_pass(self):
        atlas = self.campaign["cases"][2]
        atlas.update(halo=0, reverse=False)
        other = copy.deepcopy(atlas)
        other.update(id="atlas-reversed", reverse=True, resultDirectory="runs/atlas-reversed")
        other["tiles"].reverse()
        self.campaign["cases"].append(other)
        for repeat in range(2):
            self.make_result(other, repeat)
        result = self.result()
        self.assertTrue(result["tileOrderIndependent"], result)
        self.alter_output("atlas", 0, 0)
        self.alter_output("atlas-reversed", 0, 1)
        result = self.result()
        self.assertEqual(result["status"], "complete", result)
        self.assertFalse(result["tileOrderIndependent"])
        self.assertFalse(result["strictRgbEquivalent"])
        self.assertEqual(len(result["tileOrderComparisons"]), 1)

    def test_missing_repeat_is_not_silently_dropped(self):
        (self.root / "runs/atlas/repeat-1/results.json").unlink()
        self.assert_rejected("repeat-1")

    def test_hash_mismatch_and_escape_rejected(self):
        self.mutate(lambda r: r["cases"][0]["samples"][1]["outputFiles"][0].update(sha256="f" * 64))
        self.assert_rejected("hash mismatch")
        self.mutate(lambda r: r["cases"][0]["samples"][1]["outputFiles"][0].update(file="../elsewhere.bin"))
        self.assert_rejected("escapes")

    def test_executable_and_input_identity_are_required(self):
        self.mutate(lambda r: r["buildIdentity"].update(executableSha256="b" * 64))
        self.assert_rejected("executable identity")
        self.make_result(self.campaign["cases"][2], 0)
        self.mutate(lambda r: r.update(captureManifestSha256="b" * 64))
        self.assert_rejected("input identity")

    def test_malformed_evidence_types_are_reported_not_raised(self):
        for change in (lambda r: r["buildIdentity"].update(executableSha256=42),
                       lambda r: r.update(runtime=[]),
                       lambda r: r["cases"][0]["samples"].__setitem__(1, [])):
            with self.subTest(change=change):
                self.make_result(self.campaign["cases"][2], 0)
                self.mutate(change)
                result = self.result()
                self.assertEqual(result["status"], "rejected", result)
                self.assertTrue(result["errors"])
                self.assertFalse(result["productionQualified"])
        self.campaign["sourceManifest"]["sha256"] = None
        result = self.result()
        self.assertEqual(result["status"], "rejected", result)
        self.assertTrue(result["errors"])

    def test_capture_unclosed_failed_and_creation_are_rejected(self):
        changes = [(lambda r: r["gpuCapture"].update(requested=True, timingsInstrumented=True), "capture instrumentation"),
                   (lambda r: r.update(sessionClosed=False), "not closed"),
                   (lambda r: r["cases"][0]["samples"][1].update(success=False, reason="injected"), "steady samples incomplete"),
                   (lambda r: r["cases"][0]["samples"][1].update(createdFeatureCount=1), "steady native creation")]
        for change, reason in changes:
            with self.subTest(reason=reason):
                self.make_result(self.campaign["cases"][2], 0)
                self.mutate(change)
                self.assert_rejected(reason)

    def test_sentinel_ambiguous_and_unsupported_format_fail(self):
        def sentinel(raw):
            raw["cases"][0]["samples"][1]["providerFootprint"][0].update(unchangedInsidePixels=1, modifiedInsidePixels=7)
        self.mutate(sentinel)
        self.assert_rejected("ambiguous_output_sentinel")
        self.make_result(self.campaign["cases"][2], 0)
        self.mutate(lambda r: r["cases"][0]["samples"][1]["outputFiles"][0].update(format=10))
        self.assert_rejected("unsupported")

    def alter_output(self, kind, repeat, channel):
        def alter(raw):
            record = raw["cases"][0]["samples"][1]["outputFiles"][0]
            path = self.root / f"runs/{kind}/repeat-{repeat}" / record["file"]
            data = bytearray(path.read_bytes())
            data[channel] ^= 1
            path.write_bytes(data)
            record["sha256"] = digest(data)
        self.mutate(alter, kind, repeat)

    def test_rgb_gate_does_not_turn_alpha_only_change_into_rgb_failure(self):
        self.alter_output("atlas", 0, 3)
        result = self.result()
        self.assertTrue(result["strictRgbEquivalent"], result)
        self.assertEqual(sum(c["changedAlphaPixels"] for c in result["cases"][2]["comparisons"]), 1)

    def test_rgb_difference_is_complete_evidence_but_not_equivalent(self):
        self.alter_output("atlas", 0, 0)
        result = self.result()
        self.assertEqual(result["status"], "complete", result)
        self.assertFalse(result["strictRgbEquivalent"])
        comparison = result["cases"][2]["comparisons"][0]
        self.assertEqual(comparison["changedRgbPixels"], 1)
        self.assertAlmostEqual(comparison["maximumAbsoluteRgb"], 1 / 255)
        self.assertAlmostEqual(comparison["meanAbsoluteRgb"], 1 / 12 / 255)

    def test_baseline_repeatability_checks_all_repeats(self):
        self.alter_output("separate", 1, 0)
        result = self.result()
        self.assertEqual(result["status"], "complete", result)
        self.assertFalse(result["baselineRepeatable"])
        self.assertFalse(result["strictRgbEquivalent"])

    def test_bad_mapping_missing_slots_and_missing_timing_rejected(self):
        self.campaign["cases"][2]["tiles"][0]["atlasOwnedRect"][0] = 3
        self.assert_rejected("exceeds")
        self.campaign["cases"][2]["tiles"][0]["atlasOwnedRect"][0] = 0
        self.mutate(lambda r: r["cases"][0]["samples"][1]["outputFiles"].pop())
        self.assert_rejected("output slots")
        self.make_result(self.campaign["cases"][2], 0)
        self.mutate(lambda r: r["cases"][0]["samples"][1].update(evaluationGpuMicroseconds=[None, 10]))
        self.assert_rejected("steady samples incomplete")


if __name__ == "__main__":
    unittest.main()
