#!/usr/bin/env python3
"""Regression tests for shader-cache generation and raw packaging."""

from __future__ import annotations

import configparser
import copy
import importlib.util
import json
import shutil
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from dxbc_fixtures import make_container, make_dxbc


REPO = Path(__file__).resolve().parent.parent
BUILDER_PATH = REPO / "tools/build-shader-cache.py"
SPEC = importlib.util.spec_from_file_location("build_shader_cache", BUILDER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load shader-cache builder: {BUILDER_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)


class ShaderBytecodeTests(unittest.TestCase):
    def test_accepts_supported_stages_and_program_chunks(self) -> None:
        for stage, extension in ((0, ".pso"), (1, ".vso"), (5, ".cso")):
            for kind in (b"SHDR", b"SHEX"):
                with self.subTest(stage=stage, kind=kind):
                    BUILDER.validate_dxbc(make_dxbc(stage, b"auxiliary data", kind), extension)

    def test_rejects_wrong_stages_and_unsupported_extensions(self) -> None:
        for stage in (0, 1, 2, 3, 4, 5, 0xFFFF):
            for expected, extension in ((0, ".pso"), (1, ".vso"), (5, ".cso")):
                if stage != expected:
                    with self.subTest(stage=stage, extension=extension), self.assertRaisesRegex(ValueError, "stage"):
                        BUILDER.validate_dxbc(make_dxbc(stage), extension)
        with self.assertRaisesRegex(ValueError, "extension"):
            BUILDER.validate_dxbc(make_dxbc(), ".unknown")

    def test_rejects_malformed_headers_chunks_and_programs(self) -> None:
        valid = make_dxbc(marker=b"padding")

        def word(offset: int, value: int) -> bytes:
            changed = bytearray(valid)
            struct.pack_into("<I", changed, offset, value)
            return bytes(changed)

        program = struct.pack("<II", 0x50, 2)
        cases = {
            "signature": b"BAD!" + valid[4:],
            "header-version": word(20, 2),
            "size-small": word(24, len(valid) - 1),
            "size-large": word(24, len(valid) + 1),
            "no-chunks": word(28, 0),
            "chunk-count-overflow": word(28, 0xFFFFFFFF),
            "chunk-inside-table": word(32, 36),
            "chunk-header-truncated": word(32, len(valid) - 4),
            "chunk-offset-overflow": word(32, 0xFFFFFFFF),
            "chunk-length-overflow": word(44, 0xFFFFFFFF),
            "program-short": word(44, 4),
            "program-length": word(52, 0xFFFFFFFF),
            "no-program": make_container([(b"PRIV", b"content")]),
            "partial-program-word": make_container([(b"SHEX", program + b"x")]),
            "duplicate-program": make_container([(b"SHDR", program), (b"SHEX", program)]),
        }
        overlap = bytearray(make_container([(b"PRIV", b""), (b"SHEX", program)]))
        struct.pack_into("<I", overlap, 44, 4)
        cases["overlapping-chunks"] = bytes(overlap)
        duplicate = bytearray(make_container([(b"SHEX", program), (b"PRIV", b""), (b"PRIV", b"")]))
        struct.pack_into("<I", duplicate, 40, struct.unpack_from("<I", duplicate, 36)[0])
        cases["duplicate-chunk-offset"] = bytes(duplicate)
        for name, data in cases.items():
            with self.subTest(case=name), self.assertRaises(ValueError):
                BUILDER.validate_dxbc(data, ".pso")
        for size in range(len(valid)):
            with self.subTest(truncated_size=size), self.assertRaises(ValueError):
                BUILDER.validate_dxbc(valid[:size], ".pso")

    def test_accepts_unordered_chunk_table(self) -> None:
        data = bytearray(make_dxbc())
        first, second = struct.unpack_from("<II", data, 32)
        struct.pack_into("<II", data, 32, second, first)
        BUILDER.validate_dxbc(bytes(data), ".pso")

    def test_loose_cache_rejects_bytecode_despite_matching_inventory(self) -> None:
        for stage, extension in ((0, ".pso"), (1, ".vso"), (5, ".cso")):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory() as temporary:
                cache = Path(temporary)
                relative = "Lighting/1" + extension
                path = cache / relative
                path.parent.mkdir()
                (cache / "Info.ini").write_text(
                    "[Cache]\nPluginVersion=test\nShaderCacheABI=abi\n", encoding="utf-8"
                )
                (cache / "Manifest.json").write_text(json.dumps({
                    "schemaVersion": 2, "entries": {relative: "a" * 32},
                }), encoding="utf-8")
                valid = make_dxbc(stage)
                path.write_bytes(valid)
                self.assertEqual(BUILDER.validate_cache(cache, "VR", "test", "abi"), 1)
                for data in (valid[:-1], make_dxbc((stage + 1) % 6), b"DXBC"):
                    path.write_bytes(data)
                    with self.assertRaisesRegex(SystemExit, "DXBC"):
                        BUILDER.validate_cache(cache, "VR", "test", "abi")


class ShaderCachePackagingTests(unittest.TestCase):
    @staticmethod
    def _managed_cache(root: Path, runtime: str, *, horizon: bool = True) -> Path:
        cache = root / BUILDER.CACHE_DIRECTORY
        variants = [cache]
        if horizon:
            variants.append(root / BUILDER.HORIZON_FIX_CACHE_DIRECTORY)
        for index, variant in enumerate(variants):
            water = variant / "Water" / "1.pso"
            water.parent.mkdir(parents=True)
            water.write_bytes(make_dxbc(marker=f"{runtime}-water-{index}".encode("utf-8")))
            lighting = variant / "Lighting" / "2.pso"
            lighting.parent.mkdir()
            lighting.write_bytes(make_dxbc(marker=f"{runtime}-lighting".encode("utf-8")))
            (variant / BUILDER.MANIFEST_FILE_NAME).write_text(
                json.dumps({
                    "schemaVersion": BUILDER.MANIFEST_SCHEMA_VERSION,
                    "entries": {
                        "Water/1.pso": str(index + 1) * 32,
                        "Lighting/2.pso": "3" * 32,
                    },
                }),
                encoding="utf-8",
            )
        (cache / BUILDER.INFO_FILE_NAME).write_text(
            f"[Cache]\nPluginVersion = CSX 3.18-VR\nShaderCacheABI = {'a' * 64}\n",
            encoding="utf-8",
        )
        BUILDER.build_managed_shader_packs(
            REPO, cache, variants[1] if horizon else None, runtime, "a" * 64
        )
        return cache

    @staticmethod
    def _archive_cache(root: Path) -> Path:
        archive = root.parent / "cache.zip"
        with zipfile.ZipFile(archive, "w") as stream:
            for path in root.rglob("*"):
                if path.is_file():
                    stream.write(path, path.relative_to(root).as_posix())
        return archive

    @staticmethod
    def _publication_cache(root: Path, marker: bytes) -> Path:
        cache = root / BUILDER.CACHE_DIRECTORY
        cache.mkdir(parents=True)
        (cache / BUILDER.INFO_FILE_NAME).write_text(
            "[Cache]\nPluginVersion = CSX publication test\n",
            encoding="utf-8",
        )
        (cache / "marker.bin").write_bytes(marker)
        return root

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required to inspect cache archives")
    def test_managed_archives_cover_se_vr_and_both_horizon_states(self) -> None:
        for runtime in ("SE", "VR"):
            for horizon in (False, True):
                with self.subTest(runtime=runtime, horizon=horizon), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary) / "runtime"
                    cache = self._managed_cache(root, runtime, horizon=horizon)
                    manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text(encoding="utf-8"))
                    self.assertEqual(manifest["runtime"], runtime)
                    self.assertEqual(manifest["optimizedRecordCount"], 3 if horizon else 2)
                    self.assertEqual(
                        manifest["compatibilityVariants"],
                        ["default", "legacy-horizon-fix"] if horizon else ["default"],
                    )
                    archive = self._archive_cache(root)
                    BUILDER.validate_cache_archive(
                        archive, shutil.which("cmake"), runtime, "CSX 3.18-VR",
                        horizon_variants=horizon,
                    )

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required to inspect cache archives")
    def test_archive_rejects_mismatched_abi_and_invalid_manifest_shape(self) -> None:
        for invalid_manifest in ([], None, {"shaderCacheABI": "b" * 64}):
            with self.subTest(manifest=invalid_manifest), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "runtime"
                cache = self._managed_cache(root, "SE")
                path = cache / BUILDER.PACK_MANIFEST_FILE_NAME
                manifest = json.loads(path.read_text(encoding="utf-8"))
                if isinstance(invalid_manifest, dict):
                    manifest.update(invalid_manifest)
                else:
                    manifest = invalid_manifest
                path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaises(SystemExit):
                    BUILDER.validate_cache_archive(
                        self._archive_cache(root), shutil.which("cmake"), "SE", "CSX 3.18-VR"
                    )

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required to inspect cache archives")
    def test_shipped_archive_requires_horizon_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            root = workspace / "runtime"
            self._managed_cache(root, "VR", horizon=False)
            with self.assertRaisesRegex(SystemExit, "missing required compatibility variants"):
                BUILDER.prepare_cache_archive(
                    root, workspace, "VR", "test", "CSX 3.18-VR", shutil.which("cmake")
                )

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required to inspect cache archives")
    def test_archive_rejects_horizon_declaration_without_records(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "runtime"
            cache = self._managed_cache(root, "VR", horizon=False)
            path = cache / BUILDER.PACK_MANIFEST_FILE_NAME
            manifest = json.loads(path.read_text(encoding="utf-8"))
            manifest["compatibilityVariants"].append("legacy-horizon-fix")
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "coverage"):
                BUILDER.validate_cache_archive(
                    self._archive_cache(root), shutil.which("cmake"), "VR", "CSX 3.18-VR",
                    horizon_variants=True,
                )

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required to inspect cache archives")
    def test_archive_rejects_obsolete_separate_horizon_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "runtime"
            self._managed_cache(root, "VR")
            extra = root / BUILDER.HORIZON_FIX_CACHE_DIRECTORY / "Info.ini"
            extra.parent.mkdir()
            extra.write_text("obsolete", encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "unexpected managed-cache files"):
                BUILDER.validate_cache_archive(
                    self._archive_cache(root), shutil.which("cmake"), "VR", "CSX 3.18-VR"
                )

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required to inspect cache archives")
    def test_archive_rejects_malformed_bytecode_with_valid_pack_hashes(self) -> None:
        for runtime in ("SE", "VR"):
            for bytecode in (make_dxbc(stage=1), make_dxbc()[:-1]):
                with self.subTest(runtime=runtime, bytecode=bytecode), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary) / "runtime"
                    cache = self._managed_cache(root, runtime, horizon=False)
                    pack_manifest = json.loads((cache / BUILDER.PACK_MANIFEST_FILE_NAME).read_text(encoding="utf-8"))
                    registrations = BUILDER.compatibility_variant_manifest(REPO)["default"]["registrations"]
                    records = [
                        {
                            **BUILDER.shader_pack_record_identity(path, contract, registrations),
                            "bytecode": contents,
                        }
                        for path, contract, contents in (
                            ("Water/1.pso", "1" * 32, bytecode),
                            ("Lighting/2.pso", "3" * 32, make_dxbc()),
                        )
                    ]
                    BUILDER.write_shader_pack(
                        cache / "Optimized.A.csxpack", 1, 1, records, pack_manifest["packSetId"]
                    )
                    with self.assertRaisesRegex(SystemExit, "invalid packaged shader bytecode"):
                        BUILDER.validate_cache_archive(
                            self._archive_cache(root), shutil.which("cmake"), runtime, "CSX 3.18-VR"
                        )

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

    def test_runtime_publication_replaces_cache_and_removes_recovery_copy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out_root = root / "out"
            out_root.mkdir()
            self._publication_cache(out_root / "VR", b"previous")
            candidate = self._publication_cache(root / "candidate", b"candidate")

            published = BUILDER.publish_runtime_cache(candidate, out_root, "VR")

            self.assertEqual(published, out_root / "VR")
            self.assertEqual(
                (published / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"candidate",
            )
            self.assertFalse((out_root / ".VR.publishing").exists())
            self.assertFalse((out_root / ".VR.previous").exists())

    def test_publication_copy_preserves_competing_staging_owner(self) -> None:
        for directory in (False, True):
            with self.subTest(directory=directory), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "candidate"
                staging = root / ".publishing"
                if directory:
                    source.mkdir()
                    (source / "marker").write_bytes(b"candidate")
                else:
                    source.write_bytes(b"candidate")
                original_exists = BUILDER.path_entry_exists
                raced = False

                def competing_owner(path: Path) -> bool:
                    nonlocal raced
                    if path == staging and not raced:
                        raced = True
                        if directory:
                            staging.mkdir()
                            (staging / "marker").write_bytes(b"other invocation")
                        else:
                            staging.write_bytes(b"other invocation")
                        return False
                    return original_exists(path)

                with (
                    mock.patch.object(BUILDER, "path_entry_exists", side_effect=competing_owner),
                    self.assertRaisesRegex(SystemExit, "failed to stage"),
                ):
                    BUILDER.copy_publication_candidate(source, staging, "test cache")

                marker = staging / "marker" if directory else staging
                self.assertEqual(marker.read_bytes(), b"other invocation")

    def test_publication_copy_cleans_only_its_failed_copy(self) -> None:
        for directory in (False, True):
            with self.subTest(directory=directory), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "candidate"
                staging = root / ".publishing"
                if directory:
                    source.mkdir()
                else:
                    source.write_bytes(b"candidate")
                copy_name = "copytree" if directory else "copy2"
                with (
                    mock.patch.object(BUILDER.shutil, copy_name, side_effect=PermissionError("copy failed")),
                    self.assertRaisesRegex(SystemExit, "failed to stage"),
                ):
                    BUILDER.copy_publication_candidate(source, staging, "test cache")
                self.assertFalse(staging.exists())
                self.assertTrue(source.exists())

    def test_runtime_publication_restores_previous_cache_after_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out_root = root / "out"
            out_root.mkdir()
            destination = self._publication_cache(out_root / "VR", b"previous")
            candidate = self._publication_cache(root / "candidate", b"candidate")
            staging = out_root / ".VR.publishing"
            original_replace = Path.replace

            def fail_publication(path: Path, target: Path) -> Path:
                if path == staging:
                    raise PermissionError("simulated publication failure")
                return original_replace(path, target)

            with (
                mock.patch.object(Path, "replace", new=fail_publication),
                mock.patch.object(BUILDER.time, "sleep"),
                self.assertRaisesRegex(SystemExit, "validated staging retained"),
            ):
                BUILDER.publish_runtime_cache(candidate, out_root, "VR")

            self.assertEqual(
                (destination / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"previous",
            )
            self.assertEqual(
                (staging / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"candidate",
            )
            self.assertFalse((out_root / ".VR.previous").exists())

    def test_runtime_publication_retains_recovery_after_restore_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out_root = root / "out"
            out_root.mkdir()
            destination = self._publication_cache(out_root / "VR", b"previous")
            staging = out_root / ".VR.publishing"
            recovery = out_root / ".VR.previous" / "VR"
            original_replace = Path.replace

            with tempfile.TemporaryDirectory(dir=root) as workspace_text:
                candidate = self._publication_cache(
                    Path(workspace_text) / "candidate",
                    b"candidate",
                )

                def fail_publication_and_restore(path: Path, target: Path) -> Path:
                    if path == staging or path == recovery:
                        raise PermissionError("simulated publication or restore failure")
                    return original_replace(path, target)

                with (
                    mock.patch.object(
                        Path,
                        "replace",
                        new=fail_publication_and_restore,
                    ),
                    mock.patch.object(BUILDER.time, "sleep"),
                    self.assertRaises(SystemExit) as raised,
                ):
                    BUILDER.publish_runtime_cache(candidate, out_root, "VR")

            self.assertFalse(destination.exists())
            self.assertIn(str(recovery), str(raised.exception))
            self.assertEqual(
                (recovery / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"previous",
            )
            self.assertEqual(
                (staging / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"candidate",
            )

    def test_runtime_publication_refuses_existing_recovery_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out_root = root / "out"
            out_root.mkdir()
            destination = self._publication_cache(out_root / "VR", b"previous")
            candidate = self._publication_cache(root / "candidate", b"candidate")
            recovery = out_root / ".VR.previous"
            recovery.mkdir()
            (recovery / "operator-note.txt").write_text("retain", encoding="utf-8")

            with self.assertRaisesRegex(SystemExit, "recovery path exists"):
                BUILDER.publish_runtime_cache(candidate, out_root, "VR")

            self.assertEqual(
                (destination / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"previous",
            )
            self.assertEqual(
                (recovery / "operator-note.txt").read_text(encoding="utf-8"),
                "retain",
            )
            self.assertFalse((out_root / ".VR.publishing").exists())

    def test_runtime_publication_preserves_interrupted_recovery_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out_root = root / "out"
            out_root.mkdir()
            recovery = self._publication_cache(
                out_root / ".VR.previous" / "VR",
                b"previous",
            )
            staging = self._publication_cache(
                out_root / ".VR.publishing",
                b"candidate",
            )
            retry_candidate = self._publication_cache(
                root / "retry-candidate",
                b"retry",
            )

            with self.assertRaisesRegex(SystemExit, "recovery path exists"):
                BUILDER.publish_runtime_cache(retry_candidate, out_root, "VR")

            self.assertEqual(
                (recovery / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"previous",
            )
            self.assertEqual(
                (staging / BUILDER.CACHE_DIRECTORY / "marker.bin").read_bytes(),
                b"candidate",
            )
            self.assertFalse((out_root / "VR").exists())

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
        compile_states: list[str] = []

        def record_manifest(*args, **kwargs) -> int:
            compile_states.append(args[2])
            return 0

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
                    REPO,
                    "CSX 12.345-VR",
                    "VR",
                    BUILDER.SHIPPED_CACHE_PROFILE,
                    "test-shader-abi",
                    enabled_overrides={"HorizonFix": enabled},
                )
                states = BUILDER.read_feature_states(cache_dir)
                self.assertIs(states["HorizonFix"], enabled)
                self.assertTrue(states["CSUtility"])
                info = configparser.ConfigParser(interpolation=None)
                info.read(cache_dir / BUILDER.INFO_FILE_NAME, encoding="utf-8-sig")
                self.assertFalse(info.has_option("HorizonFix", "ShaderCacheABI"))
                BUILDER.write_shader_cache_manifest(
                    cache_dir, root / "stage", "VR", {}, record_manifest, "test-shader-abi", []
                )

        self.assertEqual(len(compile_states), 2)
        self.assertEqual(compile_states[0], compile_states[1])
        self.assertNotIn("FeatureShaderABI=HorizonFix:", compile_states[0])

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
                REPO,
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
                    REPO,
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
                cache_dir = root / runtime
                cache_dir.mkdir()
                (cache_dir / BUILDER.INFO_FILE_NAME).write_text(
                    "[Cache]\n"
                    "[LightLimitFix]\n"
                    "Enabled = true\n"
                    "ShaderCacheABI = 1\n",
                    encoding="utf-8",
                )
                BUILDER.write_shader_cache_manifest(
                    cache_dir,
                    root / "Shaders",
                    runtime,
                    {},
                    record_manifest,
                    "a" * 64,
                    [],
                )

        self.assertEqual(
            states,
            [
                f"ShaderCacheABI={'a' * 64};FeatureShaderABI=LightLimitFix:1;",
                f"VR;ShaderCacheABI={'a' * 64};FeatureShaderABI=LightLimitFix:1;",
            ],
        )

if __name__ == "__main__":
    unittest.main()
