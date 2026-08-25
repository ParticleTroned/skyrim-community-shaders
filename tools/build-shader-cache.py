#!/usr/bin/env python3
"""Build a distributable shader disk cache for this repo.

Produces the layout the runtime consumes at Data/ShaderCache/:
  ShaderCache/<ShaderName>/<descriptor:HEX>.{pso,vso,cso}
  ShaderCache/Info.ini
  ShaderCache/Manifest.json

Packaged archives also contain a FOMOD installer that preserves the required
ShaderCache directory when installing through Mod Organizer 2.

The generated cache targets this repo's shipped distribution profile:
  - shipped features are treated as active
  - features hidden from the AIO package are omitted
  - WetnessEffects ships with the AIO and is treated as active
  - Horizon Fix receives separate disabled and enabled cache variants

Usage:
  python tools/build-shader-cache.py --package
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
import xml.etree.ElementTree as ET
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

TOOLS_DIRECTORY = Path(__file__).resolve().parent
if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))

from build_provenance import (
    DEFAULT_SHADER_CONTRACT_FILES,
    canonical_bytes,
    sha256_bytes,
    shader_contract_identity,
)


REPO = Path(__file__).resolve().parent.parent
CACHE_DIRECTORY = "ShaderCache"
CACHE_EXTENSIONS = frozenset({".pso", ".vso", ".cso"})
INFO_FILE_NAME = "Info.ini"
MANIFEST_FILE_NAME = "Manifest.json"
MANIFEST_SCHEMA_VERSION = 1
FOMOD_DIRECTORY = "fomod"
FOMOD_CONFIG_FILE_NAME = "ModuleConfig.xml"
FOMOD_INFO_FILE_NAME = "info.xml"
FOMOD_HELP_URL = (
    "https://github.com/ParticleTroned/skyrim-community-shaders/blob/"
    "cs-1.7-PL-SE/docs/development/prebuilt-shader-cache.md#mod-organizer-2"
)
FOMOD_HELP_IMAGE_SOURCE = Path("docs/images/mo2-fomod-use-any-file.png")
FOMOD_HELP_IMAGE_ARCHIVE_PATH = "fomod/images/mo2-fomod-use-any-file.png"
FOMOD_HELP_IMAGE_XML_PATH = FOMOD_HELP_IMAGE_ARCHIVE_PATH.replace("/", "\\")
FOMOD_NOTICE_FLAGS = (
    "CSXMO2OpenSettingsNotice",
    "CSXMO2FileCheckNotice",
    "CSXHorizonNotice",
)
FOMOD_NOTICE_MAX_LINES = 7
FOMOD_NOTICE_MAX_LINE_LENGTH = 72
CAPTURED_VARIANT_COUNT_KEY = "captured_shader_variants"
TARGET_RUNTIME = "SE"
HORIZON_FIX_SHORT_NAME = "HorizonFix"
HORIZON_FIX_CACHE_DIRECTORY = f"{CACHE_DIRECTORY}-HorizonFix"
HORIZON_FIX_DLL_PATH = "SKSE/Plugins/HorizonFix.dll"
HORIZON_FIX_SHADER_FILE = "Water.hlsl"
CSX_PLUGIN_VERSION_PATTERN = re.compile(r"^CSX (?P<version>[0-9]+\.[0-9]+)-SE$")
CSX_VERSION_FILE = Path("cmake/CSXVersion.cmake")
CSX_VERSION_DECLARATION_PATTERN = re.compile(
    r'^\s*set\(CSX_VERSION\s+"(?P<version>[0-9]+\.[0-9]+-SE)"\)\s*$',
    re.MULTILINE,
)
HIDDEN_FEATURE_PATTERN = re.compile(
    r"IsHiddenFromUserView[^\{]*\{\s*return\s+true\s*;",
    re.DOTALL,
)
FEATURE_SHORT_NAME_PATTERN = re.compile(
    r'GetShortName[^\{]*\{\s*return\s+"([^"]+)"',
    re.DOTALL,
)
FEATURE_SHORT_NAME_CONSTANT_PATTERN = re.compile(
    r'kFeatureShortName\s*=\s*"([^"]+)"',
    re.DOTALL,
)
FEATURE_SHADER_DEFINE_PATTERN = re.compile(
    r'GetShaderDefineName[^\{]*\{\s*return\s+"([^"]+)"',
    re.DOTALL,
)

# Distribution profile transforms. The source validation config remains the
# compile inventory. Feature-package inclusion is derived from the same hidden
# feature declarations used by the AIO-Release CMake preset.
GLOBAL_SHIPPED_DEFINES = ("UNIFIED_WATER",)
FILE_SHIPPED_DEFINES = {
    "Lighting.hlsl": ("WETTERNESS",),
    "Water.hlsl": ("WETTERNESS",),
}
# A clean runtime capture only records permutations exercised by that exact
# load order. Keep separately observed, SE-valid permutations in the release
# overlay so regenerating the base capture cannot make the distributed cache
# specific to one modlist. Do not add VR-only descriptors here.
CROSS_MODLIST_SHADER_VARIANTS = {
    "RunGrass.hlsl": {
        "PSHADER": {
            "Grass:Pixel:1": (),
            "Grass:Pixel:10006": ("DO_ALPHA_TEST",),
        },
        "VSHADER": {
            "Grass:Vertex:5": (),
            "Grass:Vertex:7": (),
        },
    },
}
DEBUG_PROFILE_DEFINES = {
    "DEBUG",
    "_DEBUG",
    "D3D_DEBUG_INFO",
    "D3DCOMPILE_DEBUG",
    "D3DCOMPILE_SKIP_OPTIMIZATION",
}


@dataclass(frozen=True)
class FeatureContract:
    short_name: str
    package_name: str
    shader_define: str | None


@dataclass(frozen=True)
class DistributionProfile:
    excluded_short_names: frozenset[str]
    excluded_packages: frozenset[str]
    excluded_defines: frozenset[str]
    horizon_fix_define: str


@dataclass(frozen=True)
class CacheVariant:
    name: str
    directory: str
    horizon_fix_enabled: bool


CACHE_VARIANTS = (
    CacheVariant("standard", CACHE_DIRECTORY, False),
    CacheVariant("horizon-fix", HORIZON_FIX_CACHE_DIRECTORY, True),
)

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


def feature_short_name_from_header(contents: str) -> str | None:
    match = FEATURE_SHORT_NAME_PATTERN.search(contents)
    if match:
        return match.group(1)

    match = FEATURE_SHORT_NAME_CONSTANT_PATTERN.search(contents)
    return match.group(1) if match else None


def packaged_feature_directories(source_root: Path) -> dict[str, str]:
    """Map feature short names to their package directories."""
    features_root = source_root / "features"
    if not features_root.is_dir():
        raise SystemExit(f"missing feature directory: {features_root}")

    packages: dict[str, str] = {}
    for ini_path in sorted(features_root.glob("*/Shaders/Features/*.ini")):
        short_name = ini_path.stem
        package_name = ini_path.parents[2].name
        previous = packages.setdefault(short_name, package_name)
        if previous != package_name:
            raise SystemExit(
                f"feature {short_name} is supplied by both {previous} and "
                f"{package_name}"
            )

    if not packages:
        raise SystemExit(f"no packaged feature metadata found under {features_root}")
    return packages


def feature_contracts(source_root: Path) -> dict[str, FeatureContract]:
    """Read the AIO-relevant short-name and shader-define contracts."""
    packages = packaged_feature_directories(source_root)
    headers_root = source_root / "src/Features"
    if not headers_root.is_dir():
        raise SystemExit(f"missing feature header directory: {headers_root}")

    contracts: dict[str, FeatureContract] = {}
    for header in sorted(headers_root.rglob("*.h")):
        try:
            contents = header.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            raise SystemExit(f"cannot read feature header {header}: {exc}") from exc

        short_name = feature_short_name_from_header(contents)
        if not short_name or short_name not in packages:
            continue

        define_match = FEATURE_SHADER_DEFINE_PATTERN.search(contents)
        shader_define = define_match.group(1) if define_match else None
        contract = FeatureContract(short_name, packages[short_name], shader_define)
        previous = contracts.setdefault(short_name, contract)
        if previous != contract:
            raise SystemExit(
                f"feature contract {short_name} is declared inconsistently in "
                f"{header}"
            )

    return contracts


def derive_distribution_profile(source_root: Path) -> DistributionProfile:
    """Mirror AIO-Release hidden-feature exclusion and Horizon Fix metadata."""
    packages = packaged_feature_directories(source_root)
    contracts = feature_contracts(source_root)
    headers_root = source_root / "src/Features"

    hidden_short_names: set[str] = set()
    for header in sorted(headers_root.rglob("*.h")):
        try:
            contents = header.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            raise SystemExit(f"cannot read feature header {header}: {exc}") from exc
        if not HIDDEN_FEATURE_PATTERN.search(contents):
            continue

        short_name = feature_short_name_from_header(contents)
        if not short_name:
            raise SystemExit(
                f"cannot derive the short name of hidden feature header {header}"
            )
        if short_name not in packages:
            raise SystemExit(
                f"hidden feature {short_name} has no matching packaged feature"
            )
        hidden_short_names.add(short_name)

    horizon_fix = contracts.get(HORIZON_FIX_SHORT_NAME)
    if horizon_fix is None or not horizon_fix.shader_define:
        raise SystemExit("cannot derive the Horizon Fix shader feature contract")
    if HORIZON_FIX_SHORT_NAME in hidden_short_names:
        raise SystemExit("Horizon Fix must remain in the AIO for cache auto-selection")

    water_source = source_root / "package/Shaders" / HORIZON_FIX_SHADER_FILE
    try:
        water_contents = water_source.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise SystemExit(f"cannot read Horizon Fix shader {water_source}: {exc}") from exc
    if horizon_fix.shader_define not in water_contents:
        raise SystemExit(
            f"{water_source} does not consume {horizon_fix.shader_define}"
        )

    missing_contracts = sorted(hidden_short_names - contracts.keys())
    if missing_contracts:
        raise SystemExit(
            "cannot derive hidden feature contracts: " + ", ".join(missing_contracts)
        )

    return DistributionProfile(
        excluded_short_names=frozenset(hidden_short_names),
        excluded_packages=frozenset(
            packages[short_name] for short_name in hidden_short_names
        ),
        excluded_defines=frozenset(
            contract.shader_define
            for short_name, contract in contracts.items()
            if short_name in hidden_short_names and contract.shader_define
        ),
        horizon_fix_define=horizon_fix.shader_define,
    )


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


def config_for(source_root: Path) -> Path:
    return source_root / ".github/configs/shader-validation.yaml"


def stage_merged_shaders(
    source_root: Path,
    stage: Path,
    excluded_packages: frozenset[str],
) -> None:
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
        if feature_dir.name in excluded_packages:
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


def append_cross_modlist_variants(config: dict[str, object]) -> None:
    """Merge known SE permutations that are absent from a single capture."""
    shaders = config.get("shaders")
    if not isinstance(shaders, list):
        raise SystemExit("shader config shaders must be a list")

    shaders_by_file = {
        shader.get("file"): shader
        for shader in shaders
        if isinstance(shader, dict) and isinstance(shader.get("file"), str)
    }
    for file_name, stage_additions in CROSS_MODLIST_SHADER_VARIANTS.items():
        shader = shaders_by_file.get(file_name)
        if not isinstance(shader, dict):
            raise SystemExit(
                f"shader config is missing cross-modlist source {file_name}"
            )
        stage_configs = shader.get("configs")
        if not isinstance(stage_configs, dict):
            raise SystemExit(
                f"shader config source {file_name} has no stage configs"
            )

        for stage_name, additions in stage_additions.items():
            stage_config = stage_configs.get(stage_name)
            if not isinstance(stage_config, dict):
                raise SystemExit(
                    f"shader config source {file_name} has no {stage_name} config"
                )
            entries = stage_config.get("entries")
            if not isinstance(entries, list):
                raise SystemExit(
                    f"shader config {file_name}/{stage_name} entries must be a list"
                )

            entries_by_name: dict[str, dict[str, object]] = {}
            for entry in entries:
                if not isinstance(entry, dict) or not isinstance(
                    entry.get("entry"), str
                ):
                    raise SystemExit(
                        f"shader config {file_name}/{stage_name} contains an "
                        "invalid entry"
                    )
                entry_name = entry["entry"]
                if entry_name in entries_by_name:
                    raise SystemExit(
                        f"shader config {file_name}/{stage_name} contains "
                        f"duplicate entry {entry_name}"
                    )
                entries_by_name[entry_name] = entry

            for entry_name, defines in additions.items():
                existing = entries_by_name.get(entry_name)
                if existing is not None:
                    existing_defines = existing.get("defines")
                    if (
                        not isinstance(existing_defines, list)
                        or not all(
                            isinstance(define, str) for define in existing_defines
                        )
                        or {define.strip() for define in existing_defines}
                        != {define.strip() for define in defines}
                    ):
                        raise SystemExit(
                            f"cross-modlist variant {entry_name} conflicts with "
                            "the captured shader config"
                        )
                    continue

                entry = {"entry": entry_name, "defines": list(defines)}
                entries.append(entry)
                entries_by_name[entry_name] = entry


def apply_shipped_profile_defines(
    config: object,
    excluded_defines: frozenset[str],
    variant_file_defines: dict[str, tuple[str, ...]],
) -> object:
    profile_excluded_defines = {
        normalized_define_name(define)
        for define in excluded_defines | DEBUG_PROFILE_DEFINES
    }

    def scrub(node: object) -> object:
        if isinstance(node, dict):
            return {key: scrub(value) for key, value in node.items()}
        if isinstance(node, list):
            return [
                scrub(value)
                for value in node
                if not (
                    isinstance(value, str)
                    and normalized_define_name(value) in profile_excluded_defines
                )
            ]
        return node

    config = scrub(config)
    if not isinstance(config, dict):
        return config

    append_cross_modlist_variants(config)

    common_defines = config.get("common_defines")
    if not isinstance(common_defines, list):
        raise SystemExit("shader config common_defines must be a list")
    append_missing_defines(common_defines, GLOBAL_SHIPPED_DEFINES)

    profile_file_defines = {
        file_name: (*FILE_SHIPPED_DEFINES.get(file_name, ()), *defines)
        for file_name, defines in variant_file_defines.items()
    }
    for file_name, defines in FILE_SHIPPED_DEFINES.items():
        profile_file_defines.setdefault(file_name, defines)

    file_common_defines = config.get("file_common_defines")
    if isinstance(file_common_defines, dict):
        for file_name, defines_to_add in profile_file_defines.items():
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
        defines_to_add = profile_file_defines.get(file_name)
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

    missing_files = sorted(set(profile_file_defines) - updated_files)
    if missing_files:
        raise SystemExit(
            "shader config is missing shipped-profile entries for: "
            + ", ".join(missing_files)
        )

    return config


def validate_captured_variant_count(config: dict[str, object], config_path: Path) -> None:
    """Ensure a generated inventory still contains every captured variant."""
    expected = config.get(CAPTURED_VARIANT_COUNT_KEY)
    if expected is None:
        return
    if isinstance(expected, bool) or not isinstance(expected, int) or expected < 1:
        raise SystemExit(
            f"shader config {CAPTURED_VARIANT_COUNT_KEY} must be a positive integer: "
            f"{config_path}"
        )

    shaders = config.get("shaders")
    if not isinstance(shaders, list):
        raise SystemExit(f"shader config shaders must be a list: {config_path}")

    actual = 0
    for shader in shaders:
        if not isinstance(shader, dict):
            raise SystemExit(f"shader config contains a malformed shader entry: {config_path}")
        stage_configs = shader.get("configs")
        if not isinstance(stage_configs, dict):
            raise SystemExit(f"shader config contains malformed stage configs: {config_path}")
        for stage_config in stage_configs.values():
            if not isinstance(stage_config, dict):
                raise SystemExit(f"shader config contains a malformed stage: {config_path}")
            entries = stage_config.get("entries")
            if not isinstance(entries, list):
                raise SystemExit(f"shader config stage entries must be a list: {config_path}")
            actual += len(entries)

    if actual != expected:
        raise SystemExit(
            f"shader config inventory is incomplete: {actual} variants are present, "
            f"but its clean runtime capture declared {expected}: {config_path}"
        )
    print(f"shader config: validated {actual} captured variants from {config_path}")


def filter_profile_defines(
    config_path: Path,
    out_path: Path,
    yaml: Any,
    excluded_defines: frozenset[str],
    variant_file_defines: dict[str, tuple[str, ...]],
) -> Path:
    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise SystemExit(f"shader config must be a YAML mapping: {config_path}")

    validate_captured_variant_count(config, config_path)
    config = apply_shipped_profile_defines(
        config,
        excluded_defines,
        variant_file_defines,
    )
    out_path.write_text(
        yaml.safe_dump(config, sort_keys=False),
        encoding="utf-8",
    )
    return out_path


def remap_imagespace_dirs(cache_dir: Path, runtime: str) -> dict[str, str]:
    """Move ImageSpace blobs to the technique directories used by the runtime.

    Return the destination-to-source mapping required when hashing those blobs.
    """
    by_descriptor = {
        descriptor_pair[0]: name
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
    count = write_manifest(
        cache_dir,
        shader_root,
        "",
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


def write_info_ini(
    cache_dir: Path,
    stage: Path,
    plugin_version: str,
    feature_overrides: dict[str, bool],
    shader_cache_abi: str,
) -> dict[str, bool]:
    validate_ini_value(plugin_version, "plugin version")
    validate_ini_value(shader_cache_abi, "shader cache ABI")
    lines = [
        "[Cache]",
        f"PluginVersion = {plugin_version}",
        f"ShaderCacheABI = {shader_cache_abi}",
        "",
        "",
    ]
    feature_states: dict[str, bool] = {}

    for ini_path in sorted((stage / "Features").glob("*.ini")):
        stem = ini_path.stem

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

        enabled = feature_overrides.get(stem, True)
        feature_states[stem] = enabled
        enabled_text = "true" if enabled else "false"
        lines += [
            f"[{stem}]",
            f"Enabled = {enabled_text}",
            f"Version = {version}",
            "",
            "",
        ]

    missing_overrides = sorted(set(feature_overrides) - feature_states.keys())
    if missing_overrides:
        raise SystemExit(
            "cache feature overrides are absent from the staged AIO: "
            + ", ".join(missing_overrides)
        )

    (cache_dir / INFO_FILE_NAME).write_bytes(
        b"\xef\xbb\xbf" + "\r\n".join(lines).encode("utf-8")
    )
    return feature_states


def default_plugin_version(source_root: Path) -> str:
    version_path = source_root / CSX_VERSION_FILE
    if not version_path.is_file():
        raise SystemExit(f"cannot derive plugin version from {version_path}")

    try:
        version_source = version_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"cannot read plugin version from {version_path}") from exc

    match = CSX_VERSION_DECLARATION_PATTERN.search(version_source)
    if not match:
        raise SystemExit(f"cannot parse CSX_VERSION from {version_path}")

    plugin_version = f"CSX {match.group('version')}"
    validate_csx_plugin_version(plugin_version)
    return plugin_version


def validate_csx_plugin_version(plugin_version: str) -> str:
    """Return the exact CSX SE release tag represented by a runtime version."""
    match = CSX_PLUGIN_VERSION_PATTERN.fullmatch(plugin_version)
    if not match:
        raise SystemExit(
            "plugin version must use the 'CSX <major>.<minor>-SE' format"
        )
    return f"CSX{match.group('version')}-SE"


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
    plugin_version: str,
    expected_feature_states: dict[str, bool],
    shader_cache_abi: str,
) -> int:
    """Fail before packaging if the cache is incomplete or malformed."""
    info_path = cache_dir / INFO_FILE_NAME
    manifest_path = cache_dir / MANIFEST_FILE_NAME
    if not info_path.is_file():
        raise SystemExit(f"SE: missing {info_path}")
    if not manifest_path.is_file():
        raise SystemExit(f"SE: missing {manifest_path}")

    info = configparser.ConfigParser(interpolation=None)
    try:
        with info_path.open("r", encoding="utf-8-sig") as stream:
            info.read_file(stream)
    except (configparser.Error, OSError, UnicodeError) as exc:
        raise SystemExit(f"SE: invalid {info_path}: {exc}") from exc

    actual_version = info.get("Cache", "PluginVersion", fallback=None)
    if actual_version != plugin_version:
        raise SystemExit(
            f"SE: Info.ini plugin version is {actual_version!r}; "
            f"expected {plugin_version!r}"
        )

    actual_shader_cache_abi = info.get("Cache", "ShaderCacheABI", fallback=None)
    if actual_shader_cache_abi != shader_cache_abi:
        raise SystemExit(
            f"SE: Info.ini shader cache ABI is {actual_shader_cache_abi!r}; "
            f"expected {shader_cache_abi!r}"
        )

    actual_feature_names = set(info.sections()) - {"Cache"}
    expected_feature_names = set(expected_feature_states)
    if actual_feature_names != expected_feature_names:
        missing = sorted(expected_feature_names - actual_feature_names)
        unexpected = sorted(actual_feature_names - expected_feature_names)
        raise SystemExit(
            "SE: Info.ini feature set does not match the staged AIO; "
            f"missing={missing}, unexpected={unexpected}"
        )
    for feature_name, expected_enabled in expected_feature_states.items():
        actual_enabled = info.getboolean(
            feature_name,
            "Enabled",
            fallback=None,
        )
        if actual_enabled is not expected_enabled:
            raise SystemExit(
                f"SE: Info.ini {feature_name}/Enabled is {actual_enabled!r}; "
                f"expected {expected_enabled!r}"
            )

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"SE: invalid {manifest_path}: {exc}") from exc

    if not isinstance(manifest, dict) or manifest.get(
        "schemaVersion"
    ) != MANIFEST_SCHEMA_VERSION or not isinstance(manifest.get("entries"), dict):
        raise SystemExit("SE: unsupported or malformed cache manifest")
    entries: dict[str, object] = manifest["entries"]

    blob_paths = sorted(
        path
        for path in cache_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in CACHE_EXTENSIONS
    )
    if not blob_paths:
        raise SystemExit("SE: cache contains no compiled shader blobs")

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
        elif not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{32}", digest):
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
            f"SE: {len(missing_entries)} blobs are absent from Manifest.json; "
            f"first: {', '.join(missing_entries[:5])}"
        )
    if invalid_entries:
        raise SystemExit(
            f"SE: {len(invalid_entries)} manifest digests are invalid; "
            f"first: {', '.join(invalid_entries[:5])}"
        )
    if invalid_blobs:
        raise SystemExit(
            f"SE: {len(invalid_blobs)} files are not valid DXBC containers; "
            f"first: {', '.join(invalid_blobs[:5])}"
        )

    unexpected_entries = sorted(set(entries) - blob_keys)
    if unexpected_entries:
        raise SystemExit(
            f"SE: Manifest.json contains {len(unexpected_entries)} entries "
            f"without a compiled blob; first: {', '.join(unexpected_entries[:5])}"
        )

    print(
        f"SE: validated {len(blob_paths)} DXBC blobs and "
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


def remove_publication_staging(path: Path) -> None:
    """Remove only a builder-owned, unpublished staging path."""
    if not path_entry_exists(path):
        return
    if path.is_symlink() or path.is_file():
        path.unlink()
    else:
        shutil.rmtree(path)


def discard_publication_staging(path: Path) -> None:
    """Best-effort cleanup that never masks the publication failure."""
    try:
        remove_publication_staging(path)
    except OSError:
        pass


def copy_publication_candidate(source: Path, staging: Path, label: str) -> None:
    """Copy validated output so it inherits the destination parent's ACL.

    Python creates TemporaryDirectory workspaces with a private ACL on Windows.
    Moving a candidate out of that workspace preserves the private ACL and can
    leave an elevated build readable only from an Administrator shell. A new
    path created beneath the output root inherits the output root's ACL instead.
    """
    if path_entry_exists(staging):
        raise SystemExit(
            f"refusing to replace unexpected publication staging path: {staging}"
        )

    try:
        if source.is_dir():
            shutil.copytree(source, staging)
        else:
            shutil.copy2(source, staging)
    except OSError as exc:
        discard_publication_staging(staging)
        raise SystemExit(f"failed to stage {label} for publication: {staging}") from exc


def publish_runtime_cache(candidate_root: Path, out_root: Path, runtime: str) -> Path:
    """Publish a fully validated cache while preserving the previous cache on failure."""
    destination = runtime_output_destination(out_root, runtime)
    staging = out_root / f".{runtime}.publishing"
    copy_publication_candidate(candidate_root, staging, f"{runtime} cache")

    if not path_entry_exists(destination):
        try:
            staging.replace(destination)
        except OSError as exc:
            discard_publication_staging(staging)
            raise SystemExit(f"failed to publish {runtime} cache: {destination}") from exc
        return destination

    backup = candidate_root.parent / f"{runtime}.previous"
    if backup.exists():
        discard_publication_staging(staging)
        raise RuntimeError(f"unexpected temporary backup already exists: {backup}")

    try:
        destination.replace(backup)
    except OSError as exc:
        discard_publication_staging(staging)
        raise SystemExit(
            f"could not move the existing {runtime} cache aside: {destination}"
        ) from exc
    try:
        staging.replace(destination)
    except OSError as exc:
        try:
            backup.replace(destination)
        except OSError as restore_error:
            discard_publication_staging(staging)
            raise SystemExit(
                f"failed to publish {runtime} cache and could not restore "
                f"the previous output: {destination}"
            ) from restore_error
        discard_publication_staging(staging)
        raise SystemExit(f"failed to publish {runtime} cache: {destination}") from exc

    # The old cache remains inside the isolated temporary workspace until it
    # is cleaned up after this invocation. It is never recursively deleted
    # from the user-selected output root.
    return destination


def validate_fomod_installer(
    fomod_dir: Path,
    compatibility_tag: str,
) -> None:
    """Verify the installer explains compatibility and permits manual fallback."""
    config_path = fomod_dir / FOMOD_CONFIG_FILE_NAME
    info_path = fomod_dir / FOMOD_INFO_FILE_NAME
    try:
        root = ET.parse(config_path).getroot()
        info_root = ET.parse(info_path).getroot()
    except (ET.ParseError, OSError) as exc:
        raise SystemExit(f"invalid cache FOMOD metadata in {fomod_dir}: {exc}") from exc

    if root.find("./moduleDependencies") is not None:
        raise SystemExit(
            "cache FOMOD must expose compatibility failures inside its visible "
            "install step"
        )

    install_steps = root.findall("./installSteps/installStep")
    plugins = root.findall(
        "./installSteps/installStep/optionalFileGroups/group/plugins/plugin"
    )
    notice_plugins: dict[str, ET.Element] = {}
    for install_step in install_steps:
        step_notices: list[tuple[str, ET.Element]] = []
        for plugin in install_step.findall(
            "./optionalFileGroups/group/plugins/plugin"
        ):
            for flag in plugin.findall("./conditionFlags/flag"):
                flag_name = flag.get("name", "")
                if flag_name in FOMOD_NOTICE_FLAGS:
                    step_notices.append((flag_name, plugin))
        if len(step_notices) > 1:
            raise SystemExit(
                "cache FOMOD must show each setup notice on a separate page"
            )
        for flag_name, plugin in step_notices:
            if flag_name in notice_plugins:
                raise SystemExit(f"cache FOMOD repeats setup notice {flag_name}")
            notice_plugins[flag_name] = plugin

    cache_plugins = [
        plugin for plugin in plugins if plugin.find("./files/folder") is not None
    ]
    cache_groups = [
        group
        for group in root.findall(
            "./installSteps/installStep/optionalFileGroups/group"
        )
        if group.find("./plugins/plugin/files/folder") is not None
    ]
    if set(notice_plugins) != set(FOMOD_NOTICE_FLAGS) or len(
        cache_plugins
    ) != len(CACHE_VARIANTS):
        raise SystemExit(
            "cache FOMOD is missing its visible setup notices or cache variants"
        )
    if len(cache_groups) != 1 or cache_groups[0].get("type") != "SelectExactlyOne":
        raise SystemExit(
            "cache FOMOD must require exactly one selectable cache variant"
        )

    cache_plugins_by_source = {
        plugin.find("./files/folder").get("source", ""): plugin
        for plugin in cache_plugins
    }
    expected_cache_sources = {variant.directory for variant in CACHE_VARIANTS}
    if set(cache_plugins_by_source) != expected_cache_sources:
        raise SystemExit(
            "cache FOMOD sources do not match the generated cache variants"
        )
    cache_sources_in_order = [
        plugin.find("./files/folder").get("source", "")
        for plugin in cache_plugins
    ]
    if cache_sources_in_order[0] != HORIZON_FIX_CACHE_DIRECTORY:
        raise SystemExit(
            "cache FOMOD must default its manual fallback to Horizon Fix"
        )

    for flag_name, notice_plugin in notice_plugins.items():
        notice_type = notice_plugin.find("./typeDescriptor/type")
        if notice_type is None or notice_type.get("name") != "Required":
            raise SystemExit(
                f"cache FOMOD must present setup notice {flag_name} as required"
            )
        notice_description = notice_plugin.findtext("./description") or ""
        notice_lines = notice_description.splitlines()
        if len(notice_lines) > FOMOD_NOTICE_MAX_LINES or any(
            len(line) > FOMOD_NOTICE_MAX_LINE_LENGTH for line in notice_lines
        ):
            raise SystemExit(
                f"cache FOMOD setup notice {flag_name} is too long for MO2's "
                "description pane"
            )

    help_image = notice_plugins["CSXMO2FileCheckNotice"].find("./image")
    if (
        help_image is None
        or help_image.get("path", "").replace("\\", "/")
        != FOMOD_HELP_IMAGE_ARCHIVE_PATH
    ):
        raise SystemExit("cache FOMOD is missing its MO2 setting help image")
    help_image_path = fomod_dir.parent / Path(
        *FOMOD_HELP_IMAGE_ARCHIVE_PATH.split("/")
    )
    if not help_image_path.is_file():
        raise SystemExit(f"cache FOMOD help image does not exist: {help_image_path}")

    if info_root.findtext("./Website") != FOMOD_HELP_URL:
        raise SystemExit("cache FOMOD is missing its detailed setup help link")

    descriptions = "\n".join(
        filter(
            None,
            (
                info_root.findtext("./Description"),
                *(
                    notice_plugins[flag_name].findtext("./description")
                    for flag_name in FOMOD_NOTICE_FLAGS
                ),
                *(
                    plugin.findtext("./description")
                    for plugin in cache_plugins
                ),
            ),
        )
    )
    required_guidance = (
        "Settings",
        "Plugins",
        "Fomod Installer",
        "use_any_file",
        "true",
        "reinstall this shader-cache FOMOD",
    )
    missing_guidance = [text for text in required_guidance if text not in descriptions]
    if missing_guidance:
        raise SystemExit(
            "cache FOMOD is missing MO2 setup guidance: "
            + ", ".join(missing_guidance)
        )

    core_dependency = (
        f"SKSE/Plugins/CommunityShaders/{compatibility_tag}.marker",
        "Active",
    )
    horizon_dependency = HORIZON_FIX_DLL_PATH.replace("\\", "/")
    expected_patterns = {
        CACHE_DIRECTORY: {
            frozenset({core_dependency, (horizon_dependency, "Missing")}),
            frozenset({core_dependency, (horizon_dependency, "Inactive")}),
        },
        HORIZON_FIX_CACHE_DIRECTORY: {
            frozenset({core_dependency, (horizon_dependency, "Active")}),
        },
    }

    for source, cache_plugin in cache_plugins_by_source.items():
        dependency_type = cache_plugin.find("./typeDescriptor/dependencyType")
        default_type = (
            dependency_type.find("./defaultType")
            if dependency_type is not None
            else None
        )
        patterns = (
            dependency_type.findall("./patterns/pattern")
            if dependency_type is not None
            else []
        )
        actual_patterns: set[frozenset[tuple[str, str]]] = set()
        for pattern in patterns:
            matched_type = pattern.find("./type")
            if matched_type is None or matched_type.get("name") != "Recommended":
                raise SystemExit(
                    f"cache FOMOD variant {source} has a non-recommended match"
                )
            actual_patterns.add(
                frozenset(
                    (
                        dependency.get("file", "").replace("\\", "/"),
                        dependency.get("state", ""),
                    )
                    for dependency in pattern.findall(
                        "./dependencies/fileDependency"
                    )
                )
            )

        if default_type is None or default_type.get("name") != "Optional":
            raise SystemExit(
                f"cache FOMOD variant {source} must remain manually selectable"
            )
        if actual_patterns != expected_patterns[source]:
            raise SystemExit(
                f"cache FOMOD variant {source} has incorrect compatibility rules"
            )

        install_folder = cache_plugin.find("./files/folder")
        if install_folder is None or (
            install_folder.get("source") != source
            or install_folder.get("destination") != CACHE_DIRECTORY
        ):
            raise SystemExit(
                f"cache FOMOD variant {source} does not install at Data/ShaderCache"
            )


def write_fomod_installer(
    runtime_root: Path,
    source_root: Path,
    runtime: str,
    label: str,
    compatibility_tag: str,
) -> Path:
    """Stage an installer with automatic recommendations and manual fallback."""
    fomod_dir = runtime_root / FOMOD_DIRECTORY
    if path_entry_exists(fomod_dir):
        raise SystemExit(
            f"refusing to replace unexpected cache installer directory: {fomod_dir}"
        )

    help_image_source = source_root / FOMOD_HELP_IMAGE_SOURCE
    if not help_image_source.is_file():
        raise SystemExit(f"missing cache FOMOD help image: {help_image_source}")

    fomod_dir.mkdir()
    display_label = safe_label(label)
    module_name = f"CSX {runtime} Shader Cache - {compatibility_tag}"
    description = (
        f"Requires active {compatibility_tag}. MO2 users must set Settings > "
        "Plugins > Fomod Installer > use_any_file to true for automatic checks. "
        "Verify the selectable Horizon Fix profile before installing. Reinstall "
        "after changing Horizon Fix. Click Website for full setup instructions."
    )
    open_settings_description = (
        "MO2 SETUP - PAGE 1 OF 2\n\n"
        "Close this installer.\n\n"
        "Open Tools > Settings > Plugins.\n"
        "In the LEFT list, select Fomod Installer."
    )
    file_check_description = (
        "MO2 SETUP - PAGE 2 OF 2\n\n"
        "In the RIGHT settings table:\n"
        "1. Find use_any_file.\n"
        "2. Double-click its value and change false to true.\n"
        f"3. Click OK, enable {compatibility_tag}, then reopen this installer.\n"
        "Click the image below to enlarge it."
    )
    horizon_notice_description = (
        "HORIZON FIX CACHE\n\n"
        "The installer recommends a profile when file checks work.\n"
        "Both profiles stay selectable for a manual correction.\n\n"
        "IMPORTANT: reinstall this shader-cache FOMOD after enabling\n"
        "or disabling Horizon Fix."
    )
    standard_description = (
        f"Choose this only when {compatibility_tag} is active and HorizonFix.dll "
        "is missing or inactive. Its Water shaders are compiled without Horizon "
        "Fix compatibility. Reinstall this shader-cache FOMOD after enabling "
        "Horizon Fix."
    )
    horizon_description = (
        f"Choose this when {compatibility_tag} and SKSE\\Plugins\\HorizonFix.dll "
        "are active. Its Water shaders are compiled with Horizon Fix "
        "compatibility. This is the fallback default when MO2 cannot detect "
        "non-plugin files. Reinstall after disabling Horizon Fix."
    )
    module_config = f"""<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
        xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
  <moduleName>{module_name}</moduleName>
  <installSteps order="Explicit">
    <installStep name="MO2 setup 1/2: open Plugins settings">
      <optionalFileGroups order="Explicit">
        <group name="Required MO2 navigation" type="SelectAll">
          <plugins order="Explicit">
            <plugin name="1. Open MO2 Settings > Plugins">
              <description>{open_settings_description}</description>
              <conditionFlags>
                <flag name="CSXMO2OpenSettingsNotice">shown</flag>
              </conditionFlags>
              <typeDescriptor>
                <type name="Required" />
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
    <installStep name="MO2 setup 2/2: enable non-plugin file checks">
      <optionalFileGroups order="Explicit">
        <group name="Required FOMOD Installer setting" type="SelectAll">
          <plugins order="Explicit">
            <plugin name="2. Set use_any_file to true">
              <description>{file_check_description}</description>
              <image path="{FOMOD_HELP_IMAGE_XML_PATH}" />
              <conditionFlags>
                <flag name="CSXMO2FileCheckNotice">shown</flag>
              </conditionFlags>
              <typeDescriptor>
                <type name="Required" />
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
    <installStep name="Horizon Fix profile notice">
      <optionalFileGroups order="Explicit">
        <group name="Required profile notice" type="SelectAll">
          <plugins order="Explicit">
            <plugin name="Reinstall after changing Horizon Fix">
              <description>{horizon_notice_description}</description>
              <conditionFlags>
                <flag name="CSXHorizonNotice">shown</flag>
              </conditionFlags>
              <typeDescriptor>
                <type name="Required" />
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
    <installStep name="Choose the active Horizon Fix profile">
      <optionalFileGroups order="Explicit">
        <group name="Select exactly one installed Horizon Fix state" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Horizon Fix cache: HorizonFix.dll active">
              <description>{horizon_description}</description>
              <files>
                <folder source="{HORIZON_FIX_CACHE_DIRECTORY}"
                        destination="{CACHE_DIRECTORY}"
                        priority="0" />
              </files>
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="Optional" />
                  <patterns order="Explicit">
                    <pattern>
                      <dependencies operator="And">
                        <fileDependency
                          file="SKSE\\Plugins\\CommunityShaders\\{compatibility_tag}.marker"
                          state="Active" />
                        <fileDependency
                          file="SKSE\\Plugins\\HorizonFix.dll"
                          state="Active" />
                      </dependencies>
                      <type name="Recommended" />
                    </pattern>
                  </patterns>
                </dependencyType>
              </typeDescriptor>
            </plugin>
            <plugin name="Standard cache: Horizon Fix inactive">
              <description>{standard_description}</description>
              <files>
                <folder source="{CACHE_DIRECTORY}"
                        destination="{CACHE_DIRECTORY}"
                        priority="0" />
              </files>
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="Optional" />
                  <patterns order="Explicit">
                    <pattern>
                      <dependencies operator="And">
                        <fileDependency
                          file="SKSE\\Plugins\\CommunityShaders\\{compatibility_tag}.marker"
                          state="Active" />
                        <fileDependency
                          file="SKSE\\Plugins\\HorizonFix.dll"
                          state="Missing" />
                      </dependencies>
                      <type name="Recommended" />
                    </pattern>
                    <pattern>
                      <dependencies operator="And">
                        <fileDependency
                          file="SKSE\\Plugins\\CommunityShaders\\{compatibility_tag}.marker"
                          state="Active" />
                        <fileDependency
                          file="SKSE\\Plugins\\HorizonFix.dll"
                          state="Inactive" />
                      </dependencies>
                      <type name="Recommended" />
                    </pattern>
                  </patterns>
                </dependencyType>
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
</config>
"""
    info = f"""<?xml version="1.0" encoding="UTF-8"?>
