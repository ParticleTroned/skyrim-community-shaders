#!/usr/bin/env python3
"""Build a distributable shader disk cache for this repo.

Produces the managed layout the runtime consumes at Data/ShaderCache/:
  ShaderCache/Optimized.{A,B}.csxpack
  ShaderCache/Developer.{A,B}.csxpack
  ShaderCache/Info.ini
  ShaderCache/PackManifest.json

Packaged archives contain the raw cache variants consumed by the release AIO
FOMOD assembler. They intentionally contain no installer or runtime detection.

The default cache targets this repo's shipped distribution profile. Named
profiles can preserve a maintainer-approved tester feature set without changing
the default release behavior. Shipped SE and VR builds compile compatibility
variants into one pack; runtime selects them without an installer choice.

Usage:
  python tools/build-shader-cache.py --runtime both --package
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import struct
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
PACK_MANIFEST_FILE_NAME = "PackManifest.json"
PACK_FILE_NAMES = (
    "Optimized.A.csxpack",
    "Optimized.B.csxpack",
    "Developer.A.csxpack",
    "Developer.B.csxpack",
)
PACK_FORMAT_VERSION = 1
COMPATIBILITY_VARIANTS_FILE = Path("config/shader-compatibility-variants.json")
FOMOD_DIRECTORY = "fomod"
FOMOD_CONFIG_FILE_NAME = "ModuleConfig.xml"
FOMOD_INFO_FILE_NAME = "info.xml"
FOMOD_HELP_URL = (
    "https://github.com/ParticleTroned/skyrim-community-shaders/blob/"
    "main-VR/docs/development/prebuilt-shader-cache.md#mod-organizer-2"
)
FOMOD_HELP_IMAGE_SOURCE = Path("docs/images/mo2-fomod-use-any-file.png")
FOMOD_HELP_IMAGE_ARCHIVE_PATH = "fomod/images/mo2-fomod-use-any-file.png"
FOMOD_HELP_IMAGE_XML_PATH = FOMOD_HELP_IMAGE_ARCHIVE_PATH.replace("/", "\\")
FOMOD_BASE_NOTICE_FLAGS = (
    "CSXMO2OpenSettingsNotice",
    "CSXMO2FileCheckNotice",
)
FOMOD_HORIZON_NOTICE_FLAG = "CSXHorizonNotice"
FOMOD_NOTICE_MAX_LINES = 7
FOMOD_NOTICE_MAX_LINE_LENGTH = 72
PUBLICATION_REPLACE_ATTEMPTS = 20
PUBLICATION_REPLACE_RETRY_SECONDS = 0.5
CAPTURED_VARIANT_COUNT_KEY = "captured_shader_variants"
HORIZON_FIX_SHORT_NAME = "HorizonFix"
HORIZON_FIX_CACHE_DIRECTORY = f"{CACHE_DIRECTORY}-HorizonFix"
HORIZON_FIX_SHADER_FILE = "Water.hlsl"
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


RUNTIME_EXCLUDED_FEATURES = {
    "SE": {"VR"},
    "VR": set(),
}

# Distribution profile transforms. The source validation configs are still
# useful as compile inventories, but cache profiles select the feature state
# represented by the compiled bytecode and Info.ini metadata.
NON_SHIPPED_FEATURES = {
    "WetnessEffects": {
        "define": "WETNESS_EFFECTS",
        "package": "Wetness Effects",
    },
}
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
NON_SHIPPED_PACKAGES = {
    feature["package"]
    for feature in NON_SHIPPED_FEATURES.values()
}

# A clean SE capture records only the permutations exercised by that load
# order. Keep separately observed SE-valid RunGrass permutations in a release
# overlay so the distributed cache remains useful across modlists. These are
# deliberately never added to the VR inventory.
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


@dataclass(frozen=True)
class CacheProfile:
    name: str
    display_name: str
    supported_runtimes: frozenset[str]
    disabled_features: frozenset[str]
    excluded_defines: frozenset[str]
    global_defines: tuple[str, ...]
    file_defines: dict[str, tuple[str, ...]]


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


STANDARD_CACHE_VARIANT = CacheVariant("standard", CACHE_DIRECTORY, False)
HORIZON_FIX_CACHE_VARIANT = CacheVariant(
    "horizon-fix",
    HORIZON_FIX_CACHE_DIRECTORY,
    True,
)
CACHE_VARIANTS = (
    STANDARD_CACHE_VARIANT,
    HORIZON_FIX_CACHE_VARIANT,
)


def compatibility_variant_manifest(source_root: Path) -> dict[str, dict[str, Any]]:
    path = source_root / COMPATIBILITY_VARIANTS_FILE
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid shader compatibility variant manifest {path}: {exc}") from exc
    if (
        not isinstance(document, dict)
        or document.get("schema") != "csx.shader.compatibility.variants"
        or document.get("schemaVersion") != 1
        or not isinstance(document.get("variants"), list)
    ):
        raise SystemExit(f"unsupported shader compatibility variant manifest: {path}")
    variants: dict[str, dict[str, Any]] = {}
    for variant in document["variants"]:
        if not isinstance(variant, dict) or not isinstance(variant.get("id"), str):
            raise SystemExit(f"malformed shader compatibility variant in {path}")
        if variant["id"] in variants:
            raise SystemExit(f"duplicate shader compatibility variant {variant['id']!r}")
        if not isinstance(variant.get("registrations"), list) or not isinstance(
            variant.get("shaderDefinesBySource"), dict
        ):
            raise SystemExit(f"malformed shader compatibility variant {variant['id']!r}")
        variants[variant["id"]] = variant
    if "default" not in variants or "legacy-horizon-fix" not in variants:
        raise SystemExit("compatibility manifest must define default and legacy-horizon-fix")
    return variants


def canonical_compatibility_registration(registration: dict[str, Any]) -> str:
    required = (
        "identity",
        "contractMajor",
        "currentMinor",
        "minimumCompatibleMinor",
        "maximumCompatibleMinor",
        "resourceFingerprint",
        "scopes",
    )
    if any(field not in registration for field in required):
        raise SystemExit("compatibility registration is missing required fields")
    identity = registration["identity"]
    if (
        not isinstance(identity, str)
        or not identity
        or len(identity) > 128
        or identity.startswith(".")
        or identity.endswith(".")
        or any(character not in "abcdefghijklmnopqrstuvwxyz0123456789._-" for character in identity)
    ):
        raise SystemExit(f"invalid compatibility identity {identity!r}")
    major = registration["contractMajor"]
    current = registration["currentMinor"]
    minimum = registration["minimumCompatibleMinor"]
    maximum = registration["maximumCompatibleMinor"]
    if not all(isinstance(value, int) for value in (major, current, minimum, maximum)) or major <= 0 or not minimum <= current <= maximum:
        raise SystemExit(f"invalid shader-facing version range for {identity}")
    scope_names = {
        "shader-family": (1, "family"),
        "shader-source": (2, "source"),
        "feature": (3, "feature"),
        "global": (4, "global"),
    }
    scopes: list[tuple[int, str, str]] = []
    for scope in registration["scopes"]:
        if not isinstance(scope, dict) or scope.get("kind") not in scope_names:
            raise SystemExit(f"invalid compatibility scope for {identity}")
        order, canonical_name = scope_names[scope["kind"]]
        value = "" if scope["kind"] == "global" else scope.get("value")
        if not isinstance(value, str) or (scope["kind"] != "global" and not value):
            raise SystemExit(f"invalid compatibility scope value for {identity}")
        scopes.append((order, canonical_name, value.lower()))
    if not scopes:
        raise SystemExit(f"compatibility registration {identity} has no scopes")
    scopes = sorted(set(scopes))
    lines = [
        f"identity={identity}",
        f"contract={major}.{current}",
        f"compatible={minimum}-{maximum}",
        f"resource={registration['resourceFingerprint']}",
        *(f"scope={name}:{value}" for _, name, value in scopes),
    ]
    return "\n".join(lines)


def canonical_compatibility_requirement_set(registrations: list[dict[str, Any]]) -> str:
    canonical = sorted(
        (registration["identity"], canonical_compatibility_registration(registration))
        for registration in registrations
    )
    return "".join(f"{len(value)}:{value}\n" for _, value in canonical)


def sha256_hex(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def validate_shader_pack(path: Path, expected_lane: int) -> dict[str, int]:
    """Validate the exact committed pack format consumed by the C++ runtime."""
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"failed to read shader pack {path}: {exc}") from exc
    if len(data) < 80:
        raise SystemExit(f"shader pack is shorter than its file header: {path}")
    magic, version, lane, generation, reserved1, reserved2, reserved3, header_hash = struct.unpack_from(
        "<8sIIQ3Q32s", data, 0
    )
    if (
        magic != b"CSXSPK1\0"
        or version != PACK_FORMAT_VERSION
        or lane != expected_lane
        or any((reserved1, reserved2, reserved3))
        or header_hash != hashlib.sha256(data[:48]).digest()
    ):
        raise SystemExit(f"shader pack has an invalid file header: {path}")
    offset = 80
    records = 0
    while offset < len(data):
        if len(data) - offset < 128:
            raise SystemExit(f"shader pack contains an incomplete record tail: {path}")
        (
            record_magic,
            record_version,
            record_reserved,
            sequence,
            logical_size,
            exact_size,
            metadata_size,
            record_reserved2,
            bytecode_size,
            payload_hash,
        ) = struct.unpack_from("<8sIIQIIIIQ32s", data, offset)
        payload_size = logical_size + exact_size + metadata_size + bytecode_size
        total_size = 80 + payload_size + 48
        if (
            record_magic != b"CSXREC1\0"
            or record_version != PACK_FORMAT_VERSION
            or record_reserved
            or record_reserved2
            or sequence == 0
            or offset + total_size > len(data)
        ):
            raise SystemExit(f"shader pack has an invalid record header: {path}")
        payload = data[offset + 80 : offset + 80 + payload_size]
        trailer_magic, trailer_size, trailer_hash = struct.unpack_from(
            "<8sQ32s", data, offset + 80 + payload_size
        )
        actual_hash = hashlib.sha256(payload).digest()
        if (
            trailer_magic != b"CSXCMT1\0"
            or trailer_size != total_size
            or payload_hash != actual_hash
            or trailer_hash != actual_hash
        ):
            raise SystemExit(f"shader pack has an invalid committed record: {path}")
        records += 1
        offset += total_size
    return {"generation": generation, "recordCount": records}


def write_shader_pack(path: Path, lane: int, generation: int, entries: list[dict[str, Any]]) -> None:
    file_prefix = struct.pack(
        "<8sIIQ3Q", b"CSXSPK1\0", PACK_FORMAT_VERSION, lane, generation, 0, 0, 0
    )
    header = file_prefix + hashlib.sha256(file_prefix).digest()
    if len(header) != 80:
        raise AssertionError("shader pack file header size drifted")
    with path.open("wb") as stream:
        stream.write(header)
        for sequence, entry in enumerate(entries, start=1):
            logical = entry["logicalKey"].encode("utf-8")
            exact = entry["exactKey"].encode("utf-8")
            metadata = entry["metadata"].encode("utf-8")
            bytecode = entry["bytecode"]
            payload = logical + exact + metadata + bytecode
            payload_hash = hashlib.sha256(payload).digest()
            record_header = struct.pack(
                "<8sIIQIIIIQ32s",
                b"CSXREC1\0",
                PACK_FORMAT_VERSION,
                0,
                sequence,
                len(logical),
                len(exact),
                len(metadata),
                0,
                len(bytecode),
                payload_hash,
            )
            total_size = len(record_header) + len(payload) + 48
            trailer = struct.pack("<8sQ32s", b"CSXCMT1\0", total_size, payload_hash)
            stream.write(record_header)
            stream.write(payload)
            stream.write(trailer)
        stream.flush()
        os.fsync(stream.fileno())


def build_managed_shader_packs(
    source_root: Path,
    standard_cache: Path,
    horizon_cache: Path | None,
    runtime: str,
    shader_cache_abi: str,
) -> dict[str, int]:
    variants = compatibility_variant_manifest(source_root)
    default_requirement = canonical_compatibility_requirement_set(
        variants["default"]["registrations"]
    )
    horizon_requirement = canonical_compatibility_requirement_set(
        variants["legacy-horizon-fix"]["registrations"]
    )
    records: dict[str, dict[str, Any]] = {}
    variant_counts: dict[str, int] = {}
    inputs = [("standard", standard_cache, False)]
    if horizon_cache is not None:
        inputs.append(("horizon-fix", horizon_cache, True))
    for variant_name, cache_dir, horizon_enabled in inputs:
        manifest = read_cache_manifest_entries(cache_dir, f"{runtime}/{variant_name}")
        count = 0
        for blob_path in cache_blob_paths(cache_dir):
            relative = blob_path.relative_to(cache_dir).as_posix()
            content_contract = manifest.get(relative)
            if not isinstance(content_contract, str) or not re.fullmatch(r"[0-9a-f]{32}", content_contract):
                raise SystemExit(f"{runtime}/{variant_name}: invalid content contract for {relative}")
            family = relative.split("/", 1)[0].lower()
            requirement = horizon_requirement if horizon_enabled and family == "water" else default_requirement
            compatibility_digest = sha256_hex(requirement)
            logical_key = f"{relative}|compat={compatibility_digest}"
            exact_key = f"{logical_key}|content={content_contract}"
            bytecode = blob_path.read_bytes()
            metadata = json.dumps(
                {
                    "schemaVersion": 1,
                    "runtime": runtime,
                    "variant": variant_name,
                    "contentContract": content_contract,
                    "compatibilityRequirementSet": requirement,
                },
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            )
            previous = records.get(exact_key)
            if previous and previous["bytecode"] != bytecode:
                raise SystemExit(f"{runtime}: identical pack identity has differing bytecode: {relative}")
            records.setdefault(
                exact_key,
                {
                    "logicalKey": logical_key,
                    "exactKey": exact_key,
                    "metadata": metadata,
                    "bytecode": bytecode,
                },
            )
            count += 1
        variant_counts[variant_name] = count

    ordered = [records[key] for key in sorted(records)]
    write_shader_pack(standard_cache / "Optimized.A.csxpack", 1, 1, ordered)
    write_shader_pack(standard_cache / "Optimized.B.csxpack", 1, 0, [])
    write_shader_pack(standard_cache / "Developer.A.csxpack", 2, 1, [])
    write_shader_pack(standard_cache / "Developer.B.csxpack", 2, 0, [])
    pack_manifest = {
        "schema": "csx.shader-cache.pack-manifest",
        "schemaVersion": 1,
        "formatVersion": PACK_FORMAT_VERSION,
        "hashAlgorithm": "sha256",
        "runtime": runtime,
        "shaderCacheABI": shader_cache_abi,
        "optimizedRecordCount": len(ordered),
        "developerRecordCount": 0,
        "compatibilityVariants": [
            "default",
            *(["legacy-horizon-fix"] if horizon_cache is not None else []),
        ],
    }
    (standard_cache / PACK_MANIFEST_FILE_NAME).write_text(
        json.dumps(pack_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    for blob_path in cache_blob_paths(standard_cache):
        blob_path.unlink()
    for directory in sorted(
        (path for path in standard_cache.rglob("*") if path.is_dir()), reverse=True
    ):
        if not any(directory.iterdir()):
            directory.rmdir()
    (standard_cache / MANIFEST_FILE_NAME).write_text(
        json.dumps({"schemaVersion": MANIFEST_SCHEMA_VERSION, "entries": {}}, indent=2) + "\n",
        encoding="utf-8",
    )
    if horizon_cache is not None:
        shutil.rmtree(horizon_cache)
    print(
        f"{runtime}: packed {len(ordered)} unique optimized records from "
        + ", ".join(f"{count} {name} blobs" for name, count in variant_counts.items())
    )
    return variant_counts


BASE_EXCLUDED_DEFINES = frozenset(NON_SHIPPED_DEFINES | DEBUG_PROFILE_DEFINES)
SHIPPED_CACHE_PROFILE = CacheProfile(
    name="shipped",
    display_name="Shipped",
    supported_runtimes=frozenset({"SE", "VR"}),
    disabled_features=frozenset({"HorizonFix"}),
    excluded_defines=BASE_EXCLUDED_DEFINES,
    global_defines=("UNIFIED_WATER",),
    file_defines={
        "Lighting.hlsl": ("WETTERNESS",),
        "Water.hlsl": ("WETTERNESS",),
    },
)

# Stable VR tester profile derived from Patka's SettingsUser.json
# (SHA-256 7FB038E6F237E1A397282CF7BE3624729E361CE3E1D1D07D2A088B4C06D9063A).
# Only cache-contract inputs live here; numeric rendering preferences remain
# user settings and do not belong in a distributable shader cache profile.
PATKA_DISABLED_FEATURES = frozenset(
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
)
PATKA_EXCLUDED_DEFINES = frozenset(
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
)
PATKA_CACHE_PROFILE = CacheProfile(
    name="patka",
    display_name="Patka",
    supported_runtimes=frozenset({"VR"}),
    disabled_features=PATKA_DISABLED_FEATURES,
    excluded_defines=BASE_EXCLUDED_DEFINES | PATKA_EXCLUDED_DEFINES,
    global_defines=("UNIFIED_WATER",),
    file_defines={},
)
CACHE_PROFILES = {
    profile.name: profile
    for profile in (SHIPPED_CACHE_PROFILE, PATKA_CACHE_PROFILE)
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
    """Mirror AIO hidden-feature exclusion and derive Horizon Fix metadata."""
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
        raise SystemExit("Horizon Fix must remain in the AIO for cache selection")

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


def configs_for(source_root: Path) -> dict[str, Path]:
    return {
        "SE": source_root / ".github/configs/shader-validation.yaml",
        "VR": source_root / ".github/configs/shader-validation-vr.yaml",
    }


def stage_merged_shaders(
    source_root: Path,
    stage: Path,
    excluded_packages: frozenset[str] | set[str] | None = None,
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

    excluded_packages = (
        frozenset(NON_SHIPPED_PACKAGES)
        if excluded_packages is None
        else frozenset(excluded_packages)
    )
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


def apply_cache_profile_defines(
    config: object,
    profile: CacheProfile,
    *,
    additional_excluded_defines: frozenset[str] = frozenset(),
    excluded_define_exceptions: frozenset[str] = frozenset(),
    additional_file_defines: dict[str, tuple[str, ...]] | None = None,
    add_cross_modlist_variants: bool = False,
) -> object:
    excluded_defines = {
        normalized_define_name(define)
        for define in (
            (profile.excluded_defines - excluded_define_exceptions)
            | additional_excluded_defines
        )
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
                    and normalized_define_name(value) in excluded_defines
                )
            ]
        return node

    config = scrub(config)
    if not isinstance(config, dict):
        return config

    if add_cross_modlist_variants:
        append_cross_modlist_variants(config)

    profile_file_defines = dict(profile.file_defines)
    for file_name, defines in (additional_file_defines or {}).items():
        profile_file_defines[file_name] = (
            *profile_file_defines.get(file_name, ()),
            *defines,
        )

    common_defines = config.get("common_defines")
    if not isinstance(common_defines, list):
        raise SystemExit("shader config common_defines must be a list")
    append_missing_defines(common_defines, profile.global_defines)

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
            f"shader config is missing {profile.name}-profile entries for: "
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
    profile: CacheProfile,
    *,
    additional_excluded_defines: frozenset[str] = frozenset(),
    excluded_define_exceptions: frozenset[str] = frozenset(),
    additional_file_defines: dict[str, tuple[str, ...]] | None = None,
    add_cross_modlist_variants: bool = False,
) -> Path:
    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise SystemExit(f"shader config must be a YAML mapping: {config_path}")

    validate_captured_variant_count(config, config_path)
    config = apply_cache_profile_defines(
        config,
        profile,
        additional_excluded_defines=additional_excluded_defines,
        excluded_define_exceptions=excluded_define_exceptions,
        additional_file_defines=additional_file_defines,
        add_cross_modlist_variants=add_cross_modlist_variants,
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
    shader_cache_abi: str,
) -> int:
    """Hash source/include content for every compiled blob."""
    global_defines_state = ("VR;" if runtime == "VR" else "") + (
        f"ShaderCacheABI={shader_cache_abi};"
    )
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


def write_info_ini(
    cache_dir: Path,
    stage: Path,
    plugin_version: str,
    runtime: str,
    profile: CacheProfile,
    shader_cache_abi: str,
    *,
    excluded_features: frozenset[str] | set[str] | None = None,
    enabled_overrides: dict[str, bool] | None = None,
) -> int:
    validate_ini_value(plugin_version, "plugin version")
    validate_ini_value(shader_cache_abi, "shader cache ABI")
    lines = [
        "[Cache]",
        f"PluginVersion = {plugin_version}",
        f"ShaderCacheABI = {shader_cache_abi}",
        "",
        "",
    ]
    count = 0
    seen_features: set[str] = set()
    excluded_features = (
        frozenset(NON_SHIPPED_FEATURES)
        if excluded_features is None
        else frozenset(excluded_features)
    )
    enabled_overrides = enabled_overrides or {}

    for ini_path in sorted((stage / "Features").glob("*.ini")):
        stem = ini_path.stem
        if stem in RUNTIME_EXCLUDED_FEATURES[runtime] or stem in excluded_features:
            continue
        seen_features.add(stem)

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

        is_enabled = enabled_overrides.get(
            stem,
            stem not in profile.disabled_features,
        )
        enabled = "true" if is_enabled else "false"
        lines += [f"[{stem}]", f"Enabled = {enabled}", f"Version = {version}", "", ""]
        count += 1

    required_features = set(profile.disabled_features) | set(enabled_overrides)
    missing_features = sorted(required_features - seen_features)
    if missing_features:
        raise SystemExit(
            f"cache profile {profile.name!r} references missing feature metadata: "
            + ", ".join(missing_features)
        )

    (cache_dir / INFO_FILE_NAME).write_bytes(
        b"\xef\xbb\xbf" + "\r\n".join(lines).encode("utf-8")
    )
    return count


def read_feature_states(cache_dir: Path) -> dict[str, bool]:
    """Read the feature enablement contract written to one cache."""
    info_path = cache_dir / INFO_FILE_NAME
    config = configparser.ConfigParser(interpolation=None)
    try:
        with info_path.open("r", encoding="utf-8-sig") as stream:
            config.read_file(stream)
    except (configparser.Error, OSError, UnicodeError) as exc:
        raise SystemExit(f"cannot read cache feature metadata {info_path}: {exc}") from exc

    states: dict[str, bool] = {}
    for section in config.sections():
        if section == "Cache":
            continue
        try:
            states[section] = config.getboolean(section, "Enabled")
        except (ValueError, configparser.Error) as exc:
            raise SystemExit(
                f"cache feature {section} has invalid Enabled metadata: {info_path}"
            ) from exc
    return states


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
        # Official releases ship one ALL core binary for every supported
        # runtime, so every cache must default to that binary's identity.
        # Runtime-specific core builds remain available through the explicit
        # --plugin-version overrides.
        "SE": ["ALL", "ALL-VS2022", "AIO-Release", "FLATRIM", "SE"],
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


def cache_blob_paths(cache_dir: Path) -> list[Path]:
    """Return every compiled shader blob in stable order."""
    return sorted(
        path
        for path in cache_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in CACHE_EXTENSIONS
    )


def read_cache_manifest_entries(cache_dir: Path, runtime: str) -> dict[str, object]:
    """Read the supported manifest schema used to describe cache blobs."""
    manifest_path = cache_dir / MANIFEST_FILE_NAME
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
    return manifest["entries"]


def validate_cache(
    cache_dir: Path,
    runtime: str,
    plugin_version: str,
    shader_cache_abi: str,
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
    actual_shader_cache_abi = info.get("Cache", "ShaderCacheABI", fallback=None)
    if actual_shader_cache_abi != shader_cache_abi:
        raise SystemExit(
            f"{runtime}: Info.ini shader cache ABI is {actual_shader_cache_abi!r}; "
            f"expected {shader_cache_abi!r}"
        )

    entries = read_cache_manifest_entries(cache_dir, runtime)
    blob_paths = cache_blob_paths(cache_dir)
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


def validate_cache_profile(profile: CacheProfile, runtimes: list[str]) -> None:
    unsupported = sorted(set(runtimes) - profile.supported_runtimes)
    if unsupported:
        raise SystemExit(
            f"cache profile {profile.name!r} does not support runtime(s): "
            + ", ".join(unsupported)
        )

    noncanonical_exclusions = sorted(
        define
        for define in profile.excluded_defines
        if define != normalized_define_name(define)
    )
    if noncanonical_exclusions:
        raise SystemExit(
            f"cache profile {profile.name!r} has noncanonical exclusions: "
            + ", ".join(noncanonical_exclusions)
        )

    injected_defines = {
        normalized_define_name(define)
        for define in profile.global_defines
    }
    injected_defines.update(
        normalized_define_name(define)
        for defines in profile.file_defines.values()
        for define in defines
    )
    conflicts = sorted(injected_defines & profile.excluded_defines)
    if conflicts:
        raise SystemExit(
            f"cache profile {profile.name!r} both injects and excludes: "
            + ", ".join(conflicts)
        )


def default_package_label(profile: CacheProfile, plugin_version: str) -> str:
    if profile.name == SHIPPED_CACHE_PROFILE.name:
        return plugin_version
    return f"{plugin_version}-{profile.display_name}"


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


def replace_publication_staging(staging: Path, destination: Path) -> None:
    """Atomically publish staged output despite short-lived Windows locks."""
    last_error: OSError | None = None
    for attempt in range(PUBLICATION_REPLACE_ATTEMPTS):
        try:
            staging.replace(destination)
            return
        except OSError as exc:
            last_error = exc
            if (
                attempt + 1 == PUBLICATION_REPLACE_ATTEMPTS
                or not path_entry_exists(staging)
                or path_entry_exists(destination)
            ):
                raise
            time.sleep(PUBLICATION_REPLACE_RETRY_SECONDS)

    raise RuntimeError("publication retry loop exhausted unexpectedly") from last_error


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
            replace_publication_staging(staging, destination)
        except OSError as exc:
            raise SystemExit(
                f"failed to publish {runtime} cache: {destination}; "
                f"validated staging retained at {staging} ({exc})"
            ) from exc
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
        replace_publication_staging(staging, destination)
    except OSError as exc:
        try:
            backup.replace(destination)
        except OSError as restore_error:
            raise SystemExit(
                f"failed to publish {runtime} cache and could not restore "
                f"the previous output: {destination}; validated staging "
                f"retained at {staging}"
            ) from restore_error
        raise SystemExit(
            f"failed to publish {runtime} cache: {destination}; "
            f"validated staging retained at {staging} ({exc})"
        ) from exc

    # The old cache remains inside the isolated temporary workspace until it
    # is cleaned up after this invocation. It is never recursively deleted
    # from the user-selected output root.
    return destination


def cache_variants_for(profile: CacheProfile) -> tuple[CacheVariant, ...]:
    """Return the one managed install cache."""
    return (STANDARD_CACHE_VARIANT,)


def compile_variants_for(profile: CacheProfile) -> tuple[CacheVariant, ...]:
    """Return loose variants required as inputs to the managed pack."""
    if profile.name == SHIPPED_CACHE_PROFILE.name:
        return CACHE_VARIANTS
    return (STANDARD_CACHE_VARIANT,)


def cache_variants_for_horizon_state(
    horizon_variants: bool,
) -> tuple[CacheVariant, ...]:
    """Map installer/archive Horizon state to the expected cache layout."""
    return CACHE_VARIANTS if horizon_variants else (STANDARD_CACHE_VARIANT,)


def validate_fomod_installer(
    fomod_dir: Path,
    compatibility_tag: str,
    *,
    horizon_variants: bool | None = None,
) -> None:
    """Verify visible guidance, exact identity checks, and manual fallback."""
    config_path = fomod_dir / FOMOD_CONFIG_FILE_NAME
    info_path = fomod_dir / FOMOD_INFO_FILE_NAME
    try:
        root = ET.parse(config_path).getroot()
        info_root = ET.parse(info_path).getroot()
    except (ET.ParseError, OSError) as exc:
        raise SystemExit(f"invalid cache FOMOD metadata in {fomod_dir}: {exc}") from exc

    if horizon_variants is None:
        horizon_variants = any(
            folder.get("source") == HORIZON_FIX_CACHE_DIRECTORY
            for folder in root.findall(
                "./installSteps/installStep/optionalFileGroups/group/"
                "plugins/plugin/files/folder"
            )
        )

    if root.tag != "config" or [child.tag for child in root] != [
        "moduleName",
        "installSteps",
    ]:
        raise SystemExit(
            "cache FOMOD config must use the ordered moduleName and "
            "installSteps structure"
        )
    module_name = root.find("./moduleName")
    if module_name is None or not (module_name.text or "").strip():
        raise SystemExit("cache FOMOD moduleName must not be empty")
    if root.find("./moduleDependencies") is not None:
        raise SystemExit(
            "cache FOMOD must expose compatibility guidance before checking "
            "non-plugin files"
        )

    notice_flags = (
        *FOMOD_BASE_NOTICE_FLAGS,
        *((FOMOD_HORIZON_NOTICE_FLAG,) if horizon_variants else ()),
    )
    expected_variants = cache_variants_for_horizon_state(horizon_variants)
    install_steps = root.findall("./installSteps/installStep")
    plugins = root.findall(
        "./installSteps/installStep/optionalFileGroups/group/plugins/plugin"
    )
    notice_plugins: dict[str, ET.Element] = {}
    for install_step in install_steps:
        step_notices: list[tuple[str, ET.Element]] = []
        for plugin in install_step.findall("./optionalFileGroups/group/plugins/plugin"):
            for flag in plugin.findall("./conditionFlags/flag"):
                flag_name = flag.get("name", "")
                if flag_name in notice_flags:
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
        for group in root.findall("./installSteps/installStep/optionalFileGroups/group")
        if group.find("./plugins/plugin/files/folder") is not None
    ]
    if set(notice_plugins) != set(notice_flags) or len(cache_plugins) != len(
        expected_variants
    ):
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
    expected_cache_sources = {variant.directory for variant in expected_variants}
    if set(cache_plugins_by_source) != expected_cache_sources:
        raise SystemExit(
            "cache FOMOD sources do not match the generated cache variants"
        )
    cache_sources_in_order = [
        plugin.find("./files/folder").get("source", "")
        for plugin in cache_plugins
    ]
    if horizon_variants and cache_sources_in_order[0] != HORIZON_FIX_CACHE_DIRECTORY:
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

    expected_info_children = ["Name", "Author", "Version", "Description", "Website"]
    if (
        info_root.tag != "fomod"
        or [child.tag for child in info_root] != expected_info_children
        or any(not (child.text or "").strip() for child in info_root)
        or info_root.findtext("./Website") != FOMOD_HELP_URL
    ):
        raise SystemExit(
            "cache FOMOD info must contain ordered identity fields and the "
            "detailed setup help link"
        )

    descriptions = "\n".join(
        filter(
            None,
            (
                info_root.findtext("./Description"),
                *(notice_plugins[flag].findtext("./description") for flag in notice_flags),
                *(plugin.findtext("./description") for plugin in cache_plugins),
            ),
        )
    )
    required_guidance = (
        "Settings",
        "Plugins",
        "Fomod Installer",
        "use_any_file",
        "true",
    )
    if horizon_variants:
        required_guidance += ("reinstall this shader-cache FOMOD",)
    missing_guidance = [text for text in required_guidance if text not in descriptions]
    if missing_guidance:
        raise SystemExit(
            "cache FOMOD is missing MO2 setup guidance: "
            + ", ".join(missing_guidance)
        )

    core_dependencies = {
        ("SKSE/Plugins/CommunityShaders.dll", "Active"),
        (
            f"{CSX_MARKER_RELATIVE_DIRECTORY}/{compatibility_tag}.marker",
            "Active",
        ),
    }
    horizon_dependency = HORIZON_FIX_DLL_PATH.replace("\\", "/")
    expected_patterns = {
        CACHE_DIRECTORY: (
            {
                frozenset(core_dependencies | {(horizon_dependency, "Missing")}),
                frozenset(core_dependencies | {(horizon_dependency, "Inactive")}),
            }
            if horizon_variants
            else {frozenset(core_dependencies)}
        ),
        **(
            {
                HORIZON_FIX_CACHE_DIRECTORY: {
                    frozenset(core_dependencies | {(horizon_dependency, "Active")})
                }
            }
            if horizon_variants
            else {}
        ),
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
            dependencies = pattern.find("./dependencies")
            if (
                matched_type is None
                or matched_type.get("name") != "Recommended"
                or dependencies is None
                or dependencies.get("operator") != "And"
                or any(child.tag != "fileDependency" for child in dependencies)
            ):
                raise SystemExit(
                    f"cache FOMOD variant {source} has a malformed recommendation"
                )
            actual_patterns.add(
                frozenset(
                    (
                        dependency.get("file", "").replace("\\", "/"),
                        dependency.get("state", ""),
                    )
                    for dependency in dependencies.findall("./fileDependency")
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
            or install_folder.get("priority") != "0"
        ):
            raise SystemExit(
                f"cache FOMOD variant {source} does not install at Data/ShaderCache"
            )


def write_fomod_installer(
    runtime_root: Path,
    runtime: str,
    label: str,
    plugin_version: str,
    *,
    source_root: Path = REPO,
    horizon_variants: bool | None = None,
) -> Path:
    """Stage one managed cache; compatibility variants live inside its packs."""
    horizon_variants = False if horizon_variants is None else horizon_variants
    if horizon_variants:
        raise SystemExit(
            "installer-selected Horizon cache variants are retired; compile them into the managed pack"
        )

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
    compatibility_tag = csx_compatibility_tag(plugin_version)
    module_name = f"Community Shaders {runtime} Shader Cache - {display_label}"
    description = (
        f"Requires active {compatibility_tag}. MO2 users must set Settings > "
        "Plugins > Fomod Installer > use_any_file to true for automatic checks. "
        + (
            "Verify the selectable Horizon Fix profile before installing. "
            "Reinstall after changing Horizon Fix. "
            if horizon_variants
            else "Verify the selectable cache before installing. "
        )
        + "Click Website for full setup instructions."
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
    core_dependencies = f"""                        <fileDependency
                          file="SKSE\\Plugins\\CommunityShaders.dll"
                          state="Active" />
                        <fileDependency
                          file="SKSE\\Plugins\\CommunityShaders\\{compatibility_tag}.marker"
                          state="Active" />"""

    if horizon_variants:
        horizon_notice_step = f"""    <installStep name="Horizon Fix profile notice">
      <optionalFileGroups order="Explicit">
        <group name="Required profile notice" type="SelectAll">
          <plugins order="Explicit">
            <plugin name="Reinstall after changing Horizon Fix">
              <description>{horizon_notice_description}</description>
              <conditionFlags>
                <flag name="{FOMOD_HORIZON_NOTICE_FLAG}">shown</flag>
              </conditionFlags>
              <typeDescriptor>
                <type name="Required" />
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
"""
        standard_description = (
            f"Choose this only when {compatibility_tag} is active and "
            "HorizonFix.dll is missing or inactive. Its Water shaders are "
            "compiled without Horizon Fix compatibility. Reinstall this "
            "shader-cache FOMOD after enabling Horizon Fix."
        )
        horizon_description = (
            f"Choose this when {compatibility_tag} and "
            "SKSE\\Plugins\\HorizonFix.dll are active. Its Water shaders are "
            "compiled with Horizon Fix compatibility. This is the fallback "
            "default when MO2 cannot detect non-plugin files. Reinstall after "
            "disabling Horizon Fix."
        )
        profile_step = f"""    <installStep name="Choose the active Horizon Fix profile">
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
{core_dependencies}
                        <fileDependency file="SKSE\\Plugins\\HorizonFix.dll" state="Active" />
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
                <folder source="{CACHE_DIRECTORY}" destination="{CACHE_DIRECTORY}" priority="0" />
              </files>
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="Optional" />
                  <patterns order="Explicit">
                    <pattern>
                      <dependencies operator="And">
{core_dependencies}
                        <fileDependency file="SKSE\\Plugins\\HorizonFix.dll" state="Missing" />
                      </dependencies>
                      <type name="Recommended" />
                    </pattern>
                    <pattern>
                      <dependencies operator="And">
{core_dependencies}
                        <fileDependency file="SKSE\\Plugins\\HorizonFix.dll" state="Inactive" />
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
"""
    else:
        horizon_notice_step = ""
        cache_description = (
            f"Choose this cache when CommunityShaders.dll and the exact "
            f"{compatibility_tag} marker are active. If MO2 cannot inspect "
            "non-plugin files, verify those files manually before installing."
        )
        profile_step = f"""    <installStep name="Verify the active CSX core">
      <optionalFileGroups order="Explicit">
        <group name="Select exactly one compatible cache" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Install cache for {compatibility_tag}">
              <description>{cache_description}</description>
              <files>
                <folder source="{CACHE_DIRECTORY}" destination="{CACHE_DIRECTORY}" priority="0" />
              </files>
              <typeDescriptor>
                <dependencyType>
                  <defaultType name="Optional" />
                  <patterns order="Explicit">
                    <pattern>
                      <dependencies operator="And">
{core_dependencies}
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
"""

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
{horizon_notice_step}{profile_step}  </installSteps>
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
        validate_fomod_installer(
            fomod_dir,
            compatibility_tag,
            horizon_variants=horizon_variants,
        )
    except OSError as exc:
        shutil.rmtree(fomod_dir, ignore_errors=True)
        raise SystemExit(f"failed to stage cache installer: {fomod_dir}") from exc
    except SystemExit:
        shutil.rmtree(fomod_dir, ignore_errors=True)
        raise
    return fomod_dir

def validate_cache_archive(
    archive: Path,
    cmake: str,
    runtime: str,
    plugin_version: str,
    *,
    horizon_variants: bool | None = None,
) -> None:
    """Verify the raw runtime archive preserves each cache variant."""
    command = [cmake, "-E", "tar", "tf", str(archive)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(
            f"failed to inspect packaged {runtime} cache (exit {result.returncode})"
        )

    entry_list = [
        entry.strip().replace("\\", "/")
        for entry in result.stdout.splitlines()
        if entry.strip()
    ]
    entries = set(entry_list)
    if horizon_variants is None:
        horizon_prefix = f"{HORIZON_FIX_CACHE_DIRECTORY}/"
        horizon_variants = any(
            entry == HORIZON_FIX_CACHE_DIRECTORY or entry.startswith(horizon_prefix)
            for entry in entries
        )
    variants = CACHE_VARIANTS if horizon_variants else (STANDARD_CACHE_VARIANT,)
    required_entries = {
        *(
            f"{variant.directory}/{metadata}"
            for variant in variants
            for metadata in (INFO_FILE_NAME, MANIFEST_FILE_NAME)
        ),
		f"{FOMOD_DIRECTORY}/{FOMOD_CONFIG_FILE_NAME}",
        f"{FOMOD_DIRECTORY}/{FOMOD_INFO_FILE_NAME}",
        FOMOD_HELP_IMAGE_ARCHIVE_PATH,
        *(f"{CACHE_DIRECTORY}/{name}" for name in PACK_FILE_NAMES),
		f"{CACHE_DIRECTORY}/{PACK_MANIFEST_FILE_NAME}",
    }
    missing_entries = sorted(required_entries - entries)
    if missing_entries:
        raise SystemExit(
            f"packaged {runtime} cache is missing required install entries: "
            f"{', '.join(missing_entries)}"
        )

    duplicate_required_entries = sorted(
        entry for entry in required_entries if entry_list.count(entry) != 1
    )
    if duplicate_required_entries:
        raise SystemExit(
            f"packaged {runtime} cache has duplicate install metadata: "
            f"{', '.join(duplicate_required_entries)}"
        )

    flattened_entries = sorted({INFO_FILE_NAME, MANIFEST_FILE_NAME} & entries)
    if flattened_entries:
        raise SystemExit(
            f"packaged {runtime} cache contains flattened metadata: "
            f"{', '.join(flattened_entries)}"
        )

    installer_entries = sorted(
        entry
        for entry in entries
        if entry.casefold() == "fomod" or entry.casefold().startswith("fomod/")
    )
    if installer_entries:
        raise SystemExit(
            f"packaged {runtime} cache must not contain installer metadata: "
            f"{', '.join(installer_entries)}"
        )

    with tempfile.TemporaryDirectory(
        prefix=f".shader-cache-{runtime}-archive-"
    ) as temporary:
        extract_root = Path(temporary)
        extract_result = subprocess.run(
            [cmake, "-E", "tar", "xf", str(archive)],
            cwd=extract_root,
            capture_output=True,
            text=True,
        )
        if extract_result.returncode != 0:
            raise SystemExit(
                f"failed to extract packaged {runtime} cache for validation "
                f"(exit {extract_result.returncode})"
            )

        archived_shader_abi: str | None = None
        for variant in variants:
            info = configparser.ConfigParser(interpolation=None)
            info_path = extract_root / variant.directory / INFO_FILE_NAME
            try:
                with info_path.open("r", encoding="utf-8-sig") as stream:
                    info.read_file(stream)
            except (configparser.Error, OSError, UnicodeError) as exc:
                raise SystemExit(
                    f"packaged {runtime}/{variant.name} cache has invalid "
                    f"{INFO_FILE_NAME}: {exc}"
                ) from exc
            archived_version = info.get("Cache", "PluginVersion", fallback=None)
            if archived_version != plugin_version:
                raise SystemExit(
                    f"packaged {runtime}/{variant.name} cache plugin version is "
                    f"{archived_version!r}; expected {plugin_version!r}"
                )
			variant_shader_abi = info.get("Cache", "ShaderCacheABI", fallback=None)
            if not variant_shader_abi:
                raise SystemExit(
                    f"packaged {runtime}/{variant.name} cache has no ShaderCacheABI"
                )
            if archived_shader_abi is None:
                archived_shader_abi = variant_shader_abi
            elif archived_shader_abi != variant_shader_abi:
                raise SystemExit(f"packaged {runtime} caches disagree on ShaderCacheABI")

        pack_root = extract_root / CACHE_DIRECTORY
        pack_stats = {
            "optimizedA": validate_shader_pack(pack_root / "Optimized.A.csxpack", 1),
            "optimizedB": validate_shader_pack(pack_root / "Optimized.B.csxpack", 1),
            "developerA": validate_shader_pack(pack_root / "Developer.A.csxpack", 2),
            "developerB": validate_shader_pack(pack_root / "Developer.B.csxpack", 2),
        }
        try:
            pack_manifest = json.loads(
                (pack_root / PACK_MANIFEST_FILE_NAME).read_text(encoding="utf-8")
            )
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise SystemExit(f"packaged {runtime} cache has an invalid pack manifest") from exc
        if (
            pack_manifest.get("schema") != "csx.shader-cache.pack-manifest"
            or pack_manifest.get("schemaVersion") != 1
            or pack_manifest.get("formatVersion") != PACK_FORMAT_VERSION
            or pack_manifest.get("hashAlgorithm") != "sha256"
            or pack_manifest.get("shaderCacheABI") != archived_shader_abi
            or pack_manifest.get("optimizedRecordCount")
            != pack_stats["optimizedA"]["recordCount"] + pack_stats["optimizedB"]["recordCount"]
            or pack_manifest.get("developerRecordCount")
            != pack_stats["developerA"]["recordCount"] + pack_stats["developerB"]["recordCount"]
        ):
            raise SystemExit(f"packaged {runtime} cache pack manifest disagrees with its pack files")
        for first, second in (("optimizedA", "optimizedB"), ("developerA", "developerB")):
            if pack_stats[first]["generation"] == pack_stats[second]["generation"]:
                raise SystemExit(f"packaged {runtime} cache has ambiguous A/B generations")
		validate_fomod_installer(
            extract_root / FOMOD_DIRECTORY,
            csx_compatibility_tag(plugin_version),
			horizon_variants=horizon_variants,
		)


def prepare_cache_archive(
    runtime_root: Path,
    workspace: Path,
    runtime: str,
    label: str,
    plugin_version: str,
    cmake: str,
    *,
    profile: CacheProfile = SHIPPED_CACHE_PROFILE,
) -> Path:
    """Create a validated candidate archive without changing published output."""
    variants = cache_variants_for(profile)
    horizon_variants = HORIZON_FIX_CACHE_VARIANT in variants
    archive_name = f"ShaderCache-{runtime}-{safe_label(label)}.7z"
    temporary_archive = workspace / archive_name
    command = [
        cmake,
        "-E",
        "tar",
        "cf",
        str(temporary_archive),
        "--format=7zip",
        *(variant.directory for variant in variants),
    ]
    print("run:", " ".join(command))
    result = subprocess.run(command, cwd=runtime_root)

    if (
        result.returncode != 0
        or not temporary_archive.is_file()
        or temporary_archive.stat().st_size == 0
    ):
        raise SystemExit(
            f"failed to package {runtime} cache (exit {result.returncode})"
        )

    validate_cache_archive(
        temporary_archive,
        cmake,
        runtime,
        plugin_version,
        horizon_variants=horizon_variants,
    )
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
        replace_publication_staging(staging, archive_path)
    except OSError as exc:
        raise SystemExit(
            f"failed to publish {runtime} cache archive: {archive_path}; "
            f"validated staging retained at {staging} ({exc})"
        ) from exc
    print(f"{runtime}: packaged {archive_path} ({archive_path.stat().st_size} bytes)")
    return archive_path


def validate_horizon_variant_delta(
    standard_cache: Path,
    horizon_cache: Path,
    standard_feature_states: dict[str, bool],
    horizon_feature_states: dict[str, bool],
    *,
    runtime: str = "SE",
) -> None:
    """Ensure Horizon Fix changes only its state and Water artifacts."""
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
            for path in cache_blob_paths(cache_dir)
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
        manifest_entries = read_cache_manifest_entries(cache_dir, runtime)
        if set(manifest_entries) != set(standard_blobs):
            raise SystemExit(
                f"Horizon variant manifest does not match its blobs: {cache_dir}"
            )

    print(
        f"{runtime}: Horizon Fix variant changes {len(changed_blobs)} Water blobs and "
        "no unrelated shader blobs"
    )


def build_runtime(
    source_root: Path,
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
    profile: CacheProfile,
    distribution_profile: DistributionProfile | None = None,
) -> tuple[Path, dict[str, int], int]:
    runtime_root = workspace / runtime
    shader_contract = shader_contract_identity(
        source_root, DEFAULT_SHADER_CONTRACT_FILES, runtime
    )
    shader_cache_abi = sha256_bytes(canonical_bytes(shader_contract))
    variants = compile_variants_for(profile)
    compatibility_variants = compatibility_variant_manifest(source_root)
    has_horizon_variant = HORIZON_FIX_CACHE_VARIANT in variants
    if has_horizon_variant and distribution_profile is None:
        raise SystemExit("shipped cache variants require the Horizon Fix contract")

    build_results: dict[str, tuple[Path, int, int, dict[str, bool]]] = {}
    for variant in variants:
        cache_dir = runtime_root / variant.directory
        cache_dir.mkdir(parents=True, exist_ok=True)

        additional_excluded_defines = frozenset()
        excluded_define_exceptions = frozenset()
        additional_file_defines: dict[str, tuple[str, ...]] = {}
        excluded_features: frozenset[str] | None = None
        enabled_overrides: dict[str, bool] = {}
        add_cross_modlist = False
        if distribution_profile is not None:
            additional_excluded_defines = frozenset(
                {distribution_profile.horizon_fix_define}
            )
            enabled_overrides[HORIZON_FIX_SHORT_NAME] = bool(
                variant.horizon_fix_enabled
            )
            if runtime == "SE":
                additional_excluded_defines |= distribution_profile.excluded_defines
                excluded_define_exceptions = frozenset(NON_SHIPPED_DEFINES)
                excluded_features = distribution_profile.excluded_short_names
                add_cross_modlist = True
            if variant.horizon_fix_enabled:
                declared_defines = compatibility_variants["legacy-horizon-fix"][
                    "shaderDefinesBySource"
                ].get(HORIZON_FIX_SHADER_FILE)
                if not isinstance(declared_defines, list) or not declared_defines or not all(
                    isinstance(value, str) and value for value in declared_defines
                ):
                    raise SystemExit(
                        "legacy-horizon-fix compatibility contract must declare "
                        f"defines for {HORIZON_FIX_SHADER_FILE}"
                    )
                if distribution_profile.horizon_fix_define not in declared_defines:
                    raise SystemExit(
                        "distribution Horizon define disagrees with the compatibility contract"
                    )
                additional_file_defines[HORIZON_FIX_SHADER_FILE] = tuple(declared_defines)

        filtered_config = filter_profile_defines(
            config_path,
            workspace / f"config-{runtime}-{variant.name}.yaml",
            yaml,
            profile,
            additional_excluded_defines=additional_excluded_defines,
            excluded_define_exceptions=excluded_define_exceptions,
            additional_file_defines=additional_file_defines,
            add_cross_modlist_variants=add_cross_modlist,
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
        imagespace_remap = remap_imagespace_dirs(cache_dir, runtime)
        write_shader_cache_manifest(
            cache_dir,
            stage,
            runtime,
            imagespace_remap,
            write_manifest,
            shader_cache_abi,
        )

        section_count = write_info_ini(
            cache_dir,
            stage,
            plugin_version,
            runtime,
            profile,
            shader_cache_abi,
            excluded_features=excluded_features,
            enabled_overrides=enabled_overrides,
        )
        blob_count = validate_cache(
            cache_dir,
            f"{runtime}/{variant.name}",
            plugin_version,
            shader_cache_abi,
        )
        build_results[variant.name] = (
            cache_dir,
            blob_count,
            section_count,
            read_feature_states(cache_dir),
        )

    if has_horizon_variant:
        standard_cache, _, _, standard_states = build_results["standard"]
        horizon_cache, _, _, horizon_states = build_results["horizon-fix"]
        validate_horizon_variant_delta(
            standard_cache,
            horizon_cache,
            standard_states,
            horizon_states,
            runtime=runtime,
        )
    build_managed_shader_packs(
        source_root,
        build_results["standard"][0],
        build_results["horizon-fix"][0] if has_horizon_variant else None,
        runtime,
        shader_cache_abi,
    )

    section_counts = {result[2] for result in build_results.values()}
    if len(section_counts) != 1:
        raise SystemExit("shader-cache variants contain different feature counts")
    blob_counts = {
        variant_name: result[1]
        for variant_name, result in build_results.items()
    }
    return runtime_root, blob_counts, section_counts.pop()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--runtime",
        choices=["SE", "VR", "both"],
        default="both",
        help="Target runtime(s).",
    )
    parser.add_argument(
        "--profile",
        type=str.lower,
        choices=sorted(CACHE_PROFILES),
        default=SHIPPED_CACHE_PROFILE.name,
        help=(
            "Cache feature profile (default: shipped). Patka is the persistent "
            "VR profile for the main tester configuration."
        ),
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
        help="Create raw cache .7z archives for release AIO assembly.",
    )
    parser.add_argument(
        "--package-label",
        help=(
            "Archive label (default: the runtime's plugin version; named "
            "profiles append their display name)."
        ),
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
    profile = CACHE_PROFILES[args.profile]
    validate_cache_profile(profile, runtimes)
    print(f"cache profile: {profile.display_name} ({profile.name})")
    configs = configs_for(source_root)
    for runtime in runtimes:
        config_path = configs[runtime]
        if not config_path.is_file():
            raise SystemExit(f"missing validation config for {runtime}: {config_path}")

    distribution_profiles: dict[str, DistributionProfile] = {}
    if profile.name == SHIPPED_CACHE_PROFILE.name:
        shipped_distribution_profile = derive_distribution_profile(source_root)
        distribution_profiles = {
            runtime: shipped_distribution_profile for runtime in runtimes
        }
    if "SE" in distribution_profiles:
        hidden_features = sorted(
            distribution_profiles["SE"].excluded_short_names
        )
        print(
            "SE AIO profile: excluding hidden features "
            + (", ".join(hidden_features) if hidden_features else "(none)")
        )

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
        prepared: list[tuple[str, Path, dict[str, int], int, Path | None]] = []
        for runtime in runtimes:
            plugin_version = plugin_versions[runtime]
            distribution_profile = distribution_profiles.get(runtime)
            stage = workspace / f"staged-shaders-{runtime}"
            stage_merged_shaders(
                source_root,
                stage,
                (
                    distribution_profile.excluded_packages
                    if runtime == "SE" and distribution_profile is not None
                    else frozenset(NON_SHIPPED_PACKAGES)
                ),
            )
            print(f"{runtime}: staged merged shader tree")

            candidate_root, blob_counts, section_count = build_runtime(
                source_root=source_root,
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
                profile=profile,
                distribution_profile=distribution_profile,
            )
            archive_candidate = None
            if args.package:
                archive_candidate = prepare_cache_archive(
                    candidate_root,
                    workspace,
                    runtime,
                    args.package_label
                    or default_package_label(profile, plugin_version),
                    plugin_version,
                    cmake,
                    profile=profile,
                )
            prepared.append(
                (
                    runtime,
                    candidate_root,
                    blob_counts,
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

        for runtime, candidate_root, blob_counts, section_count, archive_candidate in prepared:
            runtime_root = publish_runtime_cache(candidate_root, out_root, runtime)
            print(
                f"{runtime}: "
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
                    runtime,
                )

    return 0


if __name__ == "__main__":
    sys.exit(main())
