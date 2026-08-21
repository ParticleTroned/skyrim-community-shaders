#!/usr/bin/env python3
"""Regression tests for version-derived shader-cache FOMOD metadata."""

from __future__ import annotations

import configparser
import copy
import importlib.util
import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
BUILDER_PATH = REPO / "tools/build-shader-cache.py"
SPEC = importlib.util.spec_from_file_location("build_shader_cache", BUILDER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load shader-cache builder: {BUILDER_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)


class ShaderCachePackagingTests(unittest.TestCase):
    @staticmethod
    def _sample_shader_config() -> dict[str, object]:
        profile_defines = [
            "CLOUD_SHADOWS",
            "CS_EDITOR",
            "CS_HAIR",
            "D3DCOMPILE_DEBUG",
            "D3DCOMPILE_SKIP_OPTIMIZATION",
            "EXTENDED_TRANSLUCENCY",
            "GRASS_COLLISION",
            "HORIZON_FIX",
            "TERRAIN_BLENDING",
            "VOLUMETRIC_SHADOWS",
            "WETNESS_EFFECTS",
        ]
        return {
            "common_defines": ["VR", "WETTERNESS", *profile_defines],
            "file_common_defines": {
                "Lighting.hlsl": {
                    "PSHADER": ["LIGHT_LIMIT_FIX", *profile_defines],
                },
                "Water.hlsl": {
                    "PSHADER": ["WATER_EFFECTS", *profile_defines],
                },
            },
            "shaders": [
                {
                    "file": "Lighting.hlsl",
                    "configs": {
                        "PSHADER": {
                            "common_defines": [
                                "LIGHT_LIMIT_FIX",
                                *profile_defines,
                            ],
                        },
                    },
                },
                {
                    "file": "Water.hlsl",
                    "configs": {
                        "PSHADER": {
                            "common_defines": [
                                "WATER_EFFECTS",
                                *profile_defines,
                            ],
                        },
                    },
                },
            ],
        }

    @staticmethod
    def _all_define_names(node: object) -> set[str]:
        names: set[str] = set()
        if isinstance(node, dict):
            for value in node.values():
                names.update(ShaderCachePackagingTests._all_define_names(value))
        elif isinstance(node, list):
            for value in node:
                if isinstance(value, str):
                    names.add(BUILDER.normalized_define_name(value))
                else:
                    names.update(ShaderCachePackagingTests._all_define_names(value))
        return names

    def test_shipped_profile_remains_the_default_contract(self) -> None:
        self.assertIs(
            BUILDER.CACHE_PROFILES["shipped"],
            BUILDER.SHIPPED_CACHE_PROFILE,
        )
        self.assertEqual(
            BUILDER.default_package_label(
                BUILDER.SHIPPED_CACHE_PROFILE,
                "CSX 12.345-VR",
            ),
            "CSX 12.345-VR",
        )

        config = BUILDER.apply_cache_profile_defines(
            copy.deepcopy(self._sample_shader_config()),
            BUILDER.SHIPPED_CACHE_PROFILE,
        )
        names = self._all_define_names(config)
        self.assertIn("UNIFIED_WATER", names)
        self.assertIn("WETTERNESS", names)
        self.assertNotIn("WETNESS_EFFECTS", names)
        self.assertNotIn("D3DCOMPILE_DEBUG", names)
        self.assertNotIn("D3DCOMPILE_SKIP_OPTIMIZATION", names)

        for shader in config["shaders"]:
            self.assertIn(
                "WETTERNESS",
                shader["configs"]["PSHADER"]["common_defines"],
            )

    def test_patka_profile_removes_disabled_feature_defines(self) -> None:
        self.assertEqual(
            BUILDER.PATKA_DISABLED_FEATURES,
            frozenset(
                {
                    "CloudShadows",
                    "CSEditor",
                    "ExtendedTranslucency",
                    "GrassCollision",
                    "HairSpecular",
                    "HorizonFix",
                    "LinearLighting",
                    "PerformanceOverlay",
                    "RenderDoc",
                    "Screenshot",
                    "TerrainBlending",
                    "VolumetricShadows",
                    "WeatherPicker",
                    "Wetterness",
                }
            ),
        )
        self.assertEqual(
            BUILDER.PATKA_EXCLUDED_DEFINES,
            frozenset(
                {
                    "CLOUD_SHADOWS",
                    "CS_EDITOR",
                    "CS_HAIR",
                    "EXTENDED_TRANSLUCENCY",
                    "GRASS_COLLISION",
                    "HORIZON_FIX",
                    "TERRAIN_BLENDING",
                    "VOLUMETRIC_SHADOWS",
                    "WETTERNESS",
                }
            ),
        )
        config = BUILDER.apply_cache_profile_defines(
            copy.deepcopy(self._sample_shader_config()),
            BUILDER.PATKA_CACHE_PROFILE,
        )
        names = self._all_define_names(config)
        self.assertIn("VR", names)
        self.assertIn("UNIFIED_WATER", names)
        self.assertIn("LIGHT_LIMIT_FIX", names)
        self.assertIn("WATER_EFFECTS", names)
        self.assertTrue(BUILDER.PATKA_EXCLUDED_DEFINES.isdisjoint(names))
        self.assertTrue(BUILDER.DEBUG_PROFILE_DEFINES.isdisjoint(names))
        self.assertTrue(BUILDER.NON_SHIPPED_DEFINES.isdisjoint(names))

    def test_patka_profile_writes_matching_feature_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache_dir = root / "ShaderCache"
            features_dir = root / "stage" / "Features"
            cache_dir.mkdir()
            features_dir.mkdir(parents=True)
            feature_names = sorted(
                BUILDER.PATKA_DISABLED_FEATURES | {"CSUtility"}
            )
            for feature_name in feature_names:
                (features_dir / f"{feature_name}.ini").write_text(
                    "[Info]\nVersion = 1-2-3\n",
                    encoding="utf-8",
                )

            BUILDER.write_info_ini(
                cache_dir,
                root / "stage",
                "CSX 12.345-VR",
                "VR",
                BUILDER.PATKA_CACHE_PROFILE,
                "test-shader-abi",
            )
            config = configparser.ConfigParser(interpolation=None)
            with (cache_dir / BUILDER.INFO_FILE_NAME).open(
                "r",
                encoding="utf-8-sig",
            ) as stream:
                config.read_file(stream)

            self.assertEqual(config.get("Cache", "PluginVersion"), "CSX 12.345-VR")
            self.assertTrue(config.getboolean("CSUtility", "Enabled"))
            for feature_name in BUILDER.PATKA_DISABLED_FEATURES:
                with self.subTest(feature=feature_name):
                    self.assertFalse(config.getboolean(feature_name, "Enabled"))

    def test_patka_profile_is_vr_only_and_has_a_distinct_label(self) -> None:
        BUILDER.validate_cache_profile(BUILDER.PATKA_CACHE_PROFILE, ["VR"])
        for runtimes in (["SE"], ["SE", "VR"]):
            with self.subTest(runtimes=runtimes):
                with self.assertRaises(SystemExit):
                    BUILDER.validate_cache_profile(
                        BUILDER.PATKA_CACHE_PROFILE,
                        runtimes,
                    )
        self.assertEqual(
            BUILDER.default_package_label(
                BUILDER.PATKA_CACHE_PROFILE,
                "CSX 12.345-VR",
            ),
            "CSX 12.345-VR-Patka",
        )

    def test_profile_validation_rejects_conflicting_defines(self) -> None:
        conflict = BUILDER.CacheProfile(
            name="conflict",
            display_name="Conflict",
            supported_runtimes=frozenset({"VR"}),
            disabled_features=frozenset(),
            excluded_defines=frozenset({"CONFLICT"}),
            global_defines=("CONFLICT",),
            file_defines={},
        )
        with self.assertRaises(SystemExit):
            BUILDER.validate_cache_profile(conflict, ["VR"])

    def test_info_metadata_rejects_missing_profile_features(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache_dir = root / "ShaderCache"
            features_dir = root / "stage" / "Features"
            cache_dir.mkdir()
            features_dir.mkdir(parents=True)
            (features_dir / "CSUtility.ini").write_text(
                "[Info]\nVersion = 1-2-3\n",
                encoding="utf-8",
            )
            with self.assertRaises(SystemExit):
                BUILDER.write_info_ini(
                    cache_dir,
                    root / "stage",
                    "CSX 12.345-VR",
                    "VR",
                    BUILDER.PATKA_CACHE_PROFILE,
                    "test-shader-abi",
                )

    def test_both_caches_default_to_the_release_core_identity(self) -> None:
        presets = {
            "configurePresets": [
                {
                    "name": "ALL",
                    "cacheVariables": {"CSX_VERSION": "12.345-VR"},
                },
                {
                    "name": "AIO-Release",
                    "cacheVariables": {"CSX_VERSION": "4.0-SE"},
                },
            ]
        }
        with tempfile.TemporaryDirectory() as temporary:
            source_root = Path(temporary)
            (source_root / "CMakePresets.json").write_text(
                json.dumps(presets),
                encoding="utf-8",
            )
            self.assertEqual(
                BUILDER.default_plugin_version(source_root, "SE"),
                "CSX 12.345-VR",
            )
            self.assertEqual(
                BUILDER.default_plugin_version(source_root, "VR"),
                "CSX 12.345-VR",
            )

    def test_compatibility_tag_is_derived_for_future_versions(self) -> None:
        cases = {
            "CSX 3.18-VR": "CSX3.18-VR",
            "CSX 4.0-SE": "CSX4.0-SE",
            "CSX 12.345-VR": "CSX12.345-VR",
        }
        for plugin_version, expected in cases.items():
            with self.subTest(plugin_version=plugin_version):
                self.assertEqual(
                    BUILDER.csx_compatibility_tag(plugin_version),
                    expected,
                )

    def test_compatibility_tag_rejects_noncanonical_labels(self) -> None:
        invalid = (
            "3.18-VR",
            "CSX3.18-VR",
            "CSX 3.18.0-VR",
            "CSX 3.18-AE",
            "CSX 3.18-vr",
            "CSX 3.18-VR ",
        )
        for plugin_version in invalid:
            with self.subTest(plugin_version=plugin_version):
                with self.assertRaises(SystemExit):
                    BUILDER.csx_compatibility_tag(plugin_version)

    def test_fomod_has_exact_version_gate_and_cache_mapping(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime_root = Path(temporary)
            fomod_dir = BUILDER.write_fomod_installer(
                runtime_root,
                "VR",
                "future-test",
                "CSX 12.345-VR",
            )
            root = ET.parse(fomod_dir / BUILDER.FOMOD_CONFIG_FILE_NAME).getroot()
            dependencies = root.findall("./moduleDependencies/fileDependency")
            self.assertEqual(
                {
                    (dependency.get("file"), dependency.get("state"))
                    for dependency in dependencies
                },
                {
                    ("SKSE/Plugins/CommunityShaders.dll", "Active"),
                    (
                        "SKSE/Plugins/CommunityShaders/CSX12.345-VR.marker",
                        "Active",
                    ),
                },
            )
            BUILDER.validate_fomod_installer(fomod_dir, "CSX12.345-VR")

    def test_fomod_validation_rejects_weakened_contracts(self) -> None:
        mutations = (
            ("operator", "Or"),
            ("marker_state", "Inactive"),
            ("marker_name", "CSX3.18-VR.marker"),
        )
        for mutation, value in mutations:
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    runtime_root = Path(temporary)
                    fomod_dir = BUILDER.write_fomod_installer(
                        runtime_root,
                        "VR",
                        "future-test",
                        "CSX 12.345-VR",
                    )
                    config_path = fomod_dir / BUILDER.FOMOD_CONFIG_FILE_NAME
                    tree = ET.parse(config_path)
                    dependencies = tree.getroot().find("./moduleDependencies")
                    self.assertIsNotNone(dependencies)
                    marker = dependencies.findall("./fileDependency")[1]
                    if mutation == "operator":
                        dependencies.set("operator", value)
                    elif mutation == "marker_state":
                        marker.set("state", value)
                    else:
                        marker.set(
                            "file",
                            f"SKSE/Plugins/CommunityShaders/{value}",
                        )
                    tree.write(config_path, encoding="utf-8", xml_declaration=True)
                    with self.assertRaises(SystemExit):
                        BUILDER.validate_fomod_installer(
                            fomod_dir,
                            "CSX12.345-VR",
                        )

    def test_fomod_validation_rejects_invalid_structure(self) -> None:
        mutations = ("wrong_root", "wrong_order", "invalid_info")
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    runtime_root = Path(temporary)
                    fomod_dir = BUILDER.write_fomod_installer(
                        runtime_root,
                        "VR",
                        "future-test",
                        "CSX 12.345-VR",
                    )
                    config_path = fomod_dir / BUILDER.FOMOD_CONFIG_FILE_NAME
                    if mutation == "invalid_info":
                        (fomod_dir / BUILDER.FOMOD_INFO_FILE_NAME).write_text(
                            "<not-fomod />",
                            encoding="utf-8",
                        )
                    else:
                        tree = ET.parse(config_path)
                        root = tree.getroot()
                        if mutation == "wrong_root":
                            root.tag = "not-config"
                        else:
                            root[:] = [root[0], root[2], root[1]]
                        tree.write(
                            config_path,
                            encoding="utf-8",
                            xml_declaration=True,
                        )
                    with self.assertRaises(SystemExit):
                        BUILDER.validate_fomod_installer(
                            fomod_dir,
                            "CSX12.345-VR",
                        )


if __name__ == "__main__":
    unittest.main()
