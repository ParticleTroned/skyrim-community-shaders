#!/usr/bin/env python3
"""Build a distributable shader disk cache for this repo.

Produces the layout the runtime consumes at Data/ShaderCache/:
  ShaderCache/<ShaderName>/<descriptor:HEX>.{pso,vso,cso}
  ShaderCache/Info.ini
  ShaderCache/Manifest.json

Packaged archives also contain a FOMOD installer that preserves the required
ShaderCache directory when installing through Mod Organizer 2.

The generated cache targets this repo's shipped distribution profile:
  - the VR feature is omitted on SE
  - shipped features are treated as active
  - plugin-detected compatibility features are inactive until detected at runtime
  - WetnessEffects is legacy, default-off, and not shipped

Usage:
  python tools/build-shader-cache.py --runtime both --package
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parent.parent
CACHE_DIRECTORY = "ShaderCache"
CACHE_EXTENSIONS = frozenset({".pso", ".vso", ".cso"})
INFO_FILE_NAME = "Info.ini"
MANIFEST_FILE_NAME = "Manifest.json"
MANIFEST_SCHEMA_VERSION = 1
FOMOD_DIRECTORY = "fomod"
FOMOD_CONFIG_FILE_NAME = "ModuleConfig.xml"
FOMOD_INFO_FILE_NAME = "info.xml"


RUNTIME_EXCLUDED_FEATURES = {
    "SE": {"VR"},
    "VR": set(),
}

# Distribution profile transforms. The source validation configs are still
# useful as compile inventories, but the shipped cache profile is different:
# WetnessEffects is legacy/non-shipped, while Wetterness and UnifiedWater ship on.
NON_SHIPPED_FEATURES = {
    "WetnessEffects": {
        "define": "WETNESS_EFFECTS",
        "package": "Wetness Effects",
    },
}
GLOBAL_SHIPPED_DEFINES = ("UNIFIED_WATER",)
FILE_SHIPPED_DEFINES = {
    "Lighting.hlsl": ("WETTERNESS",),
    "Water.hlsl": ("WETTERNESS",),
}
CACHE_DEFAULT_DISABLED_FEATURES = frozenset({"HorizonFix"})
NON_SHIPPED_DEFINES = {
    feature["define"]
    for feature in NON_SHIPPED_FEATURES.values()
}
DEBUG_PROFILE_DEFINES = {
    "DEBUG",
    "_DEBUG",
    "D3D_DEBUG_INFO",
    "D3DCOMPILE_DEBUG",
    "D3DCOMPILE_SKIP_OPTIMIZATION",
}
PROFILE_EXCLUDED_DEFINES = NON_SHIPPED_DEFINES | DEBUG_PROFILE_DEFINES
NON_SHIPPED_PACKAGES = {
    feature["package"]
    for feature in NON_SHIPPED_FEATURES.values()
}

IMAGESPACE_DIRS = {
    (0, 0): "WorldMap",
    (1, 1): "Refraction",
    (2, 2): "ISFXAA",
    (3, 3): "DepthOfField",
    (5, 5): "RadialBlur",
    (6, 6): "FullScreenBlur",
    (7, 7): "GetHit",
    (8, 8): "Map",
    (9, 9): "Blur3",
    (10, 10): "Blur5",
    (11, 11): "Blur7",
    (12, 12): "Blur9",
    (13, 13): "Blur11",
    (14, 14): "Blur13",
    (15, 15): "Blur15",
    (16, 16): "BlurNonHDR3",
    (17, 17): "BlurNonHDR5",
    (18, 18): "BlurNonHDR7",
    (19, 19): "BlurNonHDR9",
    (20, 20): "BlurNonHDR11",
    (21, 21): "BlurNonHDR13",
    (22, 22): "BlurNonHDR15",
    (23, 23): "BlurBrightPass3",
    (24, 24): "BlurBrightPass5",
    (25, 25): "BlurBrightPass7",
    (26, 26): "BlurBrightPass9",
    (27, 27): "BlurBrightPass11",
    (28, 28): "BlurBrightPass13",
    (29, 29): "BlurBrightPass15",
    (30, 30): "HDR",
    (31, 31): "WaterDisplacement",
    (32, 32): "VolumetricLighting",
    (33, 33): "Noise",
    (34, 34): "ISCopy",
    (35, 35): "ISCopyDynamicFetchDisabled",
    (36, 36): "ISCopyScaleBias",
    (37, 37): "ISCopyCustomViewport",
    (38, 38): "ISCopyGrayScale",
    (39, 39): "ISRefraction",
    (40, 40): "ISDoubleVision",
    (41, 41): "ISCopyTextureMask",
    (42, 42): "ISMap",
    (43, 43): "ISWorldMap",
    (44, 44): "ISWorldMapNoSkyBlur",
    (45, 45): "ISDepthOfField",
    (46, 46): "ISDepthOfFieldFogged",
    (47, 47): "ISDepthOfFieldMaskedFogged",
    (49, 49): "ISDistantBlur",
    (50, 50): "ISDistantBlurFogged",
    (51, 51): "ISDistantBlurMaskedFogged",
    (52, 52): "ISRadialBlur",
    (53, 53): "ISRadialBlurMedium",
    (54, 54): "ISRadialBlurHigh",
    (55, 55): "ISHDRTonemapBlendCinematic",
    (56, 56): "ISHDRTonemapBlendCinematicFade",
    (57, 57): "ISHDRDownSample16",
    (58, 58): "ISHDRDownSample4",
    (59, 59): "ISHDRDownSample16Lum",
    (60, 60): "ISHDRDownSample4RGB2Lum",
    (61, 61): "ISHDRDownSample4LumClamp",
    (62, 62): "ISHDRDownSample4LightAdapt",
    (63, 63): "ISHDRDownSample16LumClamp",
    (64, 64): "ISHDRDownSample16LightAdapt",
    (65, 65): "ISBlur3",
    (66, 66): "ISBlur5",
    (67, 67): "ISBlur7",
    (68, 68): "ISBlur9",
    (69, 69): "ISBlur11",
    (70, 70): "ISBlur13",
    (71, 71): "ISBlur15",
    (72, 72): "ISNonHDRBlur3",
    (73, 73): "ISNonHDRBlur5",
    (74, 74): "ISNonHDRBlur7",
    (75, 75): "ISNonHDRBlur9",
    (76, 76): "ISNonHDRBlur11",
    (77, 77): "ISNonHDRBlur13",
    (78, 78): "ISNonHDRBlur15",
    (79, 79): "ISBrightPassBlur3",
    (80, 80): "ISBrightPassBlur5",
    (81, 81): "ISBrightPassBlur7",
    (82, 82): "ISBrightPassBlur9",
    (83, 83): "ISBrightPassBlur11",
    (84, 84): "ISBrightPassBlur13",
    (85, 85): "ISBrightPassBlur15",
    (86, 86): "ISWaterDisplacementClearSimulation",
    (87, 87): "ISWaterDisplacementTexOffset",
    (88, 88): "ISWaterDisplacementWadingRipple",
    (89, 89): "ISWaterDisplacementRainRipple",
    (90, 90): "ISWaterWadingHeightmap",
    (91, 91): "ISWaterRainHeightmap",
    (92, 92): "ISWaterBlendHeightmaps",
    (93, 93): "ISWaterSmoothHeightmap",
    (94, 94): "ISWaterDisplacementNormals",
    (95, 95): "ISNoiseScrollAndBlend",
    (96, 96): "ISNoiseNormalmap",
    (97, 97): "ISVolumetricLighting",
    (98, 101): "ISLocalMap",
    (99, 102): "ISAlphaBlend",
    (100, 103): "ISLensFlare",
    (101, 104): "ISLensFlareVisibility",
    (102, 105): "ISApplyReflections",
    (103, 106): "ISApplyVolumetricLighting",
    (104, 107): "ISBasicCopy",
    (105, 108): "ISBlur",
    (106, 109): "ISVolumetricLightingBlurHCS",
    (107, 110): "ISVolumetricLightingBlurVCS",
    (108, 111): "ISReflectionBlurHCS",
    (109, 112): "ISReflectionBlurVCS",
    (110, 113): "ISParallaxMaskBlurHCS",
    (111, 114): "ISParallaxMaskBlurVCS",
    (112, 115): "ISDepthOfFieldBlurHCS",
    (113, 116): "ISDepthOfFieldBlurVCS",
    (114, 117): "ISCompositeVolumetricLighting",
    (115, 118): "ISCompositeLensFlare",
    (116, 119): "ISCompositeLensFlareVolumetricLighting",
    (117, 120): "ISCopySubRegionCS",
    (118, 121): "ISDebugSnow",
    (119, 122): "ISDownsample",
    (120, 123): "ISDownsampleIgnoreBrightest",
    (121, 124): "ISDownsampleCS",
    (122, 125): "ISDownsampleIgnoreBrightestCS",
    (123, 128): "ISExp",
    (124, 130): "ISIBLensFlares",
    (125, 131): "ISLightingComposite",
    (126, 132): "ISLightingCompositeNoDirectionalLight",
    (127, 133): "ISLightingCompositeMenu",
    (128, 134): "ISPerlinNoiseCS",
    (129, 135): "ISPerlinNoise2DCS",
    (130, 145): "ReflectionsRayTracing",
    (131, 146): "ISReflectionsDebugSpecMask",
    (132, 147): "ISSAOBlurH",
    (133, 148): "ISSAOBlurV",
    (134, 149): "ISSAOBlurHCS",
    (135, 150): "ISSAOBlurVCS",
    (136, 151): "ISSAOCameraZ",
    (137, 152): "ISSAOCameraZAndMipsCS",
    (138, 153): "ISSAOCompositeSAO",
    (139, 154): "ISSAOCompositeFog",
    (140, 155): "ISSAOCompositeSAOFog",
    (141, 156): "ISMinify",
    (142, 157): "ISMinifyContrast",
    (143, 158): "ISSAORawAO",
    (144, 159): "ISSAORawAONoTemporal",
    (145, 160): "ISSAORawAOCS",
    (146, 161): "ISSILComposite",
    (147, 162): "ISSILRawInd",
    (148, 163): "ISSimpleColor",
    (149, 164): "ISDisplayDepth",
    (150, 165): "ISSnowSSS",
    (151, 166): "ISTemporalAA",
    (152, 167): "ISTemporalAA_UI",
    (153, 168): "ISTemporalAA_Water",
    (154, 169): "ISUpsampleDynamicResolution",
    (155, 170): "ISWaterBlend",
    (156, 171): "ISUnderwaterMask",
    (157, 172): "ISWaterFlow",
}


def shader_source_roots(source_root: Path) -> tuple[Path, ...]:
    """Return every tree that could be copied into the merged shader stage."""
    package_shaders = source_root / "package/Shaders"
    features_root = source_root / "features"
    feature_shaders = (
        tuple(
            feature_dir / "Shaders"
            for feature_dir in sorted(features_root.iterdir())
            if (feature_dir / "Shaders").is_dir()
        )
        if features_root.is_dir()
        else ()
    )
    return (package_shaders, *feature_shaders)


def configs_for(source_root: Path) -> dict[str, Path]:
    return {
        "SE": source_root / ".github/configs/shader-validation.yaml",
        "VR": source_root / ".github/configs/shader-validation-vr.yaml",
    }


def stage_merged_shaders(source_root: Path, stage: Path) -> None:
    if stage.exists():
        raise RuntimeError(f"staging directory already exists: {stage}")

    source_roots = shader_source_roots(source_root)
    package_shaders = source_roots[0]
    features_root = source_root / "features"
    if not package_shaders.is_dir():
        raise SystemExit(f"missing package shader directory: {package_shaders}")
    if not features_root.is_dir():
        raise SystemExit(f"missing feature directory: {features_root}")

    ignore_tests = shutil.ignore_patterns("Tests")
    shutil.copytree(package_shaders, stage, ignore=ignore_tests)
    for shaders_dir in source_roots[1:]:
        feature_dir = shaders_dir.parent
        if feature_dir.name in NON_SHIPPED_PACKAGES:
            continue
        shutil.copytree(
            shaders_dir,
            stage,
            dirs_exist_ok=True,
            ignore=ignore_tests,
        )


def normalized_define_name(define: str) -> str:
    return define.split("=", 1)[0].strip().upper()


def append_missing_defines(defines: object, names: tuple[str, ...]) -> None:
    if not isinstance(defines, list):
        return

    existing = {
        normalized_define_name(define)
        for define in defines
        if isinstance(define, str)
    }
    for name in names:
        if normalized_define_name(name) not in existing:
            defines.append(name)


def apply_shipped_profile_defines(config: object) -> object:
    def scrub(node: object) -> object:
        if isinstance(node, dict):
            return {key: scrub(value) for key, value in node.items()}
        if isinstance(node, list):
            return [
                scrub(value)
                for value in node
                if not (
                    isinstance(value, str)
                    and normalized_define_name(value) in PROFILE_EXCLUDED_DEFINES
                )
            ]
        return node

    config = scrub(config)
    if not isinstance(config, dict):
        return config

    common_defines = config.get("common_defines")
    if not isinstance(common_defines, list):
        raise SystemExit("shader config common_defines must be a list")
    append_missing_defines(common_defines, GLOBAL_SHIPPED_DEFINES)

    file_common_defines = config.get("file_common_defines")
    if isinstance(file_common_defines, dict):
        for file_name, defines_to_add in FILE_SHIPPED_DEFINES.items():
            stage_defines = file_common_defines.get(file_name)
            if isinstance(stage_defines, dict):
                for defines in stage_defines.values():
                    if not isinstance(defines, list):
                        raise SystemExit(
                            f"shader config file_common_defines for {file_name} "
                            "must contain lists"
                        )
                    append_missing_defines(defines, defines_to_add)

    shaders = config.get("shaders")
    if not isinstance(shaders, list):
        raise SystemExit("shader config shaders must be a list")

    updated_files: set[str] = set()
    for shader in shaders:
        if not isinstance(shader, dict):
            continue

        file_name = shader.get("file")
        defines_to_add = FILE_SHIPPED_DEFINES.get(file_name)
        if not defines_to_add or not isinstance(file_name, str):
            continue

        stage_configs = shader.get("configs")
        if not isinstance(stage_configs, dict) or not stage_configs:
            raise SystemExit(
                f"shader config entry for {file_name} has no stage configs"
            )

        for stage_config in stage_configs.values():
            if not isinstance(stage_config, dict) or not isinstance(
                stage_config.get("common_defines"), list
            ):
                raise SystemExit(
                    f"shader config stage common_defines for {file_name} "
                    "must be a list"
                )
            append_missing_defines(
                stage_config["common_defines"],
                defines_to_add,
            )
        updated_files.add(file_name)

    missing_files = sorted(set(FILE_SHIPPED_DEFINES) - updated_files)
    if missing_files:
        raise SystemExit(
            "shader config is missing shipped-profile entries for: "
            + ", ".join(missing_files)
        )

    return config


def filter_profile_defines(config_path: Path, out_path: Path, yaml: Any) -> Path:
    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise SystemExit(f"shader config must be a YAML mapping: {config_path}")

    config = apply_shipped_profile_defines(config)
    out_path.write_text(
        yaml.safe_dump(config, sort_keys=False),
        encoding="utf-8",
    )
    return out_path


def remap_imagespace_dirs(cache_dir: Path, runtime: str) -> dict[str, str]:
    """Move ImageSpace blobs to the technique directories used by the runtime.

    Return the destination-to-source mapping required when hashing those blobs.
    """
    index = 1 if runtime == "VR" else 0
    by_descriptor = {
        descriptor_pair[index]: name
        for descriptor_pair, name in IMAGESPACE_DIRS.items()
    }
    renamed: dict[str, str] = {}

    for directory in sorted(cache_dir.iterdir()):
        if not directory.is_dir() or not directory.name.startswith("IS"):
            continue

        for path in sorted(directory.iterdir()):
            if path.suffix.lower() not in CACHE_EXTENSIONS:
                continue

            try:
                descriptor = int(path.stem, 16)
            except ValueError:
                continue

            target_dir_name = by_descriptor.get(descriptor)
            if not target_dir_name or target_dir_name == directory.name:
                continue

            previous_source = renamed.get(target_dir_name)
            if previous_source and previous_source != directory.name:
                raise SystemExit(
                    f"{runtime}: ImageSpace cache directory {target_dir_name} "
                    f"maps to both {previous_source} and {directory.name}"
                )
            renamed[target_dir_name] = directory.name
            target_dir = cache_dir / target_dir_name
            target_dir.mkdir(exist_ok=True)
            target_path = target_dir / path.name
            if target_path.exists():
                raise SystemExit(
                    f"{runtime}: refusing to overwrite remapped ImageSpace blob "
                    f"{target_path}"
                )
            path.replace(target_path)

        if not any(directory.iterdir()):
            directory.rmdir()

    return renamed


def write_shader_cache_manifest(
    cache_dir: Path,
    shader_root: Path,
    runtime: str,
    imagespace_remap: dict[str, str],
    write_manifest: Callable[..., int],
) -> int:
    """Hash source/include content for every compiled blob."""
    global_defines_state = "VR;" if runtime == "VR" else ""
    count = write_manifest(
        cache_dir,
        shader_root,
        global_defines_state,
        cache_dir / MANIFEST_FILE_NAME,
        resolve_source_name=lambda name: imagespace_remap.get(name, name),
    )
    print(
        f"{runtime}: wrote {count} content digests -> "
        f"{cache_dir / MANIFEST_FILE_NAME}"
    )
    return count


def prune_non_cache_files(cache_dir: Path) -> None:
    keep_names = {INFO_FILE_NAME, MANIFEST_FILE_NAME}

    for path in cache_dir.rglob("*"):
        if (
            path.is_file()
            and path.suffix.lower() not in CACHE_EXTENSIONS
            and path.name not in keep_names
        ):
            path.unlink()

    for directory in sorted((path for path in cache_dir.rglob("*") if path.is_dir()), reverse=True):
        if not any(directory.iterdir()):
            directory.rmdir()


def write_info_ini(cache_dir: Path, stage: Path, plugin_version: str, runtime: str) -> int:
    validate_ini_value(plugin_version, "plugin version")
    lines = ["[Cache]", f"PluginVersion = {plugin_version}", "", ""]
    count = 0

    for ini_path in sorted((stage / "Features").glob("*.ini")):
        stem = ini_path.stem
        if stem in RUNTIME_EXCLUDED_FEATURES[runtime] or stem in NON_SHIPPED_FEATURES:
            continue

        config = configparser.ConfigParser(interpolation=None)
        try:
            with ini_path.open("r", encoding="utf-8-sig") as stream:
                config.read_file(stream)
        except (configparser.Error, OSError, UnicodeError) as exc:
            raise SystemExit(f"cannot parse feature metadata {ini_path}: {exc}") from exc

        version = config.get("Info", "Version", fallback=None)
        if not version:
            raise SystemExit(f"{ini_path.name} has no Info/Version")
        validate_ini_value(version, f"{ini_path.name} feature version")

        enabled = "false" if stem in CACHE_DEFAULT_DISABLED_FEATURES else "true"
        lines += [f"[{stem}]", f"Enabled = {enabled}", f"Version = {version}", "", ""]
        count += 1

    (cache_dir / INFO_FILE_NAME).write_bytes(
        b"\xef\xbb\xbf" + "\r\n".join(lines).encode("utf-8")
    )
    return count


def default_plugin_version(source_root: Path, runtime: str) -> str:
    presets_path = source_root / "CMakePresets.json"
    if not presets_path.is_file():
        raise SystemExit(
            "cannot derive plugin version from CMakePresets.json; pass --plugin-version"
        )

    try:
        presets = json.loads(presets_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(
            f"cannot parse {presets_path}; pass --plugin-version"
        ) from exc

    candidates = {
        "SE": ["AIO-Release", "FLATRIM", "SE"],
        "VR": ["ALL", "ALL-VS2022", "VR"],
    }

    by_name = {
        preset.get("name"): preset
        for preset in presets.get("configurePresets", [])
        if isinstance(preset, dict)
    }

    for preset_name in candidates[runtime]:
        preset = by_name.get(preset_name)
        if not preset:
            continue
        cache_variables = preset.get("cacheVariables", {})
        version = cache_variables.get("CSX_VERSION")
        if isinstance(version, str) and version:
            return f"CSX {version}"

    raise SystemExit(
        f"cannot derive {runtime} plugin version from CMakePresets.json; pass --plugin-version"
    )


def locate_fxc(explicit: str | None) -> str:
    """Resolve fxc.exe without requiring a custom PATH."""
    if explicit:
        resolved = shutil.which(explicit)
        candidate = Path(resolved or explicit).expanduser()
        if candidate.is_file():
            return str(candidate.resolve())
        raise SystemExit(f"fxc.exe does not exist: {explicit}")

    from_path = shutil.which("fxc.exe") or shutil.which("fxc")
    if from_path:
        return str(Path(from_path).resolve())

    sdk_roots: list[Path] = []
    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    if program_files_x86:
        sdk_roots.append(Path(program_files_x86) / "Windows Kits/10/bin")
    sdk_roots.append(Path(r"C:\Program Files (x86)\Windows Kits\10\bin"))

    def version_key(path: Path) -> tuple[int, ...]:
        try:
            return tuple(int(part) for part in path.name.split("."))
        except ValueError:
            return ()

    for sdk_root in dict.fromkeys(sdk_roots):
        if not sdk_root.is_dir():
            continue
        for version_dir in sorted(
            (path for path in sdk_root.iterdir() if path.is_dir()),
            key=version_key,
            reverse=True,
        ):
            candidate = version_dir / "x64/fxc.exe"
            if candidate.is_file():
                return str(candidate.resolve())

    raise SystemExit(
        "fxc.exe was not found in PATH or the Windows 10 SDK. "
        "Install the Windows SDK or pass --fxc PATH."
    )


def validate_cache(
    cache_dir: Path,
    runtime: str,
    plugin_version: str,
) -> int:
    """Fail before packaging if the cache is incomplete or malformed."""
    info_path = cache_dir / INFO_FILE_NAME
    manifest_path = cache_dir / MANIFEST_FILE_NAME
    if not info_path.is_file():
        raise SystemExit(f"{runtime}: missing {info_path}")
    if not manifest_path.is_file():
        raise SystemExit(f"{runtime}: missing {manifest_path}")

    info = configparser.ConfigParser(interpolation=None)
    try:
        with info_path.open("r", encoding="utf-8-sig") as stream:
            info.read_file(stream)
    except (configparser.Error, OSError, UnicodeError) as exc:
        raise SystemExit(f"{runtime}: invalid {info_path}: {exc}") from exc

    actual_version = info.get("Cache", "PluginVersion", fallback=None)
    if actual_version != plugin_version:
        raise SystemExit(
            f"{runtime}: Info.ini plugin version is {actual_version!r}; "
            f"expected {plugin_version!r}"
        )

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"{runtime}: invalid {manifest_path}: {exc}") from exc

    if not isinstance(manifest, dict) or manifest.get(
        "schemaVersion"
    ) != MANIFEST_SCHEMA_VERSION or not isinstance(
        manifest.get("entries"), dict
    ):
        raise SystemExit(f"{runtime}: unsupported or malformed cache manifest")
    entries: dict[str, object] = manifest["entries"]

    blob_paths = sorted(
        path
        for path in cache_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in CACHE_EXTENSIONS
    )
    if not blob_paths:
        raise SystemExit(f"{runtime}: cache contains no compiled shader blobs")

    missing_entries: list[str] = []
    invalid_entries: list[str] = []
    invalid_blobs: list[str] = []
    blob_keys: set[str] = set()
    for blob_path in blob_paths:
        relative_path = blob_path.relative_to(cache_dir).as_posix()
        blob_keys.add(relative_path)
        digest = entries.get(relative_path)
        if digest is None:
            missing_entries.append(relative_path)
        elif not isinstance(digest, str) or not re.fullmatch(
            r"[0-9a-f]{32}", digest
        ):
            invalid_entries.append(relative_path)

        try:
            with blob_path.open("rb") as stream:
                signature = stream.read(4)
            if signature != b"DXBC":
                invalid_blobs.append(relative_path)
        except OSError:
            invalid_blobs.append(relative_path)

    if missing_entries:
        raise SystemExit(
            f"{runtime}: {len(missing_entries)} blobs are absent from Manifest.json; "
            f"first: {', '.join(missing_entries[:5])}"
        )
    if invalid_entries:
        raise SystemExit(
            f"{runtime}: {len(invalid_entries)} manifest digests are invalid; "
            f"first: {', '.join(invalid_entries[:5])}"
        )
    if invalid_blobs:
        raise SystemExit(
            f"{runtime}: {len(invalid_blobs)} files are not valid DXBC containers; "
            f"first: {', '.join(invalid_blobs[:5])}"
        )

    unexpected_entries = sorted(set(entries) - blob_keys)
    if unexpected_entries:
        raise SystemExit(
            f"{runtime}: Manifest.json contains {len(unexpected_entries)} entries "
            f"without a compiled blob; first: {', '.join(unexpected_entries[:5])}"
        )

    print(
        f"{runtime}: validated {len(blob_paths)} DXBC blobs and "
        f"{len(entries)} manifest entries"
    )
    return len(blob_paths)


def safe_label(value: str) -> str:
    label = re.sub(r"[^A-Za-z0-9._-]+", "-", value).strip("-")
    return label or "cache"


def validate_ini_value(value: str, label: str) -> None:
    if (
        not value
        or value != value.strip()
        or any(ord(character) < 0x20 for character in value)
    ):
        raise SystemExit(f"{label} is empty or contains unsafe INI characters")


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return parsed


def require_compile_tools() -> tuple[tuple[str, ...], Any, Callable[..., int]]:
    try:
        import yaml
        from hlslkit import compile_shaders
        from hlslkit.shader_digest import SCHEMA_VERSION, write_manifest
    except (ImportError, ModuleNotFoundError) as exc:
        raise SystemExit(
            "PyYAML and the pinned hlslkit revision are required; see "
            "tools/shader-cache-requirements.txt and "
            "docs/development/prebuilt-shader-cache.md"
        ) from exc

    if SCHEMA_VERSION != MANIFEST_SCHEMA_VERSION:
        raise SystemExit(
            "hlslkit shader manifest schema does not match this builder: "
            f"{SCHEMA_VERSION} != {MANIFEST_SCHEMA_VERSION}"
        )

    # Running the module through this interpreter guarantees the compiler and
    # manifest writer come from the same hlslkit installation. A PATH command
    # can otherwise point at a different version and silently break the digest
    # contract with the runtime.
    return (sys.executable, "-m", compile_shaders.__name__), yaml, write_manifest


def is_replaceable_runtime_output(path: Path) -> bool:
    """Only replace a cache layout this tool owns; never erase arbitrary output."""
    cache_path = path / CACHE_DIRECTORY
    path_is_junction = getattr(path, "is_junction", lambda: False)()
    cache_is_junction = getattr(cache_path, "is_junction", lambda: False)()
    if (
        not path.is_dir()
        or path.is_symlink()
        or path_is_junction
        or not cache_path.is_dir()
        or cache_path.is_symlink()
        or cache_is_junction
    ):
        return False

    info_path = cache_path / INFO_FILE_NAME
    if not info_path.is_file():
        return False

    parser = configparser.ConfigParser(interpolation=None)
    try:
        with info_path.open("r", encoding="utf-8-sig") as stream:
            parser.read_file(stream)
    except (configparser.Error, OSError, UnicodeError):
        return False

    return bool(parser.get("Cache", "PluginVersion", fallback="").strip())


def path_entry_exists(path: Path) -> bool:
    """Include dangling links, which Path.exists() deliberately hides."""
    return os.path.lexists(path)


def runtime_output_destination(out_root: Path, runtime: str) -> Path:
    """Validate and return one publication destination without changing it."""
    destination = out_root / runtime
    if path_entry_exists(destination) and not is_replaceable_runtime_output(destination):
        raise SystemExit(
            f"refusing to replace non-cache output directory: {destination}; "
            "choose an empty --out directory"
        )
    return destination


def archive_output_destination(out_root: Path, candidate: Path) -> Path:
    """Allow a reused archive label only when it names an ordinary file."""
    destination = out_root / candidate.name
    destination_is_junction = getattr(destination, "is_junction", lambda: False)()
    if path_entry_exists(destination) and (
        not destination.is_file()
        or destination.is_symlink()
        or destination_is_junction
    ):
        raise SystemExit(
            f"refusing to replace non-file or linked cache archive: {destination}; "
            "choose a different --package-label or --out directory"
        )
    return destination


def publish_runtime_cache(candidate_root: Path, out_root: Path, runtime: str) -> Path:
    """Publish a fully validated cache while preserving the previous cache on failure."""
    destination = runtime_output_destination(out_root, runtime)
    if not path_entry_exists(destination):
        candidate_root.replace(destination)
        return destination

    backup = candidate_root.parent / f"{runtime}.previous"
    if backup.exists():
        raise RuntimeError(f"unexpected temporary backup already exists: {backup}")

    try:
        destination.replace(backup)
    except OSError as exc:
        raise SystemExit(
            f"could not move the existing {runtime} cache aside: {destination}"
        ) from exc
    try:
        candidate_root.replace(destination)
    except OSError as exc:
        try:
            backup.replace(destination)
        except OSError as restore_error:
            raise SystemExit(
                f"failed to publish {runtime} cache and could not restore "
                f"the previous output: {destination}"
            ) from restore_error
        raise SystemExit(f"failed to publish {runtime} cache: {destination}") from exc

    # The old cache remains inside the isolated temporary workspace until it
    # is cleaned up after this invocation. It is never recursively deleted
    # from the user-selected output root.
    return destination


def write_fomod_installer(runtime_root: Path, runtime: str, label: str) -> Path:
    """Stage an MO2 installer that maps ShaderCache to Data/ShaderCache."""
    fomod_dir = runtime_root / FOMOD_DIRECTORY
    if path_entry_exists(fomod_dir):
        raise SystemExit(
            f"refusing to replace unexpected cache installer directory: {fomod_dir}"
        )

    fomod_dir.mkdir()
    display_label = safe_label(label)
    module_name = f"Community Shaders {runtime} Shader Cache - {display_label}"
    description = (
        "Installs the precompiled shader cache at Data\\ShaderCache without "
        "flattening its required directory."
    )
    module_config = f"""<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
        xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
  <moduleName>{module_name}</moduleName>
  <requiredInstallFiles>
    <folder source="{CACHE_DIRECTORY}" destination="{CACHE_DIRECTORY}" priority="0" />
  </requiredInstallFiles>
