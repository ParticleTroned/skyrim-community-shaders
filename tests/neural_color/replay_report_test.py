"""Synthetic report policy fixtures; none is an NR performance measurement."""
from pathlib import Path
import copy
import json
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/nr-color"))
import replay_report as rr


def fixture():
    samples = [{"iteration": i, "warmup": i == 0, "success": True, "reason": "",
                "reset": True, "createdFeatureCount": 1 if i == 0 else 0,
                "evaluationCount": 1, "gpuMicroseconds": 10 + i,
                "evaluationGpuMicroseconds": [8 + i], "createCpuMicroseconds": 1,
                "evalCpuMicroseconds": 2, "elapsedCpuMicroseconds": 14,
                "nonzeroEditPixels": 2, "maximumAbsEdit": 0.1,
                "providerFootprint": [{"modifiedInsidePixels": 64, "modifiedOutsidePixels": 0,
                                       "unchangedInsidePixels": 0, "nonfiniteInsidePixels": 0}],
                "memory": {"localUsageBefore": 100, "localUsageAfter": 120,
                           "nonlocalUsageBefore": None, "nonlocalUsageAfter": None}} for i in range(3)]
    case = {"id": "full", "axis": "capacity", "route": "A", "mode": 0,
            "history": "static_reset", "pairGroup": "capacity", "creationExtent": [16, 16],
            "resourceExtents": {name: [16, 16] for name in ("color", "depth", "motion", "output")},
            "evaluatedRects": [{"baseX": 0, "baseY": 0, "width": 8, "height": 8}],
            "evaluationsPerSample": 1, "logicalEyeCount": 1, "featureUpscaling": False,
            "useAutoMask": True, "controlMaskPassed": False, "characterSelection": True,
            "warmupIterations": 1, "requestedSamples": 2, "colorConfiguration": {"mode": "legacy_raw"},
            "tuning": {"featureMode": 0}, "sourceFrameIndices": [0], "samples": samples,
            "sourceContentSha256": "a" * 64, "sourceGuideAlignment": "native matched source guide grid",
            "transportBypass": False, "applyModelEdit": True, "logicalResourceBytes": 512,
            "initializationFingerprint": "equal initial state", "contextIds": ["full-0"],
            "status": "complete", "reason": ""}
    case["evaluatedGuideRects"] = copy.deepcopy(case["evaluatedRects"])
    case["evaluatedSourceRects"] = copy.deepcopy(case["evaluatedRects"])
    return {"schema": rr.SCHEMA, "captureManifest": "capture/manifest.json",
            "captureManifestSha256": "c" * 64, "sourceContentSha256": "a" * 64,
            "runtime": {"sha256": "b" * 64}, "adapter": {"description": "fixture; no actual GPU"}, "cases": [case]}


def paired(axis="capacity"):
    result = fixture()
    one = result["cases"][0]
    one["axis"] = axis
    two = copy.deepcopy(one)
    two.update(id="compact", contextIds=["compact-0"])
    if axis == "capacity":
        two["creationExtent"] = [8, 8]
        two["resourceExtents"] = {name: [8, 8] for name in one["resourceExtents"]}
    result["cases"].append(two)
    return result


