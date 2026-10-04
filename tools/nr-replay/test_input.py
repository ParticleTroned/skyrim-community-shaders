"""Exercise native input admission with synthetic parser fixtures, never timing."""

import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = Path(sys.argv.pop(1)).resolve()


class ReplayInput(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nr-replay-input-")
        self.root = Path(self.temporary.name)
        self.manifest_path = self.root / "manifest.json"
        self.case = 0
        self.eye = {"slot": 0, "featureUpscaling": True,
                    "motionVectorScale": [64.0, 64.0],
                    "outputSubrect": {"baseX": 0, "baseY": 0, "width": 3, "height": 5}}
        for role in ("color", "depth", "motion", "output"):
            width, height = (2, 3) if role in ("depth", "motion") else (3, 5)
            format_ = 41 if role == "depth" else (34 if role == "motion" else 28)
            pixels = bytes([20, 40, 60, 255] * (width * height))
            if role == "depth":
                pixels = struct.pack("<f", 0.5) * (width * height)
            elif role == "motion":
                pixels = bytes(width * height * 4)
            elif role == "output":
                pixels = bytes([21, 40, 60, 255] * (width * height))
            file = self.root / (role + ".bin")
            file.write_bytes(pixels)
            self.eye[role] = {"file": file.name, "width": width, "height": height,
                              "format": format_, "rowBytes": width * 4,
                              "sha256": hashlib.sha256(pixels).hexdigest()}
        self.frame = {"sourceWorldFrame": 10, "frame": 10, "generation": 1,
                      "mode": 0, "insertionPoint": 0, "colorRevision": 1,
                      "inputEpoch": 1, "colorConfiguration": {},
                      "tuning": {"useAutoMask": True, "uiCorrection": False},
                      "eyes": [self.eye]}
        self.manifest = {"schema": "csx-nr-replay-input-v1", "complete": True,
                         "state": "complete", "frames": [self.frame]}

    def tearDown(self):
        self.temporary.cleanup()

    def run_fixture(self, expected=0, storage=False, case=None, rects=None, handle_policy=None, extra=()):
        self.case += 1
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        output = self.root / f"result-{self.case}"
        arguments = [str(EXECUTABLE), "--manifest", str(self.manifest_path), "--output", str(output),
                     "--validate-storage" if storage else "--validate-input"]
        if case:
            arguments += ["--case", case]
        if rects is not None:
            arguments += ["--rects", json.dumps(rects)]
        if handle_policy is not None:
            arguments += ["--native-handle-policy", handle_policy]
        arguments += list(extra)
        run = subprocess.run(arguments, capture_output=True, text=True, timeout=15)
        self.assertEqual(run.returncode, expected, run.stdout + run.stderr)
        result = (json.loads((output / "results.json").read_text(encoding="utf-8"))
                  if (output / "results.json").exists() else {"cases": [], "reason": run.stderr})
        self.assertEqual(result["cases"], [])
        return result

    def test_native_handle_plan_preserves_calls_and_private_outputs(self):
        self.prepare_storage_fixture(28, bytes([128, 128, 128, 255]),
                                     bytes([129, 128, 128, 255]), bytes([240, 128, 128, 255]), 256)
        self.frame["mode"] = 2
        self.eye["featureUpscaling"] = False
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        for eyes in (1, 2):
            self.frame["eyes"] = [self.eye] + ([copy.deepcopy(self.eye)] if eyes == 2 else [])
            for policy in ("independent", "per-eye"):
                result = self.run_fixture(rects=rects, handle_policy=policy)
                self.assertEqual(result["customRegions"], rects)
                plan = result["nativeHandlePlan"]
                slots = [eye if policy == "per-eye" else region * 4 + eye
                         for eye in range(eyes) for region in range(2)]
                self.assertEqual(plan["nativeHandleSlots"], slots)
                self.assertEqual(plan["nativeHandleCount"], len(set(slots)))
                self.assertEqual(plan["nativeHandleMask"], sum(1 << s for s in set(slots)))
                self.assertEqual(plan["nativeHandleReuseBarriers"], eyes if policy == "per-eye" else 0)

    def test_native_handle_probe_rejects_unsupported_contexts_before_runtime(self):
        self.prepare_storage_fixture(28, bytes([128, 128, 128, 255]),
                                     bytes([129, 128, 128, 255]), bytes([240, 128, 128, 255]), 256)
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        self.assertIn("stateless C", self.run_fixture(1, rects=rects, handle_policy="per-eye")["reason"])
        self.frame["mode"] = 2
        self.assertIn("equal input grids", self.run_fixture(1, rects=rects, handle_policy="per-eye")["reason"])
        self.eye["featureUpscaling"] = False
        for bad in ([], [rects[0]], [[0, 0, 64, 128], rects[1]]):
            self.run_fixture(1, rects=bad, handle_policy="per-eye")
        self.assertIn("policy must", self.run_fixture(1, rects=rects, handle_policy="shared")["reason"])
        self.run_fixture(1, handle_policy="per-eye")
        self.run_fixture(1, rects=rects, handle_policy="per-eye", case="capacity-full")
        self.run_fixture(1, rects=rects, handle_policy="per-eye", extra=("--capacity-temporal",))
        self.run_fixture(1, rects=rects, handle_policy="per-eye", extra=("--capacity-size", "256"))
        self.run_fixture(1, rects=rects, handle_policy="per-eye", storage=True)

    def test_custom_regions_are_bounded_stateless_and_exact(self):
        self.prepare_storage_fixture(28, bytes([128, 128, 128, 255]),
                                     bytes([129, 128, 128, 255]), bytes([240, 128, 128, 255]), 128)
        rects = [[0, 0, 64, 64], [128, 128, 64, 64]]
        self.assertIn("stateless C", self.run_fixture(1, rects=rects)["reason"])
        self.frame["mode"] = 2
        self.assertEqual(self.run_fixture(rects=rects)["customRegions"], rects)
        for invalid in ([], [[0, 0, 63, 64]], [[240, 0, 64, 64]], [[-1, 0, 64, 64]],
                        [[0.5, 0, 64, 64]], [[True, 0, 64, 64]], [[0, 0, 64]], rects * 5,
                        [[0, 0, 64, 64], [64, 0, 64, 64], [128, 0, 64, 64]],
                        [[0, 0, 64, 64], [32, 0, 64, 64]],
                        [[0, 0, 2**32, 64]]):
            self.run_fixture(1, rects=invalid)

    def prepare_floor_fixture(self):
        self.prepare_storage_fixture(28, bytes([128, 128, 128, 255]),
                                     bytes([129, 128, 128, 255]), bytes([240, 128, 128, 255]), 256)
        self.frame["mode"] = 2
        self.eye["featureUpscaling"] = False
        self.manifest["runtime"] = {"sha256": "8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206"}

    def test_floor_probe_is_explicit_and_preserves_independent_single_context(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 192, 256]]
        baseline = self.run_fixture(rects=rects)
        self.assertEqual(baseline["providerFloorExperiment"], {"requested": False})
        result = self.run_fixture(rects=rects, extra=("--experimental-provider-floor", "256"))
        probe = result["providerFloorExperiment"]
        self.assertEqual(result["customRegions"], rects)
        self.assertEqual(probe["experimentalFloor"], 256)
        self.assertEqual(probe["inMemoryState"], "not_loaded")
        self.assertEqual(probe["transitions"], [])
        self.assertFalse(probe["qualityQualified"])
        self.assertFalse(probe["productionPerformanceQualified"])
        self.assertEqual(probe["modeledShapes"][0]["original"], [320, 320])
        self.assertEqual(probe["modeledShapes"][0]["candidate"], [320, 256])

    def test_kernel_chain_modes_preserve_context_and_reject_combined_experiments(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        self.assertEqual(self.run_fixture(rects=rects)["kernelChainExperiment"], {"requested": False})
        for mode in ("forward", "group"):
            switch = ("--experimental-kernel-chain", mode)
            result = self.run_fixture(rects=rects, extra=switch)
            self.assertEqual(result["customRegions"], rects)
            self.assertEqual(result["providerFloorExperiment"], {"requested": False})
            probe = result["kernelChainExperiment"]
            self.assertEqual(probe["mode"], mode)
            self.assertEqual(probe["inMemoryState"], "not_loaded")
            self.assertFalse(probe["qualityQualified"])
            self.assertFalse(probe["productionPerformanceQualified"])
            self.run_fixture(1, rects=rects, extra=switch + switch)
            self.run_fixture(1, extra=switch)
            self.run_fixture(1, rects=rects, extra=switch, handle_policy="independent")
            self.run_fixture(1, rects=rects, extra=switch, storage=True)
            for option in (("--experimental-provider-floor", "256"), ("--renderdoc", "unrequested.dll"),
                           ("--capacity-temporal",), ("--capacity-size", "256")):
                self.run_fixture(1, rects=rects, extra=switch + option)
        self.run_fixture(1, rects=rects, extra=("--experimental-kernel-chain", "unknown"))
        self.run_fixture(1, rects=[[0, 0, 128, 128], [128, 0, 128, 256]],
                         extra=("--experimental-kernel-chain", "group"))
        self.manifest["runtime"]["sha256"] = "0" * 64
        self.assertIn("pinned provider", self.run_fixture(1, rects=rects,
                      extra=("--experimental-kernel-chain", "forward"))["reason"])

    def test_kernel_module_capture_requires_forward_probe(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        self.assertFalse(self.run_fixture(rects=rects)["captureKernelModules"])
        switch = ("--experimental-kernel-chain", "forward", "--capture-kernel-modules")
        result = self.run_fixture(rects=rects, extra=switch)
        self.assertTrue(result["captureKernelModules"])
        self.assertEqual(result["kernelChainExperiment"]["inMemoryState"], "not_loaded")
        self.run_fixture(1, rects=rects, extra=("--capture-kernel-modules",))
        self.run_fixture(1, rects=rects, extra=("--experimental-kernel-chain", "group", "--capture-kernel-modules"))
        self.run_fixture(1, rects=rects, extra=switch + ("--capture-kernel-modules",))
        self.run_fixture(1, rects=rects, extra=switch, storage=True)

    def test_kernel_replacement_rejects_missing_identity_and_parser_only_modes(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        replacement = ("--experimental-kernel-replacement", "not-loaded.json")
        for capture in ((), ("--experimental-kernel-chain", "forward"),
                        ("--experimental-kernel-chain", "forward", "--capture-kernel-modules")):
            self.run_fixture(1, rects=rects, extra=capture + replacement)
        self.run_fixture(1, rects=rects, extra=replacement + replacement)

    def test_kernel_pair_rejects_unqualified_and_parser_only_modes(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        capture = ("--experimental-kernel-chain", "forward", "--capture-kernel-modules", "--batch-timing-only")
        for mode in ("original", "control", "layer-control", "batch", "model-batch", "unknown"):
            pair = ("--experimental-kernel-pair", mode)
            self.run_fixture(1, rects=rects, extra=pair)
            self.run_fixture(1, rects=rects, extra=capture + pair)
        pair = ("--experimental-kernel-pair", "control")
        self.run_fixture(1, rects=rects, extra=capture + pair + pair)
        self.run_fixture(1, rects=rects, extra=capture + pair + ("--warmup", "0"))

    def test_model_replacement_requires_owned_live_pair_schedule(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        replacement = ("--experimental-model-replacement", "not-loaded.json")
        capture = ("--experimental-kernel-chain", "forward", "--capture-kernel-modules", "--batch-timing-only")
        self.run_fixture(1, rects=rects, extra=replacement)
        self.run_fixture(1, rects=rects, extra=replacement + replacement)
        for mode in ("original", "control", "layer-control", "batch", "model-batch"):
            self.run_fixture(1, rects=rects, extra=capture + ("--experimental-kernel-pair", mode) + replacement)

    def test_batch_only_timing_is_explicit_for_bounded_independent_contexts(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 128, 128], [128, 128, 128, 128]]
        self.assertFalse(self.run_fixture(rects=rects)["batchTimingOnly"])
        for mode in (None, "forward", "group"):
            switch = ("--batch-timing-only",) + (("--experimental-kernel-chain", mode) if mode else ())
            result = self.run_fixture(rects=rects, extra=switch)
            self.assertTrue(result["batchTimingOnly"])
            self.assertEqual(result["customRegions"], rects)
        self.run_fixture(1, extra=("--batch-timing-only",))
        self.run_fixture(1, rects=rects, extra=("--batch-timing-only", "--batch-timing-only"))
        self.run_fixture(1, rects=rects, handle_policy="per-eye", extra=("--batch-timing-only",))
        self.run_fixture(1, rects=rects, extra=("--batch-timing-only", "--experimental-provider-floor", "256"))

    def test_model_batch_stage_prefix_is_bounded_and_requires_live_model_mode(self):
        for value in ("0", "159", "1000", "-1", "+1", "1junk", "1.0", ""):
            result = self.run_fixture(1, extra=("--experimental-model-batch-stages", value))
            self.assertIn("model batch stages", result["reason"])
        for value in ("1", "157", "158"):
            result = self.run_fixture(1, extra=("--experimental-model-batch-stages", value))
            self.assertIn("requires the model-batch pair schedule", result["reason"])
        self.assertIn("specified once", self.run_fixture(1, extra=(
            "--experimental-model-batch-stages", "1", "--experimental-model-batch-stages", "2"))["reason"])

    def test_model_n1_stage_prefix_is_bounded_and_requires_live_n1_mode(self):
        for value in ("0", "159", "1000", "-1", "+1", "1junk", "1.0", ""):
            result = self.run_fixture(1, extra=("--experimental-model-n1-stages", value))
            self.assertIn("model N1 stages", result["reason"])
        for value in ("1", "157", "158"):
            result = self.run_fixture(1, extra=("--experimental-model-n1-stages", value))
            self.assertIn("requires a single-pass", result["reason"])
        self.assertIn("specified once", self.run_fixture(1, extra=(
            "--experimental-model-n1-stages", "1", "--experimental-model-n1-stages", "2"))["reason"])

    def test_kernel_repetitions_require_bounded_live_complete_schedules(self):
        for value in ("0", "1", "5", "100", "-1", "+2", "02", "2junk", "2.0", ""):
            result = self.run_fixture(1, extra=("--experimental-kernel-repetitions", value))
            self.assertIn("kernel repetitions must be exactly", result["reason"])
        for value in ("2", "3", "4"):
            result = self.run_fixture(1, extra=("--experimental-kernel-repetitions", value))
            self.assertIn("requires a complete", result["reason"])
        self.assertIn("specified once", self.run_fixture(1, extra=(
            "--experimental-kernel-repetitions", "2", "--experimental-kernel-repetitions", "3"))["reason"])
        self.prepare_floor_fixture()
        for mode in ("original", "layer-control", "model-batch"):
            result = self.run_fixture(1, rects=[[0, 0, 192, 256]], extra=(
                "--experimental-kernel-chain", "forward", "--capture-kernel-modules",
                "--batch-timing-only", "--experimental-kernel-pair", mode,
                "--experimental-kernel-repetitions", "4"))
            self.assertIn("requires live forward", result["reason"])

    def test_kernel_comparison_requires_four_complete_model_repetitions(self):
        for value in ("", "batch", "model-batch", "forward", "ORIGINAL"):
            result = self.run_fixture(1, extra=("--experimental-kernel-comparison", value))
            self.assertIn("kernel comparison must be", result["reason"])
        for mode in ("original", "layer-control"):
            result = self.run_fixture(1, extra=("--experimental-kernel-comparison", mode))
            self.assertIn("with four complete model-batch repetitions", result["reason"])
        self.assertIn("specified once", self.run_fixture(1, extra=(
            "--experimental-kernel-comparison", "original",
            "--experimental-kernel-comparison", "layer-control"))["reason"])

    def test_floor_probe_rejects_incompatible_controls_and_geometry(self):
        self.prepare_floor_fixture()
        rects = [[0, 0, 192, 256]]
        switch = ("--experimental-provider-floor", "256")
        for value in ("0", "128", "320", "256junk", "0256", "-256"):
            self.run_fixture(1, rects=rects, extra=("--experimental-provider-floor", value))
        self.run_fixture(1, rects=rects, extra=switch + switch)
        self.run_fixture(1, extra=switch)
        self.run_fixture(1, rects=rects, extra=switch, handle_policy="independent")
        self.run_fixture(1, rects=rects, extra=switch, storage=True)
        self.run_fixture(1, rects=rects, extra=switch + ("--capacity-size", "256"))
        self.run_fixture(1, rects=rects, extra=switch + ("--renderdoc", "unrequested.dll"))
        self.run_fixture(1, rects=[[0, 0, 64, 128]], extra=switch)
        self.eye["featureUpscaling"] = True
        self.assertIn("equal input grids", self.run_fixture(1, rects=rects, extra=switch)["reason"])
        self.eye["featureUpscaling"] = False
        self.frame["mode"] = 1
        self.run_fixture(1, rects=rects, extra=switch)
        self.frame["mode"] = 2
        self.manifest["runtime"]["sha256"] = "0" * 64
        self.assertIn("pinned provider", self.run_fixture(1, rects=rects, extra=switch)["reason"])
        self.prepare_floor_fixture()
        second = copy.deepcopy(self.frame)
        second["sourceWorldFrame"] = 11
        self.manifest["frames"].append(second)
        self.assertIn("one immutable", self.run_fixture(1, rects=rects, extra=switch)["reason"])

    def test_scaled_odd_native_grids(self):
        result = self.run_fixture()
        self.assertEqual(result["status"], "input_validated_no_runtime_measurement")
        identity = result["buildIdentity"]
        self.assertEqual(identity["executableSha256"], hashlib.sha256(EXECUTABLE.read_bytes()).hexdigest())
        repository = Path(__file__).resolve().parents[2]
        ngx_library = repository / "extern/Streamline-DX12/external/ngx-sdk/lib/Windows_x86_64/nvsdk_ngx_d.lib"
        self.assertEqual(identity["ngxLibrarySha256"], hashlib.sha256(ngx_library.read_bytes()).hexdigest())
        self.assertTrue({"src/Features/Upscaling/NeuralRendering/" + name + ".h" for name in
                         ("NativeEvaluationLayout", "Runtime", "D3D12Interop", "ExecutionEvidence",
                          "ComputeSubrect", "PipelinePolicy", "CharacterMultiRoi", "CharacterComputeSubrect",
                          "CharacterMaskWorkPolicy", "CharacterRegionPolicy", "RoiDescriptor")}
                        <= identity["replaySourceSha256"].keys())
        for path, digest in identity["replaySourceSha256"].items():
            self.assertEqual(len(digest), 64)
            if not path.startswith("generated/"):
                self.assertEqual(digest, hashlib.sha256((repository / path).read_bytes()).hexdigest())

    def test_checksum_is_required_and_checked(self):
        self.eye["color"]["sha256"] = "0" * 64
        self.assertIn("hash", self.run_fixture(1)["reason"])
        del self.eye["color"]["sha256"]
        self.run_fixture(1)

    def test_partial_capture_rejected(self):
        self.manifest["complete"] = False
        self.assertIn("incomplete", self.run_fixture(1)["reason"])

    def test_payload_size_and_row_pitch(self):
        self.eye["color"]["rowBytes"] = 16
        self.assertIn("packed", self.run_fixture(1)["reason"])

    def test_path_escape_rejected(self):
        self.eye["color"]["file"] = "../outside.bin"
        self.assertIn("escapes", self.run_fixture(1)["reason"])

    def test_zero_edit_control_rejected(self):
        self.eye["output"] = copy.deepcopy(self.eye["color"])
        self.assertIn("nonzero", self.run_fixture(1)["reason"])

    def test_native_float_decoders_and_nonfinite_control(self):
        for format_, source, edited, nan in (
            (10, struct.pack("<4H", 0x3800, 0x3800, 0x3800, 0x3c00),
             struct.pack("<4H", 0x3801, 0x3800, 0x3800, 0x3c00),
             struct.pack("<4H", 0x7e01, 0x3800, 0x3800, 0x3c00)),
            (26, struct.pack("<I", 0x3c0 | (0x3c0 << 11) | (0x1e0 << 22)),
             struct.pack("<I", 0x3c1 | (0x3c0 << 11) | (0x1e0 << 22)),
             struct.pack("<I", 0x7c1 | (0x3c0 << 11) | (0x1e0 << 22))),
        ):
            for role, pixel in (("color", source), ("output", edited)):
                payload = pixel * 15
                (self.root / (role + ".bin")).write_bytes(payload)
                self.eye[role].update(format=format_, rowBytes=len(pixel) * 3,
                                      sha256=hashlib.sha256(payload).hexdigest())
            self.run_fixture()
            payload = nan * 15
            (self.root / "output.bin").write_bytes(payload)
            self.eye["output"]["sha256"] = hashlib.sha256(payload).hexdigest()
            self.assertIn("nonfinite", self.run_fixture(1)["reason"])

    def test_temporal_gap_and_repeat_rejected(self):
        second = copy.deepcopy(self.frame)
        self.manifest["frames"].append(second)
        self.run_fixture(1)
        second["sourceWorldFrame"] = 12
        self.run_fixture(1)
        second["sourceWorldFrame"] = 11
        self.run_fixture()

    def prepare_storage_fixture(self, format_, pixel, edited, peak, guide_width=128):
        self.eye["outputSubrect"].update(width=256, height=256)
        originals = {}
        for role in ("color", "output", "depth", "motion"):
            width = 256 if role in ("color", "output") else guide_width
            fmt = format_ if role in ("color", "output") else 41 if role == "depth" else 34
            item = pixel if role == "color" else edited if role == "output" else struct.pack("<f", .5) if role == "depth" else bytes(4)
            payload = bytearray(item * width * width)
            if role == "output":
                offset = (173 * width + 187) * len(item)
                payload[offset:offset + len(item)] = peak
            (self.root / (role + ".bin")).write_bytes(payload)
            self.eye[role].update(width=width, height=width, format=fmt, rowBytes=width * len(item), sha256=hashlib.sha256(payload).hexdigest())
            originals[role] = bytes(payload)
        return originals

    def test_storage_patterns_preserve_offset_valid_inputs_in_every_format(self):
        for format_, pixel, edited, peak in (
            (28, bytes([128, 128, 128, 255]), bytes([129, 128, 128, 255]), bytes([240, 128, 128, 255])),
            (10, struct.pack("<4e", .5, .5, .5, 1), struct.pack("<4e", .6, .5, .5, 1), struct.pack("<4e", .9, .5, .5, 1)),
            (2, struct.pack("<4f", .5, .5, .5, 1), struct.pack("<4f", .6, .5, .5, 1), struct.pack("<4f", .9, .5, .5, 1)),
            (26, struct.pack("<I", 0x380 | (0x380 << 11) | (0x1c0 << 22)),
             struct.pack("<I", 0x381 | (0x380 << 11) | (0x1c0 << 22)),
             struct.pack("<I", 0x3c0 | (0x380 << 11) | (0x1c0 << 22))),
        ):
            originals = self.prepare_storage_fixture(format_, pixel, edited, peak)
            result = self.run_fixture(storage=True)
            self.assertEqual(len(result["inputStorageValidation"]), 3)
            for case in result["inputStorageValidation"]:
                for proof in case["inputs"]:
                    source = originals[proof["resource"]]
                    width, height, fmt = proof["width"], proof["height"], proof["format"]
                    stride = len(source) // (width * height)
                    rect = proof["validRect"]
                    self.assertGreater(rect["baseX"], 0)
                    self.assertGreater(rect["baseY"], 0)
                    valid = b"".join(source[(y * width + rect["baseX"]) * stride:(y * width + rect["baseX"] + rect["width"]) * stride]
                                     for y in range(rect["baseY"], rect["baseY"] + rect["height"]))
                    self.assertEqual(proof["validSha256"], hashlib.sha256(valid).hexdigest())
                    expected, changed = bytearray(source), 0
                    for y in range(height):
                        for x in range(width):
                            if (rect["baseX"] <= x < rect["baseX"] + rect["width"]
                                    and rect["baseY"] <= y < rect["baseY"] + rect["height"]) or proof["policy"] == "captured":
                                continue
                            a = (x ^ y) & 1
                            patterns = {28: bytes([255 * a, 255 * (1-a), 255 * a, 255]),
                                        10: struct.pack("<4e", a, 1-a, a, 1),
                                        2: struct.pack("<4f", a, 1-a, a, 1),
                                        26: struct.pack("<I", 0x3c0 | (0x1e0 << 22) if a else 0x3c0 << 11),
                                        41: struct.pack("<f", .25 if a else .75),
                                        34: struct.pack("<2e", 1 if a else -1, -1 if a else 1)}
                            replacement = bytes(stride) if proof["policy"] == "zero" else patterns[fmt]
                            offset = (y * width + x) * stride
                            changed += source[offset:offset + stride] != replacement
                            expected[offset:offset + stride] = replacement
                    self.assertEqual(proof["changedOutsidePixels"], changed)
                    self.assertEqual(proof["capturedSha256"], hashlib.sha256(source).hexdigest())
                    self.assertEqual(proof["uploadedSha256"], hashlib.sha256(expected).hexdigest())

    def test_storage_validation_rejects_unsupported_small_capture(self):
        self.assertIn("256x256", self.run_fixture(1, storage=True)["reason"])

    def test_input_context_changes_only_selected_resource_outside_mapped_halo(self):
        originals = self.prepare_storage_fixture(28, bytes([128, 128, 128, 255]),
                                                bytes([129, 128, 128, 255]), bytes([240, 128, 128, 255]), 171)
        for role in ("all", "color", "depth", "motion"):
            for halo in (16, 16384):
                result = self.run_fixture(storage=True, case=f"input-context-{role}-finite_pattern-{halo}")
                self.assertEqual(len(result["inputStorageValidation"]), 1)
                proofs = result["inputStorageValidation"][0]["inputs"]
                color = proofs[0]["validRect"]
                x, y = max(0, color["baseX"] - halo), max(0, color["baseY"] - halo)
                r, b = min(256, color["baseX"] + color["width"] + halo), min(256, color["baseY"] + color["height"] + halo)
                for proof in proofs:
                    w = proof["width"]
                    expected = dict(baseX=x*w//256, baseY=y*w//256, width=(r*w+255)//256-x*w//256, height=(b*w+255)//256-y*w//256)
                    self.assertEqual(proof["preservedRect"], expected)
                    source = originals[proof["resource"]]
                    preserved = b"".join(source[(row*w+expected["baseX"])*4:(row*w+expected["baseX"]+expected["width"])*4]
                                         for row in range(expected["baseY"], expected["baseY"]+expected["height"]))
                    self.assertEqual(proof["preservedSha256"], hashlib.sha256(preserved).hexdigest())
                    selected = role in ("all", proof["resource"])
                    self.assertEqual(proof["policy"], "finite_pattern" if selected else "captured")
                    if selected and halo == 16:
                        self.assertEqual(proof["changedOutsidePixels"], w*w-expected["width"]*expected["height"])
                    else:
                        self.assertEqual(proof["changedOutsidePixels"], 0)
                        self.assertEqual(proof["uploadedSha256"], hashlib.sha256(source).hexdigest())
        self.assertIn("known storage case", self.run_fixture(1, storage=True, case="input-context-all-zero-999")["reason"])

    def test_existing_output_preserved(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        target = self.root / "existing"
        target.mkdir()
        marker = target / "results.json"
        marker.write_bytes(b"user-owned-evidence")
        run = subprocess.run([str(EXECUTABLE), "--manifest", str(self.manifest_path),
                              "--output", str(target), "--validate-input"],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(marker.read_bytes(), b"user-owned-evidence")


if __name__ == "__main__":
    unittest.main()