</config>
"""
    info = f"""<?xml version="1.0" encoding="UTF-8"?>
<fomod>
  <Name>{module_name}</Name>
  <Author>Community Shaders Expanded</Author>
  <Version>{display_label}</Version>
  <Description>{description}</Description>
</fomod>
"""

    try:
        (fomod_dir / FOMOD_CONFIG_FILE_NAME).write_text(
            module_config, encoding="utf-8", newline="\n"
        )
        (fomod_dir / FOMOD_INFO_FILE_NAME).write_text(
            info, encoding="utf-8", newline="\n"
        )
    except OSError as exc:
        shutil.rmtree(fomod_dir, ignore_errors=True)
        raise SystemExit(f"failed to stage cache installer: {fomod_dir}") from exc
    return fomod_dir


def validate_cache_archive(archive: Path, cmake: str, runtime: str) -> None:
    """Verify the archive is installable without flattening ShaderCache."""
    command = [cmake, "-E", "tar", "tf", str(archive)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(
            f"failed to inspect packaged {runtime} cache (exit {result.returncode})"
        )

    entries = {
        entry.strip().replace("\\", "/")
        for entry in result.stdout.splitlines()
        if entry.strip()
    }
    required_entries = {
        f"{CACHE_DIRECTORY}/{INFO_FILE_NAME}",
        f"{CACHE_DIRECTORY}/{MANIFEST_FILE_NAME}",
        f"{FOMOD_DIRECTORY}/{FOMOD_CONFIG_FILE_NAME}",
        f"{FOMOD_DIRECTORY}/{FOMOD_INFO_FILE_NAME}",
    }
    missing_entries = sorted(required_entries - entries)
    if missing_entries:
        raise SystemExit(
            f"packaged {runtime} cache is missing required install entries: "
            f"{', '.join(missing_entries)}"
        )

    flattened_entries = sorted({INFO_FILE_NAME, MANIFEST_FILE_NAME} & entries)
    if flattened_entries:
        raise SystemExit(
            f"packaged {runtime} cache contains flattened metadata: "
            f"{', '.join(flattened_entries)}"
        )


def prepare_cache_archive(
    runtime_root: Path,
    workspace: Path,
    runtime: str,
    label: str,
    cmake: str,
) -> Path:
    """Create a validated candidate archive without changing published output."""
    archive_name = f"ShaderCache-{runtime}-{safe_label(label)}.7z"
    temporary_archive = workspace / archive_name
    fomod_dir = write_fomod_installer(runtime_root, runtime, label)
    try:
        command = [
            cmake,
            "-E",
            "tar",
            "cf",
            str(temporary_archive),
            "--format=7zip",
            CACHE_DIRECTORY,
            FOMOD_DIRECTORY,
        ]
        print("run:", " ".join(command))
        result = subprocess.run(command, cwd=runtime_root)
    finally:
        try:
            shutil.rmtree(fomod_dir)
        except OSError as exc:
            raise SystemExit(
                f"failed to remove temporary cache installer directory: {fomod_dir}"
            ) from exc

    if (
        result.returncode != 0
        or not temporary_archive.is_file()
        or temporary_archive.stat().st_size == 0
    ):
        raise SystemExit(
            f"failed to package {runtime} cache (exit {result.returncode})"
        )

    validate_cache_archive(temporary_archive, cmake, runtime)
    print(
        f"{runtime}: prepared {temporary_archive.name} "
        f"({temporary_archive.stat().st_size} bytes)"
    )
    return temporary_archive


def publish_cache_archive(candidate: Path, out_root: Path, runtime: str) -> Path:
    archive_path = archive_output_destination(out_root, candidate)
    try:
        candidate.replace(archive_path)
    except OSError as exc:
        raise SystemExit(
            f"failed to publish {runtime} cache archive: {archive_path}"
        ) from exc
    print(f"{runtime}: packaged {archive_path} ({archive_path.stat().st_size} bytes)")
    return archive_path


def build_runtime(
    stage: Path,
    workspace: Path,
    runtime: str,
    config_path: Path,
    plugin_version: str,
    jobs: int,
    fxc: str,
    compiler: tuple[str, ...],
    yaml: Any,
    write_manifest: Callable[..., int],
) -> tuple[Path, int, int]:
    runtime_root = workspace / runtime
    cache_dir = runtime_root / CACHE_DIRECTORY
    cache_dir.mkdir(parents=True, exist_ok=True)

    filtered_config = filter_profile_defines(
        config_path,
        workspace / f"config-{runtime}.yaml",
        yaml,
    )
    command = [
        *compiler,
        "--shader-dir",
        str(stage),
        "--output-dir",
        str(cache_dir),
        "--config",
        str(filtered_config),
        "--optimization-level",
        "3",
        "--suppress-warnings",
        "X1519",
        "--max-warnings",
        "999999",
        "--jobs",
        str(jobs),
        "--fxc",
        fxc,
    ]

    print("run:", " ".join(command))
    result = subprocess.run(command)
    if result.returncode != 0:
        raise SystemExit(
            f"hlslkit-compile failed for {runtime} (exit {result.returncode})"
        )

    prune_non_cache_files(cache_dir)
    imagespace_remap = remap_imagespace_dirs(cache_dir, runtime)
    write_shader_cache_manifest(
        cache_dir,
        stage,
        runtime,
        imagespace_remap,
        write_manifest,
    )

    section_count = write_info_ini(cache_dir, stage, plugin_version, runtime)
    blob_count = validate_cache(cache_dir, runtime, plugin_version)
    return runtime_root, blob_count, section_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--runtime",
        choices=["SE", "VR", "both"],
        default="both",
        help="Target runtime(s).",
    )
    parser.add_argument(
        "--source-root",
        help="Repo checkout to take shaders/configs/version from (default: this repo).",
    )
    parser.add_argument(
        "--out",
        default="dist/shader-cache",
        help="Output root for validated caches and optional archives.",
    )
    parser.add_argument(
        "--plugin-version",
        help="Override the Info.ini plugin version for one selected runtime.",
    )
    parser.add_argument(
        "--plugin-version-se",
        help="Override the SE Info.ini plugin version when building both runtimes.",
    )
    parser.add_argument(
        "--plugin-version-vr",
        help="Override the VR Info.ini plugin version when building both runtimes.",
    )
    parser.add_argument(
        "--fxc",
        help="Path to fxc.exe (default: locate it in PATH or the Windows SDK).",
    )
    parser.add_argument(
        "--jobs",
        type=positive_int,
        default=os.cpu_count() or 4,
        help="Parallel compile jobs to pass to hlslkit-compile.",
    )
    parser.add_argument(
        "--package",
        action="store_true",
        help="Create install-ready .7z archives after validation.",
    )
    parser.add_argument(
        "--package-label",
        help="Archive label (default: the runtime's plugin version).",
    )
    args = parser.parse_args()
    if args.plugin_version and (
        args.plugin_version_se or args.plugin_version_vr
    ):
        raise SystemExit(
            "--plugin-version cannot be combined with runtime-specific overrides"
        )
    if args.runtime == "both" and args.plugin_version:
        raise SystemExit(
            "--plugin-version is only valid for one runtime; use "
            "--plugin-version-se and --plugin-version-vr instead"
        )
    if args.runtime == "SE" and args.plugin_version_vr:
        raise SystemExit("--plugin-version-vr requires --runtime VR or both")
    if args.runtime == "VR" and args.plugin_version_se:
        raise SystemExit("--plugin-version-se requires --runtime SE or both")

    source_root = Path(args.source_root).resolve() if args.source_root else REPO
    if not source_root.is_dir():
        raise SystemExit(f"source root does not exist: {source_root}")
    runtimes = ["SE", "VR"] if args.runtime == "both" else [args.runtime]
    configs = configs_for(source_root)
    for runtime in runtimes:
        config_path = configs[runtime]
        if not config_path.is_file():
            raise SystemExit(f"missing validation config for {runtime}: {config_path}")

    version_overrides = {
        "SE": args.plugin_version_se,
        "VR": args.plugin_version_vr,
    }
    plugin_versions: dict[str, str] = {}
    for runtime in runtimes:
        plugin_version = (
            args.plugin_version
            or version_overrides[runtime]
            or default_plugin_version(source_root, runtime)
        )
        validate_ini_value(plugin_version, f"{runtime} plugin version")
        plugin_versions[runtime] = plugin_version

    out_root = Path(args.out).resolve()
    if source_root == out_root or source_root.is_relative_to(out_root):
        raise SystemExit("--out must not be the source root or one of its parents")
    for shader_source_root in shader_source_roots(source_root):
        resolved_shader_source_root = shader_source_root.resolve()
        if out_root == resolved_shader_source_root or out_root.is_relative_to(
            resolved_shader_source_root
        ):
            raise SystemExit(
                "--out must not be inside a shader source directory: "
                f"{resolved_shader_source_root}"
            )
    out_root.mkdir(parents=True, exist_ok=True)

    compiler, yaml, write_manifest = require_compile_tools()
    cmake = shutil.which("cmake") if args.package else None
    if args.package and not cmake:
        raise SystemExit("cmake is required when --package is enabled")
    jobs = args.jobs
    fxc = locate_fxc(args.fxc)
    print(f"using fxc.exe: {fxc}")

    with tempfile.TemporaryDirectory(prefix=".shader-cache-build-", dir=out_root) as temporary:
        workspace = Path(temporary)
        stage = workspace / "staged-shaders"
        stage_merged_shaders(source_root, stage)
        print("staged merged shader tree")

        prepared: list[tuple[str, Path, int, int, Path | None]] = []
        for runtime in runtimes:
            plugin_version = plugin_versions[runtime]
            candidate_root, blob_count, section_count = build_runtime(
                stage=stage,
                workspace=workspace,
                runtime=runtime,
                config_path=configs[runtime],
                plugin_version=plugin_version,
                jobs=jobs,
                fxc=fxc,
                compiler=compiler,
                yaml=yaml,
                write_manifest=write_manifest,
            )
            archive_candidate = None
            if args.package:
                archive_candidate = prepare_cache_archive(
                    candidate_root,
                    workspace,
                    runtime,
                    args.package_label or plugin_version,
                    cmake,
                )
            prepared.append(
                (
                    runtime,
                    candidate_root,
                    blob_count,
                    section_count,
                    archive_candidate,
                )
            )

        # Do not replace any prior output until every requested runtime has
        # compiled, validated, and (when requested) packaged successfully.
        for runtime, *_ in prepared:
            runtime_output_destination(out_root, runtime)
        for _, _, _, _, archive_candidate in prepared:
            if archive_candidate:
                archive_output_destination(out_root, archive_candidate)

        for runtime, candidate_root, blob_count, section_count, archive_candidate in prepared:
            runtime_root = publish_runtime_cache(candidate_root, out_root, runtime)
            print(
                f"{runtime}: {blob_count} cache blobs, Info.ini with "
                f"{section_count} feature sections -> {runtime_root / CACHE_DIRECTORY}"
            )
            if archive_candidate:
                publish_cache_archive(
                    archive_candidate,
                    out_root,
                    runtime,
                )

    return 0


if __name__ == "__main__":
    sys.exit(main())
