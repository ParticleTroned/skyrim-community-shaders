#!/usr/bin/env python3
"""Regression tests for the managed-cache release FOMOD."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock


REPO = Path(__file__).resolve().parent.parent
BUILDER_PATH = REPO / "tools/build-fomod-package.py"
SPEC = importlib.util.spec_from_file_location("build_fomod_package", BUILDER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load FOMOD builder: {BUILDER_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)


class FomodPackageTests(unittest.TestCase):
    SHADER_CACHE_ABI = "a" * 64

    @staticmethod
    def _write_cache(
        cache_directory: Path,
        runtime: str,
        shader_cache_abi: str,
    ) -> None:
        cache_directory.mkdir(parents=True, exist_ok=True)
        contract_runtime = "SE" if runtime == BUILDER.RUNTIME_SE_AE else "VR"
        (cache_directory / BUILDER.CACHE_INFO_FILE).write_text(
            "[Cache]\n"
            "PluginVersion = CSX 3.18-VR\n"
            f"ShaderCacheABI = {shader_cache_abi}\n",
            encoding="utf-8",
        )
        (cache_directory / BUILDER.MANIFEST_FILE).write_text(
            json.dumps({"schemaVersion": 1, "entries": {}}),
            encoding="utf-8",
        )
        (cache_directory / BUILDER.PACK_MANIFEST_FILE).write_text(
            json.dumps(
                {
                    "schema": "csx.shader-cache.pack-manifest",
                    "schemaVersion": 1,
                    "formatVersion": 1,
                    "runtime": contract_runtime,
                    "shaderCacheABI": shader_cache_abi,
                    "compatibilityVariants": ["default", "legacy-horizon-fix"],
                }
            ),
            encoding="utf-8",
        )
        for pack_name in BUILDER.PACK_FILES:
            (cache_directory / pack_name).write_bytes(b"pack")

    def _inputs(self, root: Path) -> tuple[Path, Path, Path]:
        core = root / "core"
        core.mkdir()
        (core / "core-file.txt").write_text("core", encoding="utf-8")
        build_manifest = core / BUILDER.CORE_BUILD_MANIFEST
        build_manifest.parent.mkdir(parents=True)
        build_manifest.write_text(
            json.dumps(
                {
                    "identity": {
                        "shaderCache": {"abiId": self.SHADER_CACHE_ABI}
                    }
                }
            ),
            encoding="utf-8",
        )

        se_cache = root / "se"
        vr_cache = root / "vr"
        self._write_cache(
            se_cache / BUILDER.CACHE_DIRECTORY,
            BUILDER.RUNTIME_SE_AE,
            self.SHADER_CACHE_ABI,
        )
        self._write_cache(
            vr_cache / BUILDER.CACHE_DIRECTORY,
            BUILDER.RUNTIME_VR,
            self.SHADER_CACHE_ABI,
        )
        return core, se_cache, vr_cache

    @staticmethod
    def _read_json(path: Path) -> dict:
        return json.loads(path.read_text(encoding="utf-8"))

    @staticmethod
    def _write_json(path: Path, value: dict) -> None:
        path.write_text(json.dumps(value), encoding="utf-8")

    def test_stages_one_page_two_managed_cache_fomod(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            output = root / "staged"

            BUILDER.stage_package(core, se_cache, vr_cache, output, "v3.18.0")
            BUILDER.validate_staged_package(output, "v3.18.0")

            config = ET.parse(
                output / BUILDER.FOMOD_DIRECTORY / BUILDER.MODULE_CONFIG_FILE
            ).getroot()
            steps = config.findall("./installSteps/installStep")
            self.assertEqual(
                [step.get("name") for step in steps],
                ["Choose the Skyrim runtime"],
            )

            runtime_options = steps[0].findall(
                "./optionalFileGroups/group/plugins/plugin"
            )
            self.assertEqual(
                [option.get("name") for option in runtime_options],
                ["Skyrim VR", "Skyrim SE/AE", "No prebuilt shader cache"],
            )
            self.assertEqual(
                [
                    option.findtext("./conditionFlags/flag")
                    for option in runtime_options
                ],
                [BUILDER.RUNTIME_VR, BUILDER.RUNTIME_SE_AE, BUILDER.RUNTIME_NONE],
            )

            mappings = config.findall(
                "./conditionalFileInstalls/patterns/pattern/files/folder"
            )
            self.assertEqual(len(mappings), 2)
            self.assertEqual(
                {folder.get("destination") for folder in mappings},
                {BUILDER.CACHE_DIRECTORY},
            )
            for variant in BUILDER.CACHE_VARIANTS:
                self.assertTrue(
                    (
                        output
                        / variant.staging_directory
                        / BUILDER.CACHE_DIRECTORY
                        / BUILDER.CACHE_INFO_FILE
                    ).is_file()
                )

    def test_generated_config_contains_no_automatic_detection(self) -> None:
        root = BUILDER.build_module_config().getroot()
        tags = {element.tag for element in root.iter()}
        self.assertTrue(
            {
                "gameDependency",
                "fileDependency",
                "dependencyType",
                "moduleDependencies",
            }.isdisjoint(tags)
        )
        serialized = ET.tostring(root, encoding="unicode").casefold()
        for forbidden in (
            "mo2",
            "mod organizer",
            "use_any_file",
            ".dll",
            ".marker",
            "nexus",
            "discord",
            "open shaders",
        ):
            self.assertNotIn(forbidden, serialized)

    def test_rejects_cache_with_missing_managed_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            (vr_cache / BUILDER.CACHE_DIRECTORY / BUILDER.PACK_FILES[0]).unlink()
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core,
                    se_cache,
                    vr_cache,
                    root / "staged",
                    "v3.18.0",
                )

    def test_rejects_info_abi_that_does_not_match_core(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            cache = vr_cache / BUILDER.CACHE_DIRECTORY
            info_path = cache / BUILDER.CACHE_INFO_FILE
            info_path.write_text(
                "[Cache]\n"
                "PluginVersion = CSX 3.18-VR\n"
                f"ShaderCacheABI = {'b' * 64}\n",
                encoding="utf-8",
            )
            output = root / "staged"
            with self.assertRaises(SystemExit) as caught:
                BUILDER.stage_package(
                    core,
                    se_cache,
                    vr_cache,
                    output,
                    "v3.18.0",
                )
            message = str(caught.exception)
            self.assertIn(str(cache), message)
            self.assertIn(self.SHADER_CACHE_ABI, message)
            self.assertIn("b" * 64, message)
            self.assertFalse(output.exists())

    def test_rejects_pack_manifest_abi_that_does_not_match_core(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            cache = vr_cache / BUILDER.CACHE_DIRECTORY
            manifest_path = cache / BUILDER.PACK_MANIFEST_FILE
            manifest = self._read_json(manifest_path)
            manifest["shaderCacheABI"] = "b" * 64
            self._write_json(manifest_path, manifest)
            output = root / "staged"
            with self.assertRaises(SystemExit) as caught:
                BUILDER.stage_package(
                    core, se_cache, vr_cache, output, "v3.18.0"
                )
            message = str(caught.exception)
            self.assertIn(str(manifest_path), message)
            self.assertIn(self.SHADER_CACHE_ABI, message)
            self.assertIn("b" * 64, message)
            self.assertFalse(output.exists())

    def test_rejects_runtime_cache_roots_in_the_wrong_slots(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            cases = (
                ("swapped", vr_cache, se_cache),
                ("both-se", se_cache, se_cache),
                ("both-vr", vr_cache, vr_cache),
            )
            for name, se_input, vr_input in cases:
                with self.subTest(name=name):
                    output = root / f"staged-{name}"
                    with self.assertRaises(SystemExit) as caught:
                        BUILDER.stage_package(
                            core, se_input, vr_input, output, "v3.18.0"
                        )
                    message = str(caught.exception)
                    self.assertIn("expected", message)
                    self.assertIn("observed", message)
                    self.assertFalse(output.exists())

    def test_staged_validation_rejects_wrong_runtime_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            output = root / "staged"
            BUILDER.stage_package(core, se_cache, vr_cache, output, "v3.18.0")
            manifest_path = (
                output
                / BUILDER.CACHE_VARIANTS[0].staging_directory
                / BUILDER.CACHE_DIRECTORY
                / BUILDER.PACK_MANIFEST_FILE
            )
            manifest = self._read_json(manifest_path)
            manifest["runtime"] = "SE"
            self._write_json(manifest_path, manifest)
            with self.assertRaises(SystemExit) as caught:
                BUILDER.validate_staged_package(output, "v3.18.0")
            message = str(caught.exception)
            self.assertIn(str(manifest_path), message)
            self.assertIn("expected 'VR'", message)
            self.assertIn("observed 'SE'", message)

    def test_rejects_invalid_core_manifest_before_staging(self) -> None:
        mutations = (
            ("missing", None),
            ("malformed", "{"),
            ("missing-abi", json.dumps({"identity": {}})),
            (
                "uppercase",
                json.dumps({"identity": {"shaderCache": {"abiId": "A" * 64}}}),
            ),
            (
                "short",
                json.dumps({"identity": {"shaderCache": {"abiId": "a" * 63}}}),
            ),
            (
                "non-string",
                json.dumps({"identity": {"shaderCache": {"abiId": 1}}}),
            ),
        )
        for name, contents in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                core, se_cache, vr_cache = self._inputs(root)
                manifest_path = core / BUILDER.CORE_BUILD_MANIFEST
                if contents is None:
                    manifest_path.unlink()
                else:
                    manifest_path.write_text(contents, encoding="utf-8")
                output = root / "staged"
                with self.assertRaises(SystemExit):
                    BUILDER.stage_package(
                        core, se_cache, vr_cache, output, "v3.18.0"
                    )
                self.assertFalse(output.exists())

    def test_removes_staging_tree_after_post_copy_validation_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            output = root / "staged"
            with mock.patch.object(
                BUILDER,
                "validate_staged_package",
                side_effect=SystemExit("post-copy rejection"),
            ):
                with self.assertRaisesRegex(SystemExit, "post-copy rejection"):
                    BUILDER.stage_package(
                        core, se_cache, vr_cache, output, "v3.18.0"
                    )
            self.assertFalse(output.exists())

    def test_refuses_to_replace_existing_staging_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            output = root / "staged"
            output.mkdir()
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core,
                    se_cache,
                    vr_cache,
                    output,
                    "v3.18.0",
                )

    def test_refuses_staging_inside_an_input_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core,
                    se_cache,
                    vr_cache,
                    core / "staged",
                    "v3.18.0",
                )


if __name__ == "__main__":
    unittest.main()
