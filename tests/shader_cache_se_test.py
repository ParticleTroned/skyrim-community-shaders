#!/usr/bin/env python3
"""SE inventory, exact macro state, and managed-cache AIO integration checks."""

from __future__ import annotations

import configparser
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import shader_cache_packaging_test as packaging_tests

BUILDER = packaging_tests.BUILDER
REPO = packaging_tests.REPO
from shader_cache_manifest import compile_task_defines, prepare_fxc_defines

try:
    import yaml
    from hlslkit.compile_shaders import parse_shader_configs
except ImportError:
    yaml = None
    parse_shader_configs = None


class SECacheContractTests(unittest.TestCase):
    def test_release_identity_comes_only_from_se_cmake_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            version = root / BUILDER.CSX_VERSION_FILE
            version.parent.mkdir()
            version.write_text('set(CSX_VERSION "12.345-SE")\n', encoding="utf-8")
            (root / "CMakePresets.json").write_text('{"configurePresets": []}', encoding="utf-8")
            self.assertEqual(BUILDER.default_plugin_version(root), "CSX 12.345-SE")
            self.assertEqual(BUILDER.validate_csx_plugin_version("CSX 12.345-SE"), "CSX12.345-SE")
            for invalid in ('12.345-SE', 'CSX 12.345-VR', 'CSX 12.345-SE\nUnexpected=true', 'CSX 12.345.1-SE'):
                with self.subTest(version=invalid), self.assertRaises(SystemExit):
                    BUILDER.validate_csx_plugin_version(invalid)
            version.unlink()
            with self.assertRaisesRegex(SystemExit, "cannot derive"):
                BUILDER.default_plugin_version(root)

    def test_only_se_runtime_profile_is_available(self) -> None:
        self.assertEqual(set(BUILDER.CACHE_PROFILES), {"shipped"})
        self.assertEqual(BUILDER.SHIPPED_CACHE_PROFILE.supported_runtimes, {"SE"})
        with self.assertRaises(SystemExit):
            BUILDER.validate_cache_profile(BUILDER.SHIPPED_CACHE_PROFILE, ["VR"])
        result = subprocess.run(
            [sys.executable, str(REPO / "tools/build-shader-cache.py"), "--runtime", "VR"],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid choice", result.stderr)

    def test_imagespace_indices_remain_native_se_values(self) -> None:
        self.assertTrue(all(isinstance(key, int) for key in BUILDER.IMAGESPACE_DIRS))
        self.assertEqual(BUILDER.IMAGESPACE_DIRS[154], "ISUpsampleDynamicResolution")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "ISTemporalAA"
            source.mkdir()
            (source / "9A.pso").write_bytes(b"source bytes are not changed by remapping")
            result = BUILDER.remap_imagespace_dirs(root, "SE")
            self.assertEqual(result, {"ISUpsampleDynamicResolution": "ISTemporalAA"})
            self.assertEqual((root / "ISUpsampleDynamicResolution/9A.pso").read_bytes(), b"source bytes are not changed by remapping")

    def test_resolved_macros_preserve_empty_values_and_canonical_utf8_lengths(self) -> None:
        tasks = [("RunGrass.hlsl", "PSHADER", "Grass:Pixel:10006", ["Z=", "PBR_GRASS=1", "Z=", "NAME=é", "BARE"])]
        self.assertEqual(
            compile_task_defines(tasks),
            {("RunGrass", "10006.pso"): "4:BARE1:14:NAME2:é9:PBR_GRASS1:11:Z0:"},
        )
        with self.assertRaisesRegex(ValueError, "duplicate"):
            compile_task_defines(tasks + tasks)
        for stage, descriptor in (("UNKNOWN", "1"), ("PSHADER", "not-a-descriptor"), ("PSHADER", "123456789")):
            with self.subTest(stage=stage, descriptor=descriptor), self.assertRaises(ValueError):
                compile_task_defines([("Lighting.hlsl", stage, descriptor, [])])
        config = {"shaders": [{"configs": {"PSHADER": {
            "common_defines": ["PBR_GRASS=1", "WETTERNESS"],
            "entries": [{"defines": ["HDR_OUTPUT", ["VANILLA_FRESNEL", "OPTION=2"]]}],
        }}}]}
        prepare_fxc_defines(config)
        stage = config["shaders"][0]["configs"]["PSHADER"]
        self.assertEqual(stage["common_defines"], ["PBR_GRASS=1", "WETTERNESS="])
        self.assertEqual(stage["entries"][0]["defines"], ["HDR_OUTPUT=", ["VANILLA_FRESNEL=", "OPTION=2"]])

    def test_manifest_global_state_is_flat_and_uses_sorted_enabled_feature_abis(self) -> None:
        states = []
        def record(*args, **kwargs):
            states.append(args[2])
            self.assertEqual(kwargs["compile_tasks"], [])
            return 0
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Info.ini").write_text(
                "[Cache]\n[ZFeature]\nEnabled=true\nShaderCacheABI=2\n"
                "[Disabled]\nEnabled=false\nShaderCacheABI=3\n"
                "[HorizonFix]\nEnabled=true\n"
                "[AFeature]\nEnabled=true\nShaderCacheABI=1\n", encoding="utf-8",
            )
            BUILDER.write_shader_cache_manifest(root, root, "SE", {}, record, "a" * 64, [])
        self.assertEqual(states, [f"ShaderCacheABI={'a' * 64};FeatureShaderABI=AFeature:1;FeatureShaderABI=ZFeature:2;"])

    @unittest.skipUnless(yaml is not None and parse_shader_configs is not None, "pinned hlslkit and PyYAML are required")
    def test_shipped_inventory_keeps_hdr_fresnel_and_excludes_retired_wetness(self) -> None:
        config_path = REPO / ".github/configs/shader-validation.yaml"
        captured = yaml.safe_load(config_path.read_text(encoding="utf-8"))
        BUILDER.validate_captured_variant_count(captured, config_path)
        original_tasks = parse_shader_configs(str(config_path))
        self.assertEqual(len(original_tasks), captured["captured_shader_variants"])
        original_keys = {task[:3] for task in original_tasks}
        distribution = BUILDER.derive_distribution_profile(REPO)
        with tempfile.TemporaryDirectory() as temporary:
            path = BUILDER.filter_profile_defines(
                config_path, Path(temporary) / "se.yaml", yaml, BUILDER.SHIPPED_CACHE_PROFILE,
                feature_states={"ReverseZ": True},
                additional_excluded_defines=distribution.excluded_defines | {distribution.horizon_fix_define},
                add_cross_modlist_variants=True, source_root=REPO,
            )
            tasks = parse_shader_configs(str(path))
        self.assertEqual(len(tasks), len(compile_task_defines(tasks)))
        by_key = {task[:3]: task[3] for task in tasks}
        self.assertTrue(original_keys <= by_key.keys())
        preserved = {"HDR_OUTPUT", "VANILLA_FRESNEL"}
        observed = set()
        for source, stage, entry, defines in original_tasks:
            expected = {BUILDER.normalized_define_name(value) for value in defines} & preserved
            actual = {BUILDER.normalized_define_name(value) for value in by_key[(source, stage, entry)]}
            self.assertTrue(expected <= actual, (source, stage, entry, expected - actual))
            observed |= expected
        self.assertEqual(observed, preserved)
        for source, stage, entry, defines in tasks:
            names = {BUILDER.normalized_define_name(value) for value in defines}
            self.assertNotIn("VR", names)
            self.assertNotIn("WETNESS_EFFECTS", names)
            self.assertIn("REVERSE_Z=1" if source == "Utility.hlsl" else "REVERSE_Z=", defines)
            if source == "RunGrass.hlsl":
                self.assertIn("PBR_GRASS=1", defines)
                self.assertIn("GRASS_OPTIMIZATIONS=", defines)
                self.assertIn("WETTERNESS=", defines)
        self.assertIn(("RunGrass.hlsl", "VSHADER", "Grass:Vertex:0"), by_key)
        self.assertIn(("RunGrass.hlsl", "PSHADER", "Grass:Pixel:10006"), by_key)

    @unittest.skipUnless(yaml is not None and parse_shader_configs is not None, "pinned hlslkit and PyYAML are required")
    def test_disabled_reverse_z_removes_captured_depth_defines(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "captured.yaml"
            captured = yaml.safe_load((REPO / ".github/configs/shader-validation.yaml").read_text(encoding="utf-8"))
            captured["common_defines"].append("REVERSE_Z=1")
            source.write_text(yaml.safe_dump(captured), encoding="utf-8")
            filtered = BUILDER.filter_profile_defines(
                source, root / "standard.yaml", yaml, BUILDER.SHIPPED_CACHE_PROFILE,
                feature_states={"ReverseZ": False}, source_root=REPO,
            )
            tasks = parse_shader_configs(str(filtered))
            self.assertTrue(tasks)
            for task in tasks:
                self.assertNotIn("REVERSE_Z", {BUILDER.normalized_define_name(value) for value in task[3]})

    @unittest.skipUnless(parse_shader_configs is not None, "pinned hlslkit is required")
    def test_fxc_adapter_preserves_upstream_validation_and_empty_definitions(self) -> None:
        import shader_cache_compile as adapter
        with mock.patch.object(adapter, "_compile_shader", return_value="compiled") as compile_shader:
            self.assertEqual(adapter.compile_shader("fxc", "shader", "PSHADER", "entry", ["HDR_OUTPUT=", "PBR_GRASS=1"], "tail"), "compiled")
            self.assertEqual(compile_shader.call_args.args[4], ["HDR_OUTPUT= ", "PBR_GRASS=1"])
            self.assertEqual(compile_shader.call_args.args[5], "tail")
        with mock.patch.object(adapter, "_validate_shader_inputs", return_value="validated") as validate:
            self.assertEqual(adapter.validate_shader_inputs("fxc", "shader", "output", ["HDR_OUTPUT= ", "PBR_GRASS=1"], "root"), "validated")
            self.assertEqual(validate.call_args.args[3], ["HDR_OUTPUT", "PBR_GRASS=1"])


@unittest.skipUnless(shutil.which("cmake"), "CMake is required to validate actual AIO archives")
class SECacheAIOTests(unittest.TestCase):
    @staticmethod
    def prepare_fixture(root: Path, *, horizon: bool = True) -> tuple[Path, Path, str]:
        version = BUILDER.default_plugin_version(REPO)
        tag = BUILDER.validate_csx_plugin_version(version)
        core = root / "core"
        files = {
            "SKSE/Plugins/CommunityShaders.dll": b"SE plugin fixture",
            f"SKSE/Plugins/CommunityShaders/{tag}.marker": b"",
            "SKSE/Plugins/CommunityShaders/FrameGeneration/sl.dlss_g.dll": b"SE frame generation payload",
            "SKSE/Plugins/CommunityShaders/HDR/HDR.ini": b"SE HDR settings",
            "Shaders/ISTemporalAA.hlsl": b"HDR_OUTPUT and flat presentation shader fixture",
            "Shaders/Features/HorizonFix.ini": b"[Info]\nVersion = test-horizon\n",
            "Shaders/Features/LightLimitFix.ini": b"[Info]\nVersion = test-lights\n",
            "license/readme.txt": b"core documentation",
        }
        for relative, content in files.items():
            path = core / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        cache_root = root / "cache"
        cache = packaging_tests.ShaderCachePackagingTests._managed_cache(cache_root, "SE", horizon=horizon)
        abi = BUILDER.sha256_bytes(BUILDER.canonical_bytes(BUILDER.shader_contract_identity(REPO, BUILDER.DEFAULT_SHADER_CONTRACT_FILES)))
        info = (
            f"[Cache]\nPluginVersion={version}\nShaderCacheABI={abi}\n"
            "[HorizonFix]\nEnabled=false\nVersion=test-horizon\n"
            "[LightLimitFix]\nEnabled=true\nVersion=test-lights\nShaderCacheABI=1\n"
        )
        (cache / "Info.ini").write_text(info, encoding="utf-8")
        manifest_path = cache / BUILDER.PACK_MANIFEST_FILE_NAME
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["shaderCacheABI"] = abi
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return core, cache_root, version

    def test_aio_rejects_cache_with_runtime_disabled_wetness(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, cache_root, version = self.prepare_fixture(root)
            (core / "Shaders/Features/WetnessEffects.ini").write_text(
                "[Info]\nVersion=test-wetness\n", encoding="utf-8",
            )
            info_path = cache_root / "ShaderCache/Info.ini"
            baseline = info_path.read_text(encoding="utf-8")
            for enabled in ("true", "invalid"):
                with self.subTest(enabled=enabled):
                    info_path.write_text(baseline + f"[WetnessEffects]\nEnabled={enabled}\n", encoding="utf-8")
                    before = BUILDER.ordinary_tree_files(cache_root)
                    with self.assertRaisesRegex(SystemExit, "WetnessEffects"):
                        BUILDER.validate_aio_inputs(core, cache_root, version, BUILDER.validate_csx_plugin_version(version))
                    self.assertEqual(BUILDER.ordinary_tree_files(cache_root), before)
            info_path.write_text(
                baseline + "[WetnessEffects]\nEnabled=false\nVersion=test-wetness\n", encoding="utf-8",
            )
            BUILDER.validate_aio_inputs(core, cache_root, version, BUILDER.validate_csx_plugin_version(version))

    def test_aio_archive_preserves_complete_core_and_optional_cache_mapping(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, cache, version = self.prepare_fixture(root)
            original_core = BUILDER.ordinary_tree_files(core)
            archive = BUILDER.prepare_aio_archive(
                core, cache, root, "SE", "release label", version,
                BUILDER.validate_csx_plugin_version(version), shutil.which("cmake"),
            )
            self.assertEqual(archive.name, "CSX_AIO-release-label.7z")
            self.assertEqual(BUILDER.ordinary_tree_files(core), original_core)
            extracted = root / "extracted"
            extracted.mkdir()
            subprocess.run([shutil.which("cmake"), "-E", "tar", "xf", str(archive)], cwd=extracted, check=True, capture_output=True)
            self.assertEqual(BUILDER.ordinary_tree_files(extracted / BUILDER.AIO_CORE_DIRECTORY), original_core)
            BUILDER.validate_fomod_installer(extracted / "fomod")
            self.assertEqual(set(BUILDER.ordinary_tree_files(extracted / BUILDER.AIO_CACHE_DIRECTORY)), {
                *BUILDER.PACK_FILE_NAMES, "Info.ini", "PackManifest.json",
            })

    def test_release_workflow_cli_can_assemble_without_shader_compiler(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, cache, _ = self.prepare_fixture(root)
            out = root / "combined"
            result = subprocess.run([
                sys.executable, str(REPO / "tools/build-shader-cache.py"),
                "--assemble-aio", str(cache), "--aio-root", str(core),
                "--out", str(out), "--package-label", "ci-test",
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            archive = out / "CSX_AIO-ci-test.7z"
            self.assertTrue(archive.is_file())
            self.assertGreater(archive.stat().st_size, 0)

    def test_aio_rejects_mismatched_feature_version_abi_and_partial_cache(self) -> None:
        for case in ("feature-version", "source-abi", "missing-pack", "extra-cache-tree"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                core, cache_root, version = self.prepare_fixture(root)
                cache = cache_root / "ShaderCache"
                if case == "feature-version":
                    path = core / "Shaders/Features/LightLimitFix.ini"
                    path.write_text("[Info]\nVersion=different\n", encoding="utf-8")
                elif case == "source-abi":
                    path = cache / "Info.ini"
                    text = path.read_text(encoding="utf-8")
                    lines = text.splitlines()
                    lines[2] = "ShaderCacheABI=" + "b" * 64
                    path.write_text("\n".join(lines), encoding="utf-8")
                elif case == "missing-pack":
                    (cache / "Developer.B.csxpack").unlink()
                else:
                    (cache_root / "ShaderCache-HorizonFix").mkdir()
                with self.assertRaises(SystemExit):
                    BUILDER.validate_aio_inputs(core, cache_root, version, BUILDER.validate_csx_plugin_version(version))

    def test_aio_rejects_linked_payload_without_changing_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, cache, version = self.prepare_fixture(root)
            outside = root / "outside.txt"
            outside.write_bytes(b"preserved")
            link = core / "linked.txt"
            try:
                link.symlink_to(outside)
            except OSError as exc:
                self.skipTest(f"symlinks unavailable: {exc}")
            with self.assertRaisesRegex(SystemExit, "linked"):
                BUILDER.validate_aio_inputs(core, cache, version, BUILDER.validate_csx_plugin_version(version))
            self.assertEqual(outside.read_bytes(), b"preserved")

    def test_standard_only_aio_requires_explicit_option(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, cache, version = self.prepare_fixture(root, horizon=False)
            tag = BUILDER.validate_csx_plugin_version(version)
            with self.assertRaisesRegex(SystemExit, "missing required compatibility variants"):
                BUILDER.validate_aio_inputs(core, cache, version, tag)
            BUILDER.validate_aio_inputs(core, cache, version, tag, include_horizon_fix=False)


if __name__ == "__main__":
    unittest.main()