class ReplayReportTest(unittest.TestCase):
    def test_retains_all_raw_values_and_excludes_warmup(self):
        source = fixture()
        result = rr.report(source)
        self.assertEqual(result["rawResults"], source)
        row = result["cases"][0]
        self.assertEqual(row["gpuMicroseconds"]["n"], 2)
        self.assertEqual(row["gpuMicroseconds"]["mean"], 11.5)
        self.assertEqual(row["excludedSamples"], [{"iteration": 0, "reasons": ["warmup"]}])
        self.assertEqual(row["evaluatedPixelsPerSample"], 64)
        self.assertEqual(row["creationExtent"], [16, 16])
        self.assertEqual(row["memory"]["nonlocalUsageAfter"]["n"], 0)
        self.assertEqual(row["logicalResourceBytes"], 512)
        self.assertEqual(result["observedSuccessfulCallCounts"], [1])
        source["cases"][0]["samples"][0]["reason"] = "mutated"
        self.assertNotEqual(result["rawResults"], source)

    def test_one_sample_has_no_invented_uncertainty(self):
        result = fixture()
        case = result["cases"][0]
        case["requestedSamples"] = 1
        case["samples"].pop()
        stats = rr.report(result)["cases"][0]["gpuMicroseconds"]
        self.assertIsNone(stats["sampleStdev"])
        self.assertIsNone(stats["mean95Interval"])
        self.assertEqual(stats["intervalReason"], "at_least_two_samples_required")

    def test_failure_bypass_cancelled_zero_edit_and_missing_timings_not_fast(self):
        edits = [lambda c, s: s.update(success=False, reason="cancelled"),
                 lambda c, s: c.update(transportBypass=True),
                 lambda c, s: c.update(applyModelEdit=False),
                 lambda c, s: s.update(nonzeroEditPixels=0, maximumAbsEdit=0),
                 lambda c, s: s.update(gpuMicroseconds=None),
                 lambda c, s: s.update(evaluationGpuMicroseconds=[None]),
                 lambda c, s: s.update(evaluationCount=0),
                 lambda c, s: c.update(status="failed", reason="provider failed")]
        for edit in edits:
            with self.subTest(edit=edit):
                source = fixture()
                case = source["cases"][0]
                for sample in case["samples"]:
                    edit(case, sample)
                result = rr.report(source)
                self.assertEqual(result["measuredCaseCount"], 0)
                self.assertIsNone(result["cases"][0]["gpuMicroseconds"]["mean"])
                self.assertEqual(result["rawResults"], source)
                self.assertTrue(result["cases"][0]["excludedSamples"])

    def test_count_mismatch_keeps_raw_but_disqualifies_case(self):
        source = fixture()
        source["cases"][0]["requestedSamples"] += 1
        result = rr.report(source)
        self.assertEqual(result["measuredCaseCount"], 0)
        self.assertIn("observed_sample_counts_differ_from_request", result["cases"][0]["reasons"])

    def test_nan_and_bool_numeric_rejected(self):
        for field, value in (("maximumAbsEdit", float("nan")), ("gpuMicroseconds", float("inf")),
                             ("iteration", True), ("evaluationCount", -1)):
            source = fixture()
            source["cases"][0]["samples"][1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                rr.report(source)

    def test_duplicate_iterations_cases_and_bounds_rejected(self):
        edits = [lambda r: r["cases"][0]["samples"][1].update(iteration=0),
                 lambda r: r["cases"].append(copy.deepcopy(r["cases"][0])),
                 lambda r: r["cases"][0]["evaluatedRects"][0].update(baseX=15),
                 lambda r: r["cases"][0]["resourceExtents"].update(depth=[8, 0]),
                 lambda r: r["cases"][0].update(evaluationsPerSample=2),
                 lambda r: r["cases"][0].update(pairGroup={}),
                 lambda r: r["cases"][0].update(route="C"),
                 lambda r: r["cases"][0].update(status="invented")]
        for edit in edits:
            source = fixture()
            edit(source)
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                rr.report(source)

    def test_cold_and_static_reset_require_observed_state(self):
        source = fixture()
        case = source["cases"][0]
        case["history"] = "cold_create"
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)
        for sample in case["samples"]:
            sample["createdFeatureCount"] = 1
        self.assertEqual(rr.report(source)["measuredCaseCount"], 1)
        for sample in case["samples"]:
            sample["reset"] = False
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)

    def test_failed_warmup_and_false_continuous_history_do_not_qualify(self):
        source = fixture()
        case = source["cases"][0]
        case["samples"][0]["success"] = False
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)
        case["samples"][0]["success"] = True
        case["history"] = "continuous"
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)
        for sample in case["samples"][1:]:
            sample["reset"] = False
        self.assertEqual(rr.report(source)["measuredCaseCount"], 1)
        case["samples"][1]["createdFeatureCount"] = 1
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)

    def test_auto_mask_and_manual_control_contract(self):
        for field, value in (("controlMaskPassed", True), ("useAutoMask", False)):
            source = fixture()
            source["cases"][0][field] = value
            with self.subTest(field=field):
                self.assertEqual(rr.report(source)["measuredCaseCount"], 0)

    def test_capacity_pair_matches_content_guides_settings(self):
        source = paired()
        self.assertTrue(rr.report(source)["comparisons"][0]["comparable"])
        for field in ("sourceGuideAlignment", "colorConfiguration", "sourceFrameIndices"):
            changed = copy.deepcopy(source)
            changed["cases"][1][field] = [1] if field == "sourceFrameIndices" else "mismatched"
            with self.subTest(field=field):
                self.assertFalse(rr.report(changed)["comparisons"][0]["comparable"])
        source["cases"][1]["sourceContentSha256"] = "d" * 64
        with self.assertRaises(ValueError):
            rr.report(source)

    def test_independent_axis_geometry_and_equal_area(self):
        source = paired("width")
        source["cases"][1]["evaluatedRects"][0]["height"] = 7
        source["cases"][1]["evaluatedSourceRects"][0]["height"] = 7
        source["cases"][1]["evaluatedGuideRects"][0]["height"] = 7
        for sample in source["cases"][1]["samples"]:
            sample["providerFootprint"][0]["modifiedInsidePixels"] = 56
        self.assertIn("geometry_changed_outside_axis", rr.report(source)["comparisons"][0]["reasons"])
        source = paired("aspect_ratio")
        source["cases"][1]["evaluatedRects"][0].update(width=4, height=16)
        source["cases"][1]["evaluatedSourceRects"][0].update(width=4, height=16)
        source["cases"][1]["evaluatedGuideRects"][0].update(width=4, height=16)
        self.assertTrue(rr.report(source)["comparisons"][0]["comparable"])
        source["cases"][1]["evaluatedRects"][0]["height"] = 15
        source["cases"][1]["evaluatedSourceRects"][0]["height"] = 15
        source["cases"][1]["evaluatedGuideRects"][0]["height"] = 15
        for sample in source["cases"][1]["samples"]:
            sample["providerFootprint"][0]["modifiedInsidePixels"] = 60
        self.assertFalse(rr.report(source)["comparisons"][0]["comparable"])

    def test_distinct_guide_grid_and_compact_source_crop(self):
        source = paired()
        for case in source["cases"]:
            case["resourceExtents"]["depth"] = [4, 4]
            case["resourceExtents"]["motion"] = [4, 4]
            case["evaluatedGuideRects"][0].update(width=4, height=4)
            case["evaluatedSourceRects"][0].update(baseX=8, baseY=8)
        full = source["cases"][0]
        full["evaluatedRects"][0].update(baseX=8, baseY=8)
        full["resourceExtents"]["depth"] = [8, 8]
        full["resourceExtents"]["motion"] = [8, 8]
        full["evaluatedGuideRects"][0].update(baseX=4, baseY=4)
        self.assertTrue(rr.report(source)["comparisons"][0]["comparable"])
        changed = copy.deepcopy(source)
        changed["cases"][1]["evaluatedSourceRects"][0]["baseX"] = 9
        self.assertIn("unmatched_source_windows", rr.report(changed)["comparisons"][0]["reasons"])
        changed = copy.deepcopy(source)
        changed["cases"][1]["evaluatedGuideRects"][0]["width"] = 5
        with self.assertRaises(ValueError):
            rr.report(changed)
        changed["cases"][1]["evaluatedGuideRects"][0]["width"] = 3
        with self.assertRaises(ValueError):
            rr.report(changed)
        source = paired("unknown_axis")
        self.assertFalse(rr.report(source)["comparisons"][0]["comparable"])

    def test_provider_footprint_missing_unwritten_and_spare_writes(self):
        source = fixture()
        for sample in source["cases"][0]["samples"]:
            sample["providerFootprint"][0]["modifiedOutsidePixels"] = 192
        row = rr.report(source)["cases"][0]
        self.assertEqual(row["providerFootprint"][0]["modifiedOutsidePixels"]["maximum"], 192)
        self.assertEqual(row["evaluatedPixelsPerSample"], 64)
        self.assertIn("creation capacity", row["evaluatedAreaMeaning"])
        for sample in source["cases"][0]["samples"]:
            sample["providerFootprint"][0].update(modifiedInsidePixels=63, unchangedInsidePixels=1)
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)
        for sample in source["cases"][0]["samples"]:
            sample.pop("providerFootprint")
        self.assertEqual(rr.report(source)["measuredCaseCount"], 0)

    def test_minimum_shape_is_explicit_coupled_probe(self):
        source = paired("minimum_shape")
        case = source["cases"][1]
        for field in ("evaluatedRects", "evaluatedGuideRects", "evaluatedSourceRects"):
            case[field][0].update(width=7, height=7)
        for sample in case["samples"]:
            sample["providerFootprint"][0]["modifiedInsidePixels"] = 49
        compared = rr.report(source)["comparisons"][0]
        self.assertTrue(compared["comparable"])
        self.assertEqual(compared["scope"], "coupled_square_dimension_probe")

    def test_temporal_pairs_need_independent_equal_initialization(self):
        source = paired("offset")
        for case in source["cases"]:
            case["history"] = "continuous"
            for sample in case["samples"][1:]:
                sample["reset"] = False
        self.assertTrue(rr.report(source)["comparisons"][0]["comparable"])
        source["cases"][1]["contextIds"] = source["cases"][0]["contextIds"]
        self.assertIn("independent_contexts_not_proven", rr.report(source)["comparisons"][0]["reasons"])
        source["cases"][1].update(contextIds=["separate"], initializationFingerprint="changed")
        self.assertIn("equal_initialization_not_proven", rr.report(source)["comparisons"][0]["reasons"])
        source["cases"][1]["contextIds"] = [{}]
        self.assertIn("independent_contexts_not_proven", rr.report(source)["comparisons"][0]["reasons"])

    def test_unavailable_capture_has_no_measured_data(self):
        source = {"schema": rr.SCHEMA, "acquisitionUnavailable": "no source resource bundle", "cases": []}
        result = rr.report(source)
        self.assertEqual(result["measuredCaseCount"], 0)
        self.assertEqual(result["observedSuccessfulCallCounts"], [])
        self.assertIn("no source resource bundle", rr.markdown(result))
        for status, reason in (("failed", "no capture manifest"), ("input_validated_no_runtime_measurement", ""),
                               ("inspection_complete_no_evaluation", "")):
            result = rr.report({"schema": rr.SCHEMA, "status": status, "reason": reason, "cases": []})
            self.assertEqual(result["measuredCaseCount"], 0)
            self.assertTrue(result["acquisitionUnavailable"])

    def test_skipped_case_keeps_geometry_and_explanation_without_samples(self):
        source = fixture()
        source["cases"][0].update(status="unavailable", reason="fractional guide crop", sourceFrameIndices=[], samples=[])
        result = rr.report(source)
        self.assertEqual(result["measuredCaseCount"], 0)
        self.assertIn("fractional guide crop", result["cases"][0]["reasons"])
        self.assertEqual(result["cases"][0]["evaluatedPixelsPerSample"], 64)

    def test_cli_retains_receipt_hash_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.json"
            source.write_text(json.dumps(fixture()), encoding="utf-8")
            target = root / "report"
            command = [sys.executable, rr.__file__, str(source), "--output", str(target)]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads((target / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result["sourceReceipt"]["bytes"], source.stat().st_size)
            self.assertEqual(len(result["sourceReceipt"]["sha256"]), 64)
            self.assertIn("11.500", (target / "report.md").read_text(encoding="utf-8"))
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 2)


if __name__ == "__main__":
    unittest.main()
