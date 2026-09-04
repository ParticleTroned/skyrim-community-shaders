#!/usr/bin/env python3
"""Regression tests for shader-cache generation and raw packaging."""

from __future__ import annotations

import configparser
import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


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

    def test_publication_replace_retries_transient_lock(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staging = root / ".VR.publishing"
            destination = root / "VR"
            staging.mkdir()
            (staging / "cache.pso").write_bytes(b"cache")
            original_replace = Path.replace
            attempts = 0

            def transient_replace(path: Path, target: Path) -> Path:
                nonlocal attempts
                attempts += 1
                if attempts < 3:
                    raise PermissionError("simulated scanner lock")
                return original_replace(path, target)

            with (
                mock.patch.object(Path, "replace", new=transient_replace),
                mock.patch.object(BUILDER.time, "sleep") as sleep,
            ):
                BUILDER.replace_publication_staging(staging, destination)

            self.assertEqual(attempts, 3)
            self.assertEqual(sleep.call_count, 2)
            self.assertFalse(staging.exists())
            self.assertEqual((destination / "cache.pso").read_bytes(), b"cache")

    def test_publication_replace_preserves_staging_after_retry_limit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staging = root / ".VR.publishing"
            destination = root / "VR"
            staging.mkdir()

            with (
                mock.patch.object(
                    Path,
                    "replace",
                    side_effect=PermissionError("simulated persistent lock"),
                ),
                mock.patch.object(BUILDER.time, "sleep") as sleep,
                self.assertRaises(PermissionError),
            ):
                BUILDER.replace_publication_staging(staging, destination)

            self.assertEqual(
                sleep.call_count,
                BUILDER.PUBLICATION_REPLACE_ATTEMPTS - 1,
            )
            self.assertTrue(staging.is_dir())
            self.assertFalse(destination.exists())

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

    def test_horizon_variants_layer_onto_the_shipped_profile(self) -> None:
        standard_config = BUILDER.apply_cache_profile_defines(
            copy.deepcopy(self._sample_shader_config()),
            BUILDER.SHIPPED_CACHE_PROFILE,
            additional_excluded_defines=frozenset({"HORIZON_FIX"}),
            excluded_define_exceptions=frozenset({"WETNESS_EFFECTS"}),
        )
        standard_names = self._all_define_names(standard_config)
        self.assertIn("WETNESS_EFFECTS", standard_names)
        self.assertNotIn("HORIZON_FIX", standard_names)

        horizon_config = BUILDER.apply_cache_profile_defines(
            copy.deepcopy(self._sample_shader_config()),
            BUILDER.SHIPPED_CACHE_PROFILE,
            additional_excluded_defines=frozenset({"HORIZON_FIX"}),
            excluded_define_exceptions=frozenset({"WETNESS_EFFECTS"}),
            additional_file_defines={"Water.hlsl": ("HORIZON_FIX",)},
        )
        shaders = {
            shader["file"]: shader
            for shader in horizon_config["shaders"]
        }
        self.assertNotIn(
            "HORIZON_FIX",
            shaders["Lighting.hlsl"]["configs"]["PSHADER"]["common_defines"],
        )
        self.assertIn(
            "HORIZON_FIX",
            shaders["Water.hlsl"]["configs"]["PSHADER"]["common_defines"],
        )

        self.assertEqual(
            BUILDER.cache_variants_for(BUILDER.SHIPPED_CACHE_PROFILE),
            (BUILDER.STANDARD_CACHE_VARIANT,),
        )
        self.assertEqual(
            BUILDER.compile_variants_for(BUILDER.SHIPPED_CACHE_PROFILE),
            BUILDER.CACHE_VARIANTS,
        )
        self.assertEqual(
            BUILDER.CACHE_VARIANTS[0],
            BUILDER.STANDARD_CACHE_VARIANT,
        )
        self.assertEqual(
            BUILDER.cache_variants_for(BUILDER.PATKA_CACHE_PROFILE),
            (BUILDER.STANDARD_CACHE_VARIANT,),
        )

    def test_se_cross_modlist_overlay_adds_only_known_rungrass_variants(self) -> None:
        config = {
            "shaders": [
                {
                    "file": "RunGrass.hlsl",
                    "configs": {
                        "PSHADER": {"entries": []},
                        "VSHADER": {"entries": []},
                    },
                }
            ]
        }
        BUILDER.append_cross_modlist_variants(config)
        stages = config["shaders"][0]["configs"]
        self.assertEqual(
            {
                entry["entry"]: tuple(entry["defines"])
                for entry in stages["PSHADER"]["entries"]
            },
            {
                "Grass:Pixel:1": (),
                "Grass:Pixel:10006": ("DO_ALPHA_TEST",),
            },
        )
        self.assertEqual(
            {
                entry["entry"]: tuple(entry["defines"])
                for entry in stages["VSHADER"]["entries"]
            },
            {
                "Grass:Vertex:5": (),
                "Grass:Vertex:7": (),
            },
        )

    def test_vr_horizon_variants_write_opposite_feature_states(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            features_dir = root / "stage" / "Features"
            features_dir.mkdir(parents=True)
            for feature_name in ("CSUtility", "HorizonFix"):
                (features_dir / f"{feature_name}.ini").write_text(
                    "[Info]\nVersion = 1-2-3\n",
                    encoding="utf-8",
                )

            for enabled in (False, True):
                cache_dir = root / f"cache-{enabled}"
                cache_dir.mkdir()
                BUILDER.write_info_ini(
                    cache_dir,
                    root / "stage",
                    "CSX 12.345-VR",
                    "VR",
                    BUILDER.SHIPPED_CACHE_PROFILE,
                    "test-shader-abi",
                    enabled_overrides={"HorizonFix": enabled},
                )
                states = BUILDER.read_feature_states(cache_dir)
                self.assertIs(states["HorizonFix"], enabled)
                self.assertTrue(states["CSUtility"])

    def test_horizon_variant_delta_rejects_malformed_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            standard_cache = root / "standard"
            horizon_cache = root / "horizon"
            for cache_dir, blob in (
                (standard_cache, b"standard"),
                (horizon_cache, b"horizon"),
            ):
                water_dir = cache_dir / "Water"
                water_dir.mkdir(parents=True)
                (water_dir / "variant.pso").write_bytes(blob)
                (cache_dir / BUILDER.MANIFEST_FILE_NAME).write_text(
                    "{\"schemaVersion\": 1, \"entries\": []}",
                    encoding="utf-8",
                )

            with self.assertRaises(SystemExit):
                BUILDER.validate_horizon_variant_delta(
                    standard_cache,
                    horizon_cache,
                    {"HorizonFix": False},
                    {"HorizonFix": True},
                    runtime="VR",
                )

    def test_se_distribution_profile_derives_horizon_contract(self) -> None:
        profile = BUILDER.derive_distribution_profile(REPO)
        self.assertEqual(profile.horizon_fix_define, "HORIZON_FIX")
        self.assertNotIn("HorizonFix", profile.excluded_short_names)

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

    def test_runtime_remains_in_each_record_compile_state(self) -> None:
        states: list[str] = []

        def record_manifest(*args, **kwargs) -> int:
            states.append(args[2])
            return 0

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for runtime in ("SE", "VR"):
                BUILDER.write_shader_cache_manifest(
                    root / runtime,
                    root / "Shaders",
                    runtime,
                    {},
                    record_manifest,
                    "a" * 64,
                )

        self.assertEqual(
            states,
            [
                f"ShaderCacheABI={'a' * 64};",
                f"VR;ShaderCacheABI={'a' * 64};",
            ],
        )

if __name__ == "__main__":
    unittest.main()