<fomod>
  <Name>{module_name}</Name>
  <Author>Community Shaders Expanded</Author>
  <Version>{display_label}</Version>
  <Description>{description}</Description>
  <Website>{FOMOD_HELP_URL}</Website>
</fomod>
"""

    try:
        help_image_destination = runtime_root / Path(
            *FOMOD_HELP_IMAGE_ARCHIVE_PATH.split("/")
        )
        help_image_destination.parent.mkdir()
        shutil.copy2(help_image_source, help_image_destination)
        (fomod_dir / FOMOD_CONFIG_FILE_NAME).write_text(
            module_config, encoding="utf-8", newline="\n"
        )
        (fomod_dir / FOMOD_INFO_FILE_NAME).write_text(
            info, encoding="utf-8", newline="\n"
        )
        validate_fomod_installer(fomod_dir, compatibility_tag)
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
        f"{HORIZON_FIX_CACHE_DIRECTORY}/{INFO_FILE_NAME}",
        f"{HORIZON_FIX_CACHE_DIRECTORY}/{MANIFEST_FILE_NAME}",
        f"{FOMOD_DIRECTORY}/{FOMOD_CONFIG_FILE_NAME}",
        f"{FOMOD_DIRECTORY}/{FOMOD_INFO_FILE_NAME}",
        FOMOD_HELP_IMAGE_ARCHIVE_PATH,
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
    source_root: Path,
    workspace: Path,
    runtime: str,
    label: str,
    compatibility_tag: str,
    cmake: str,
) -> Path:
    """Create a validated candidate archive without changing published output."""
    archive_name = f"ShaderCache-{runtime}-{safe_label(label)}.7z"
    temporary_archive = workspace / archive_name
    fomod_dir = write_fomod_installer(
        runtime_root,
        source_root,
        runtime,
        label,
        compatibility_tag,
    )
    try:
        command = [
            cmake,
            "-E",
            "tar",
            "cf",
            str(temporary_archive),
            "--format=7zip",
            CACHE_DIRECTORY,
            HORIZON_FIX_CACHE_DIRECTORY,
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
    staging = out_root / f".{candidate.name}.publishing"
    copy_publication_candidate(candidate, staging, f"{runtime} cache archive")
    try:
        staging.replace(archive_path)
    except OSError as exc:
        discard_publication_staging(staging)
        raise SystemExit(
            f"failed to publish {runtime} cache archive: {archive_path}"
        ) from exc
    print(f"{runtime}: packaged {archive_path} ({archive_path.stat().st_size} bytes)")
    return archive_path


def validate_horizon_variant_delta(
    standard_cache: Path,
    horizon_cache: Path,
    standard_feature_states: dict[str, bool],
    horizon_feature_states: dict[str, bool],
) -> None:
    """Ensure Horizon Fix changes only its state and Water shader artifacts."""
    expected_horizon_states = dict(standard_feature_states)
    expected_horizon_states[HORIZON_FIX_SHORT_NAME] = True
    if standard_feature_states.get(HORIZON_FIX_SHORT_NAME) is not False:
        raise SystemExit("standard cache must record Horizon Fix disabled")
    if horizon_feature_states != expected_horizon_states:
        raise SystemExit(
            "Horizon Fix cache feature metadata differs outside HorizonFix/Enabled"
        )

    def cache_blobs(cache_dir: Path) -> dict[str, Path]:
        return {
            path.relative_to(cache_dir).as_posix(): path
            for path in cache_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in CACHE_EXTENSIONS
        }

    standard_blobs = cache_blobs(standard_cache)
    horizon_blobs = cache_blobs(horizon_cache)
    if standard_blobs.keys() != horizon_blobs.keys():
        missing = sorted(standard_blobs.keys() - horizon_blobs.keys())
        unexpected = sorted(horizon_blobs.keys() - standard_blobs.keys())
        raise SystemExit(
            "Horizon Fix cache changed the permutation inventory; "
            f"missing={missing[:5]}, unexpected={unexpected[:5]}"
        )

    changed_blobs = {
        relative_path
        for relative_path, standard_path in standard_blobs.items()
        if standard_path.read_bytes() != horizon_blobs[relative_path].read_bytes()
    }
    water_prefix = f"{Path(HORIZON_FIX_SHADER_FILE).stem}/"
    unexpected_changes = sorted(
        path for path in changed_blobs if not path.startswith(water_prefix)
    )
    if unexpected_changes:
        raise SystemExit(
            "Horizon Fix altered non-Water shader blobs; first: "
            + ", ".join(unexpected_changes[:5])
        )
    if not changed_blobs:
        raise SystemExit("Horizon Fix did not alter any compiled Water shader blobs")

    for cache_dir in (standard_cache, horizon_cache):
        manifest = json.loads(
            (cache_dir / MANIFEST_FILE_NAME).read_text(encoding="utf-8")
        )
        if set(manifest["entries"]) != set(standard_blobs):
            raise SystemExit(
                f"Horizon variant manifest does not match its blobs: {cache_dir}"
            )

    print(
        f"SE: Horizon Fix variant changes {len(changed_blobs)} Water blobs and "
        "no unrelated shader blobs"
    )


def build_runtime(
    source_root: Path,
    stage: Path,
    workspace: Path,
    runtime: str,
    config_path: Path,
    plugin_version: str,
    profile: DistributionProfile,
    jobs: int,
    fxc: str,
    compiler: tuple[str, ...],
    yaml: Any,
    write_manifest: Callable[..., int],
) -> tuple[Path, dict[str, int], int]:
    runtime_root = workspace / runtime
    build_results: dict[str, tuple[Path, int, dict[str, bool]]] = {}
    shader_contract = shader_contract_identity(
        source_root, DEFAULT_SHADER_CONTRACT_FILES, runtime
    )
    shader_cache_abi = sha256_bytes(canonical_bytes(shader_contract))
    excluded_defines = profile.excluded_defines | {
        profile.horizon_fix_define
    }

    for variant in CACHE_VARIANTS:
        cache_dir = runtime_root / variant.directory
        cache_dir.mkdir(parents=True, exist_ok=True)
        variant_file_defines = (
            {HORIZON_FIX_SHADER_FILE: (profile.horizon_fix_define,)}
            if variant.horizon_fix_enabled
            else {}
        )
        filtered_config = filter_profile_defines(
            config_path,
            workspace / f"config-{runtime}-{variant.name}.yaml",
            yaml,
            excluded_defines,
            variant_file_defines,
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
                f"hlslkit-compile failed for {runtime}/{variant.name} "
                f"(exit {result.returncode})"
            )

        prune_non_cache_files(cache_dir)
        imagespace_remap = remap_imagespace_dirs(
            cache_dir,
            f"{runtime}/{variant.name}",
        )
        write_shader_cache_manifest(
            cache_dir,
            stage,
            f"{runtime}/{variant.name}",
            imagespace_remap,
            write_manifest,
        )

        feature_states = write_info_ini(
            cache_dir,
            stage,
            plugin_version,
            {HORIZON_FIX_SHORT_NAME: variant.horizon_fix_enabled},
            shader_cache_abi,
        )
        blob_count = validate_cache(
            cache_dir,
            plugin_version,
            feature_states,
            shader_cache_abi,
        )
        build_results[variant.name] = (cache_dir, blob_count, feature_states)

    standard_cache, _, standard_states = build_results["standard"]
    horizon_cache, _, horizon_states = build_results["horizon-fix"]
    validate_horizon_variant_delta(
        standard_cache,
        horizon_cache,
        standard_states,
        horizon_states,
    )

    feature_counts = {len(result[2]) for result in build_results.values()}
    if len(feature_counts) != 1:
        raise SystemExit("shader-cache variants contain different feature counts")
    blob_counts = {
        variant_name: result[1]
        for variant_name, result in build_results.items()
    }
    return runtime_root, blob_counts, feature_counts.pop()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
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
        help=(
            "Override the Info.ini CSX version using "
            "'CSX <major>.<minor>-SE'."
        ),
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
        help="Archive label (default: the CSX compatibility tag).",
    )
    args = parser.parse_args()

    source_root = Path(args.source_root).resolve() if args.source_root else REPO
    if not source_root.is_dir():
        raise SystemExit(f"source root does not exist: {source_root}")
    config_path = config_for(source_root)
    if not config_path.is_file():
        raise SystemExit(f"missing SE validation config: {config_path}")

    plugin_version = args.plugin_version or default_plugin_version(source_root)
    validate_ini_value(plugin_version, "SE plugin version")
    compatibility_tag = validate_csx_plugin_version(plugin_version)
    profile = derive_distribution_profile(source_root)
    print(
        "AIO profile: excluding hidden features "
        + ", ".join(sorted(profile.excluded_short_names))
    )

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
        stage_merged_shaders(
            source_root,
            stage,
            profile.excluded_packages,
        )
        print("staged merged shader tree")

        candidate_root, blob_counts, section_count = build_runtime(
            source_root=source_root,
            stage=stage,
            workspace=workspace,
            runtime=TARGET_RUNTIME,
            config_path=config_path,
            plugin_version=plugin_version,
            profile=profile,
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
                source_root,
                workspace,
                TARGET_RUNTIME,
                args.package_label or compatibility_tag,
                compatibility_tag,
                cmake,
            )

        # Validate every destination before replacing any prior output.
        runtime_output_destination(out_root, TARGET_RUNTIME)
        if archive_candidate:
            archive_output_destination(out_root, archive_candidate)

        runtime_root = publish_runtime_cache(
            candidate_root,
            out_root,
            TARGET_RUNTIME,
        )
        print(
            "SE: "
            + ", ".join(
                f"{variant_name}={blob_count} cache blobs"
                for variant_name, blob_count in blob_counts.items()
            )
            + f", each Info.ini has {section_count} feature sections -> "
            + str(runtime_root)
        )
        if archive_candidate:
            publish_cache_archive(
                archive_candidate,
                out_root,
                TARGET_RUNTIME,
            )

    return 0


if __name__ == "__main__":
    sys.exit(main())
