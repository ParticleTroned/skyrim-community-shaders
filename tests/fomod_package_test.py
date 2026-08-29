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


REPO = Path(__file__).resolve().parent.parent
BUILDER_PATH = REPO / "tools/build-fomod-package.py"
SPEC = importlib.util.spec_from_file_location("build_fomod_package", BUILDER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load FOMOD builder: {BUILDER_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)


class FomodPackageTests(unittest.TestCase):
    @staticmethod
    def _write_cache(cache_directory: Path, runtime: str) -> None:
        cache_directory.mkdir(parents=True, exist_ok=True)
        contract_runtime = "SE" if runtime == BUILDER.RUNTIME_SE_AE else "VR"
        pack_set_id = "0123456789abcdef0123456789abcdef"
        (cache_directory / BUILDER.CACHE_INFO_FILE).write_text(
            "[Cache]\n"
            f"PluginVersion = CSX 3.18-{contract_runtime}\n"
            "ShaderCacheABI = test\n",
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
                    "schemaVersion": 2,
                    "formatVersion": 1,
                    "hashAlgorithm": "sha256",
                    "packSetId": pack_set_id,
                    "runtime": contract_runtime,
                    "shaderCacheABI": "test",
                    "optimizedRecordCount": 0,
                    "developerRecordCount": 0,
                    "compatibilityVariants": ["default", "legacy-horizon-fix"],
                    "files": {
                        "Optimized.A.csxpack": {
                            "lane": 1,
                            "generation": 1,
                            "recordCount": 0,
                        },
                        "Optimized.B.csxpack": {
                            "lane": 1,
                            "generation": 0,
                            "recordCount": 0,
                        },
                        "Developer.A.csxpack": {
                            "lane": 2,
                            "generation": 1,
                            "recordCount": 0,
                        },
                        "Developer.B.csxpack": {
                            "lane": 2,
                            "generation": 0,
                            "recordCount": 0,
                        },
                    },
                }
            ),
            encoding="utf-8",
        )
        for pack_name in BUILDER.PACK_FILES:
            BUILDER.SHADER_CACHE_CONTRACT.write_shader_pack(
                cache_directory / pack_name,
                BUILDER.PACK_LANES[pack_name],
                1 if ".A." in pack_name else 0,
                [],
                pack_set_id,
            )

    def _inputs(self, root: Path) -> tuple[Path, Path, Path]:
        core = root / "core"
        core.mkdir()
        (core / "core-file.txt").write_text("core", encoding="utf-8")

        se_cache = root / "se"
        vr_cache = root / "vr"
        self._write_cache(
            se_cache / BUILDER.CACHE_DIRECTORY, BUILDER.RUNTIME_SE_AE
        )
        self._write_cache(vr_cache / BUILDER.CACHE_DIRECTORY, BUILDER.RUNTIME_VR)
        return core, se_cache, vr_cache

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

    def test_rejects_pack_with_only_a_plausible_filename(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            (vr_cache / BUILDER.CACHE_DIRECTORY / BUILDER.PACK_FILES[0]).write_bytes(
                b"pack"
            )
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core, se_cache, vr_cache, root / "staged", "v3.18.0"
                )

    def test_rejects_pack_manifest_for_another_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            manifest_path = (
                vr_cache / BUILDER.CACHE_DIRECTORY / BUILDER.PACK_MANIFEST_FILE
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["runtime"] = "SE"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core, se_cache, vr_cache, root / "staged", "v3.18.0"
                )

    def test_rejects_manifest_count_not_present_in_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            manifest_path = (
                vr_cache / BUILDER.CACHE_DIRECTORY / BUILDER.PACK_MANIFEST_FILE
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["optimizedRecordCount"] = 1
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core, se_cache, vr_cache, root / "staged", "v3.18.0"
                )

    def test_rejects_pack_set_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core, se_cache, vr_cache = self._inputs(root)
            pack_path = vr_cache / BUILDER.CACHE_DIRECTORY / BUILDER.PACK_FILES[0]
            BUILDER.SHADER_CACHE_CONTRACT.write_shader_pack(
                pack_path,
                BUILDER.PACK_LANES[BUILDER.PACK_FILES[0]],
                1,
                [],
                "fedcba9876543210fedcba9876543210",
            )
            with self.assertRaises(SystemExit):
                BUILDER.stage_package(
                    core, se_cache, vr_cache, root / "staged", "v3.18.0"
                )

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
