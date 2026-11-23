"""Regression coverage for combined depth and Horizon Fix cache packs."""

from __future__ import annotations

import copy
import inspect
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from dxbc_fixtures import make_dxbc
import shader_cache_packaging_test as packaging
from shader_cache_packaging_test import BUILDER, REPO
from fomod_package_test import BUILDER as FOMOD

RUNTIME_TEST = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None


class DepthVariantTests(unittest.TestCase):
    ABI = "a" * 64

    @staticmethod
    def loose(cache: Path, reverse: bool, horizon: bool) -> None:
        helper = f"Standalone/{'b' if reverse else 'a'}{'0' * 31}.cso"
        values = {
            "Water/1.pso": f"water-{reverse}-{horizon}",
            "Lighting/4019.pso": f"pbr-detail-{reverse}",
            "Lighting/104019.pso": f"pbr-detail-alpha-{reverse}",
            helper: f"helper-{reverse}",
        }
        for path, value in values.items():
            target = cache / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(make_dxbc(stage=5 if path == helper else 0, marker=value.encode()))
        (cache / BUILDER.INFO_FILE_NAME).write_text(
            f"[Cache]\nPluginVersion = CSX 3.18-VR\nShaderCacheABI = {DepthVariantTests.ABI}\n", encoding="utf-8"
        )
        (cache / BUILDER.MANIFEST_FILE_NAME).write_text(json.dumps({
            "schemaVersion": BUILDER.MANIFEST_SCHEMA_VERSION,
            "entries": {path: BUILDER.sha256_hex(value)[:32] for path, value in values.items()},
            "standaloneInputs": {helper: {"source": "Common/DepthPyramidBaseCS.hlsl", "profile": "cs_5_0"}},
        }), encoding="utf-8")

    def pack(self, root: Path, *, horizon: bool = True) -> Path:
        inputs = {}
        for reverse in (False, True):
            suffix = "-ReverseZ" if reverse else ""
            standard = root / (BUILDER.CACHE_DIRECTORY + suffix)
            horizon_cache = root / (BUILDER.HORIZON_FIX_CACHE_DIRECTORY + suffix) if horizon else None
            self.loose(standard, reverse, False)
            if horizon_cache:
                self.loose(horizon_cache, reverse, True)
            inputs["on" if reverse else "off"] = (standard, horizon_cache)
        standard, horizon_cache = inputs["off"]
        BUILDER.build_managed_shader_packs(REPO, standard, horizon_cache, "VR", self.ABI, depth_inputs=inputs)
        self.assertEqual({path.name for path in root.iterdir()}, {BUILDER.CACHE_DIRECTORY})
        return standard

    @staticmethod
    def validate(cache: Path, manifest: dict) -> None:
        inventory = BUILDER.PackagedCompatibilityInventory(
            REPO, manifest["compatibilityVariants"], manifest.get("standaloneInputs", {}), manifest.get("depthVariants")
        )
        stats = {name: BUILDER.validate_shader_pack(
            cache / name, lane, manifest["packSetId"],
            inspect_record=inventory.inspector(name) if lane == 1 else None,
        ) for name, lane in BUILDER.PACK_LANES.items()}
        BUILDER.validate_pack_manifest_contract(manifest, "VR", stats, required_depth_modes=("off", "on"))
        inventory.validate(stats)

    def test_combined_pack_and_fomod_admit_all_four_conditions(self) -> None:
        for horizon in (False, True):
            with self.subTest(horizon=horizon), tempfile.TemporaryDirectory() as temporary:
                cache = self.pack(Path(temporary), horizon=horizon)
                manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
                self.assertEqual(manifest["optimizedRecordCount"], 10 if horizon else 8)
                self.assertEqual(set(manifest["depthVariants"]), {"off", "on"})
                for variants in manifest["depthVariants"].values():
                    self.assertEqual(set(variants), {"default", "legacy-horizon-fix"} if horizon else {"default"})
                    self.assertTrue(all(len(keys) == 4 for keys in variants.values()))
                self.validate(cache, manifest)
                FOMOD.validate_cache_source(cache, FOMOD.RUNTIME_VR, self.ABI, horizon)
                if shutil.which("cmake"):
                    archive_root = Path(temporary) / "archive"
                    archive_root.mkdir()
                    shutil.copytree(cache, archive_root / BUILDER.CACHE_DIRECTORY)
                    archive = packaging.ShaderCachePackagingTests._archive_cache(archive_root)
                    BUILDER.validate_cache_archive(archive, shutil.which("cmake"), "VR", "CSX 3.18-VR",
                                                   horizon_variants=horizon, required_depth_modes=("off", "on"))

    def test_missing_mislabeled_and_duplicate_depth_records_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.pack(Path(temporary))
            original = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
            mutations = {
                "missing-mode": lambda value: value["depthVariants"].pop("on"),
                "missing-inventory": lambda value: value.pop("depthVariants"),
                "missing-horizon": lambda value: value["depthVariants"]["on"].pop("legacy-horizon-fix"),
                "unknown-record": lambda value: value["depthVariants"]["on"].update(default=["0" * 64]),
                "same-depth-twice": lambda value: value["depthVariants"].update(on=copy.deepcopy(value["depthVariants"]["off"])),
                "wrong-compatibility": lambda value: value["depthVariants"]["on"].update(default=value["depthVariants"]["on"]["legacy-horizon-fix"]),
                "duplicate-key": lambda value: value["depthVariants"]["on"]["default"].append(value["depthVariants"]["on"]["default"][0]),
                "missing-key": lambda value: value["depthVariants"]["on"]["default"].pop(),
                "bad-type": lambda value: value.update(depthVariants=[]),
            }
            for name, mutate in mutations.items():
                with self.subTest(name=name):
                    manifest = copy.deepcopy(original)
                    mutate(manifest)
                    with self.assertRaises(SystemExit):
                        self.validate(cache, manifest)
            manifest = copy.deepcopy(original)
            mutations["unknown-record"](manifest)
            (cache / BUILDER.PACK_MANIFEST_FILE_NAME).write_text(json.dumps(manifest))
            with self.assertRaises(SystemExit):
                FOMOD.validate_cache_source(cache, FOMOD.RUNTIME_VR, self.ABI)

    def test_builder_dispatches_each_depth_and_preserves_single_mode(self) -> None:
        for both, reverse, horizon in ((True, False, True), (True, False, False), (False, True, True), (False, False, True)):
            with self.subTest(both=both, reverse=reverse, horizon=horizon), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                signature = inspect.signature(BUILDER.compile_runtime_depth)
                calls = []

                def compile_depth(*args, **kwargs):
                    request = signature.bind(*args, **kwargs).arguments
                    depth = request["reverse_z_enabled"]
                    calls.append(depth)
                    result = {}
                    for variant in BUILDER.compile_variants_for(BUILDER.SHIPPED_CACHE_PROFILE, horizon):
                        cache = root / "VR" / (variant.directory + request["cache_suffix"])
                        self.loose(cache, depth, variant.horizon_fix_enabled)
                        result[variant.name] = (cache, 4, 3, {"ReverseZ": True, "HorizonFix": variant.horizon_fix_enabled})
                    return result

                with mock.patch.object(BUILDER, "compile_runtime_depth", side_effect=compile_depth):
                    runtime_root, counts, sections = BUILDER.build_runtime(
                        REPO, root, root, "VR", REPO / ".github/configs/shader-validation-vr.yaml",
                        "test", 1, "fxc", ("compiler",), None, lambda: None, BUILDER.SHIPPED_CACHE_PROFILE,
                        include_horizon_fix=horizon, reverse_z_enabled=reverse, both_depth_conventions=both,
                    )
                self.assertEqual(calls, [False, True] if both else [reverse])
                self.assertEqual(len(counts), len(calls) * (2 if horizon else 1))
                self.assertEqual(sections, 3)
                manifest = json.loads((runtime_root / BUILDER.CACHE_DIRECTORY / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
                self.assertEqual(set(manifest["depthVariants"]), {"off", "on"} if both else {"on" if reverse else "off"})

    def test_cli_rejects_conflicting_depth_selections(self) -> None:
        for flag in ("--reverse-z", "--no-reverse-z"):
            result = subprocess.run([sys.executable, str(REPO / "tools/build-shader-cache.py"),
                                     "--both-depth-conventions", flag], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("not allowed with argument", result.stderr)

    def test_depth_manifest_cannot_omit_a_seed_permutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.pack(Path(temporary))
            manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
            omitted = set()
            BUILDER.validate_shader_pack(
                cache / "Optimized.A.csxpack", 1, manifest["packSetId"],
                inspect_record=lambda logical, exact, metadata, bytecode:
                    omitted.add(BUILDER.sha256_hex(exact)) if logical.startswith("Lighting/4019.pso|") else None,
            )
            self.assertEqual(len(omitted), 2)
            for variants in manifest["depthVariants"].values():
                for name, keys in variants.items():
                    variants[name] = sorted(set(keys) - omitted)
            with self.assertRaisesRegex(SystemExit, "depth coverage.*baseline"):
                self.validate(cache, manifest)
            (cache / BUILDER.PACK_MANIFEST_FILE_NAME).write_text(json.dumps(manifest))
            with self.assertRaisesRegex(SystemExit, "depth coverage.*baseline"):
                FOMOD.validate_cache_source(cache, FOMOD.RUNTIME_VR, self.ABI)

    def test_horizon_records_cannot_belong_to_both_depths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.pack(Path(temporary))
            manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
            off, on = (manifest["depthVariants"][depth] for depth in ("off", "on"))
            off_water = set(off["legacy-horizon-fix"]) - set(off["default"])
            self.assertEqual(len(off_water), 1)
            on["legacy-horizon-fix"] = sorted(set(on["legacy-horizon-fix"]) | off_water)
            with self.assertRaisesRegex(SystemExit, "unchanged engine compile identity"):
                self.validate(cache, manifest)

    def test_depth_baseline_survives_runtime_append_and_compaction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.pack(Path(temporary))
            manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
            records = []
            BUILDER.validate_shader_pack(
                cache / "Optimized.A.csxpack", 1, manifest["packSetId"],
                inspect_record=lambda logical, exact, metadata, bytecode:
                    records.append({"logicalKey": logical, "exactKey": exact, "metadata": metadata, "bytecode": bytecode}),
            )
            records.append({
                **BUILDER.shader_pack_record_identity("Lighting/4019.pso", "f" * 32, []),
                "bytecode": make_dxbc(marker=b"runtime-update"),
            })
            for name, generation in (("Optimized.A.csxpack", 1), ("Optimized.B.csxpack", 2)):
                with self.subTest(name=name):
                    BUILDER.write_shader_pack(cache / name, 1, generation, records, manifest["packSetId"])
                    self.validate(cache, manifest)
                    FOMOD.validate_cache_source(cache, FOMOD.RUNTIME_VR, self.ABI)

    def test_feature_abi_drift_fails_before_merging_depths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            results = []
            for reverse in (False, True):
                cache = root / "VR" / ("ShaderCache-ReverseZ" if reverse else "ShaderCache")
                self.loose(cache, reverse, False)
                with (cache / BUILDER.INFO_FILE_NAME).open("a", encoding="utf-8") as info:
                    info.write(f"[ReverseZ]\nEnabled = true\nShaderCacheABI = {int(reverse) + 1}\n")
                results.append({"standard": (cache, 4, 1, {"ReverseZ": True})})
            with mock.patch.object(BUILDER, "compile_runtime_depth", side_effect=results), \
                    mock.patch.object(BUILDER, "build_managed_shader_packs") as pack:
                with self.assertRaisesRegex(SystemExit, "feature shader ABI"):
                    BUILDER.build_runtime(REPO, root, root, "VR", root / "config.yaml", "test", 1,
                                          "fxc", ("compiler",), None, lambda: None, BUILDER.SHIPPED_CACHE_PROFILE,
                                          include_horizon_fix=False, both_depth_conventions=True)
                pack.assert_not_called()

    def test_invalid_depth_inventory_does_not_write_packs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            standard, reverse = root / "ShaderCache", root / "ShaderCache-ReverseZ"
            for cache in (standard, reverse):
                self.loose(cache, False, False)
            with mock.patch.object(BUILDER, "write_shader_pack", wraps=BUILDER.write_shader_pack) as write:
                with self.assertRaises(SystemExit):
                    BUILDER.build_managed_shader_packs(REPO, standard, None, "VR", self.ABI,
                                                       depth_inputs={"off": (standard, None), "on": (reverse, None)})
                write.assert_not_called()
            for cache in (standard, reverse):
                self.assertTrue((cache / "Lighting/4019.pso").exists())
                self.assertFalse(any(cache.glob("*.csxpack")))

    @unittest.skipUnless(RUNTIME_TEST, "native pack test executable is required")
    def test_native_reader_finds_each_combined_record(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.pack(Path(temporary))
            manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text())
            keys = []
            BUILDER.validate_shader_pack(cache / "Optimized.A.csxpack", 1, manifest["packSetId"],
                                         inspect_record=lambda logical, exact, metadata, bytecode: keys.append(exact))
            for exact in keys:
                subprocess.run([str(RUNTIME_TEST), str(cache / "Optimized.A.csxpack"),
                                str(cache / "Optimized.B.csxpack"), exact, manifest["packSetId"]], check=True)

    def test_conflicting_bytecode_preserves_loose_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            standard, horizon = root / "ShaderCache", root / "ShaderCache-HorizonFix"
            self.loose(standard, False, False)
            self.loose(horizon, False, True)
            (horizon / "Lighting/4019.pso").write_bytes(make_dxbc(marker=b"conflict"))
            with self.assertRaisesRegex(SystemExit, "identical pack identity has differing bytecode"):
                BUILDER.build_managed_shader_packs(REPO, standard, horizon, "VR", self.ABI,
                                                   depth_inputs={"off": (standard, horizon)})
            for cache in (standard, horizon):
                self.assertTrue((cache / BUILDER.MANIFEST_FILE_NAME).exists())
                self.assertFalse((cache / BUILDER.PACK_MANIFEST_FILE_NAME).exists())

    def test_second_depth_failure_does_not_pack_partial_results(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "VR/ShaderCache"
            self.loose(cache, False, False)
            first = {"standard": (cache, 4, 3, {"ReverseZ": True})}
            with mock.patch.object(BUILDER, "compile_runtime_depth", side_effect=[first, SystemExit("compile failed")]), \
                    mock.patch.object(BUILDER, "build_managed_shader_packs") as pack:
                with self.assertRaisesRegex(SystemExit, "compile failed"):
                    BUILDER.build_runtime(REPO, root, root, "VR", root / "config.yaml", "test", 1,
                                          "fxc", ("compiler",), None, lambda: None, BUILDER.SHIPPED_CACHE_PROFILE,
                                          include_horizon_fix=False, both_depth_conventions=True)
                pack.assert_not_called()
            self.assertTrue((cache / "Lighting/4019.pso").exists())

    def test_reused_standalone_depth_identity_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            standard, reverse = root / "ShaderCache", root / "ShaderCache-ReverseZ"
            self.loose(standard, False, False)
            self.loose(reverse, True, False)
            off = json.loads((standard / BUILDER.MANIFEST_FILE_NAME).read_text())
            on = json.loads((reverse / BUILDER.MANIFEST_FILE_NAME).read_text())
            off_helper, on_helper = next(iter(off["standaloneInputs"])), next(iter(on["standaloneInputs"]))
            (reverse / on_helper).unlink()
            shutil.copyfile(standard / off_helper, reverse / off_helper)
            on["entries"].pop(on_helper)
            on["entries"][off_helper] = off["entries"][off_helper]
            on["standaloneInputs"] = off["standaloneInputs"]
            (reverse / BUILDER.MANIFEST_FILE_NAME).write_text(json.dumps(on))
            with self.assertRaisesRegex(SystemExit, "unchanged standalone compile identity"):
                BUILDER.build_managed_shader_packs(REPO, standard, None, "VR", self.ABI,
                                                   depth_inputs={"off": (standard, None), "on": (reverse, None)})


if __name__ == "__main__":
    unittest.main()
