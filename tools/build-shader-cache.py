#!/usr/bin/env python3
"""Build a distributable shader disk cache for this repo.

Produces the managed layout the runtime consumes at Data/ShaderCache/:
  ShaderCache/Optimized.{A,B}.csxpack
  ShaderCache/Developer.{A,B}.csxpack
  ShaderCache/Info.ini
  ShaderCache/PackManifest.json

Standalone archives install the managed cache at Data/ShaderCache. The AIO
assembler preserves the full SE core and offers an optional prebuilt cache.
Scoped compatibility variants share one pack and are selected by the runtime.

Usage:
  python tools/build-shader-cache.py --package
  python tools/build-shader-cache.py --assemble-aio CACHE_ROOT --aio-root AIO_ROOT
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


from shader_cache_manifest import (
    SCHEMA_VERSION as MANIFEST_SCHEMA_VERSION,
    STAGE_EXTENSIONS,
    compile_task_defines,
    prepare_fxc_defines,
)
from shader_bytecode import validate_dxbc

REPO = Path(__file__).resolve().parent.parent
CACHE_DIRECTORY = "ShaderCache"
CACHE_EXTENSIONS = frozenset(STAGE_EXTENSIONS.values())
INFO_FILE_NAME = "Info.ini"
MANIFEST_FILE_NAME = "Manifest.json"
PACK_MANIFEST_FILE_NAME = "PackManifest.json"
PACK_LANES = {
    "Optimized.A.csxpack": 1,
    "Optimized.B.csxpack": 1,
    "Developer.A.csxpack": 2,
    "Developer.B.csxpack": 2,
}
PACK_FILE_NAMES = tuple(PACK_LANES)
PACK_FORMAT_VERSION = 1
PACK_MANIFEST_SCHEMA_VERSION = 2
PACK_MAX_RECORD_SIZE = 512 * 1024 * 1024
COMPATIBILITY_VARIANTS_FILE = Path("config/shader-compatibility-variants.json")
PUBLICATION_REPLACE_ATTEMPTS = 20
PUBLICATION_REPLACE_RETRY_SECONDS = 0.5
CAPTURED_VARIANT_COUNT_KEY = "captured_shader_variants"
NORMALIZED_VARIANT_COUNT_KEY = "normalized_shader_variants"
CSX_PLUGIN_VERSION_PATTERN = re.compile(r"^CSX (?P<version>[0-9]+\.[0-9]+)-SE$")
TARGET_RUNTIME = "SE"

CSX_VERSION_FILE = Path("cmake/CSXVersion.cmake")

CSX_VERSION_DECLARATION_PATTERN = re.compile(
    r'^\s*set\(CSX_VERSION\s+"(?P<version>[0-9]+\.[0-9]+-SE)"\)\s*$',
    re.MULTILINE,
)

FOMOD_DIRECTORY = "fomod"

FOMOD_CONFIG_FILE_NAME = "ModuleConfig.xml"

FOMOD_INFO_FILE_NAME = "info.xml"

FOMOD_WEBSITE = (
    "https://github.com/ParticleTroned/skyrim-community-shaders/blob/"
    "cs-1.7-PL-SE/docs/development/prebuilt-shader-cache.md"
)

AIO_CORE_DIRECTORY = "Core"
AIO_CACHE_DIRECTORY = "CachePayload"

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
FEATURE_SHADER_ABI_PATTERN = re.compile(
    r'GetShaderCacheAbiVersion[^\{]*\{\s*return\s+"([^"]*)"',
    re.DOTALL,
)


# Distribution profile transforms. The source validation configs are still
# useful as compile inventories, but cache profiles select the feature state
# represented by the compiled bytecode and Info.ini metadata.
DEBUG_PROFILE_DEFINES = {
    "DEBUG",
    "_DEBUG",
    "D3D_DEBUG_INFO",
    "D3DCOMPILE_DEBUG",
    "D3DCOMPILE_SKIP_OPTIMIZATION",
}
# A clean SE capture records only the permutations exercised by that load
# order. Keep separately observed SE-valid RunGrass permutations in a release
# overlay so the distributed cache remains useful across modlists. These are
# kept separate from the captured inventory.
CROSS_MODLIST_SHADER_VARIANTS = {
    "RunGrass.hlsl": {
        "PSHADER": {
            "Grass:Pixel:1": (),
            "Grass:Pixel:10006": ("DO_ALPHA_TEST",),
        },
        "VSHADER": {
            "Grass:Vertex:0": (),
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
    shader_abi: str | None


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
        or type(document.get("schemaVersion")) is not int
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
        identities: set[str] = set()
        for registration in variant["registrations"]:
            canonical_compatibility_registration(registration)
            identity = registration["identity"]
            if identity in identities:
                raise SystemExit(f"duplicate compatibility identity {identity!r} in {variant['id']!r}")
            identities.add(identity)
        variants[variant["id"]] = variant
    if "default" not in variants or "legacy-horizon-fix" not in variants:
        raise SystemExit("compatibility manifest must define default and legacy-horizon-fix")
    return variants


def normalize_shader_source(value: str, identity: str) -> str:
    value = value.lower().replace("\\", "/")
    if value.startswith("/") or ":" in value:
        raise SystemExit(f"shader source scope must be a relative path for {identity}")
    components = [
        component for component in value.split("/") if component not in ("", ".")
    ]
    if ".." in components:
        raise SystemExit(f"shader source scope contains traversal for {identity}")
    if components and components[0] == "data":
        components.pop(0)
    value = "/".join(components)
    if not value:
        raise SystemExit(f"shader source scope is empty for {identity}")
    return value


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
    if not isinstance(registration, dict) or any(
        field not in registration for field in required
    ):
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
    if (
        not all(
            type(value) is int and 0 <= value <= 0xFFFFFFFF
            for value in (major, current, minimum, maximum)
        )
        or major == 0
        or not minimum <= current <= maximum
    ):
        raise SystemExit(f"invalid shader-facing version range for {identity}")
    fingerprint = registration["resourceFingerprint"]
    if (
        not isinstance(fingerprint, str)
        or len(fingerprint.encode("utf-8")) > 512
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in fingerprint)
    ):
        raise SystemExit(f"invalid compatibility resource fingerprint for {identity}")
    if not isinstance(registration["scopes"], list) or not 1 <= len(registration["scopes"]) <= 64:
        raise SystemExit(f"compatibility registration {identity} requires one to 64 scopes")
    scope_names = {
        "shader-family": (1, "family"),
        "shader-source": (2, "source"),
        "feature": (3, "feature"),
        "global": (4, "global"),
    }
    scopes: list[tuple[int, str, str]] = []
    for scope in registration["scopes"]:
        if (
            not isinstance(scope, dict)
            or not isinstance(scope.get("kind"), str)
            or scope["kind"] not in scope_names
        ):
            raise SystemExit(f"invalid compatibility scope for {identity}")
        if scope["kind"] in {"shader-source", "feature"}:
            raise SystemExit(
                f"unsupported compatibility scope for {identity}: {scope['kind']}"
            )
        order, canonical_name = scope_names[scope["kind"]]
        value = "" if scope["kind"] == "global" else scope.get("value")
        if not isinstance(value, str) or (scope["kind"] != "global" and not value):
            raise SystemExit(f"invalid compatibility scope value for {identity}")
        if (
            len(value.encode("utf-8")) > 512
            or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)
        ):
            raise SystemExit(f"invalid compatibility scope value for {identity}")
        value = value.encode("utf-8").lower().decode("utf-8")
        if scope["kind"] == "shader-source":
            value = normalize_shader_source(value, identity)
        scopes.append((order, canonical_name, value))
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
    return "".join(f"{len(value.encode('utf-8'))}:{value}\n" for _, value in canonical)


def canonical_compatibility_domain_registration(
    registration: dict[str, Any],
) -> str:
    canonical = canonical_compatibility_registration(registration)
    lines = canonical.split("\n")
    return "\n".join(
        (
            lines[0],
            f"contract-major={registration['contractMajor']}",
            *(
                line
                for line in lines
                if line.startswith("resource=") or line.startswith("scope=")
            ),
        )
    )


def canonical_compatibility_domain_set(
    registrations: list[dict[str, Any]],
) -> str:
    canonical = sorted(
        (
            registration["identity"],
            canonical_compatibility_domain_registration(registration),
        )
        for registration in registrations
    )
    return "".join(f"{len(value.encode('utf-8'))}:{value}\n" for _, value in canonical)


def compatibility_registration_applies(
    registration: dict[str, Any],
    shader_family: str,
    shader_source: str,
    features: set[str],
) -> bool:
    family = shader_family.encode("utf-8").lower().decode("utf-8")
    source = normalize_shader_source(shader_source, registration.get("identity", "provider"))
    normalized_features = {feature.lower() for feature in features}
    for scope in registration.get("scopes", []):
        kind = scope.get("kind")
        value = str(scope.get("value", "")).encode("utf-8").lower().decode("utf-8")
        if kind == "global":
            return True
        if kind == "shader-family" and value == family:
            return True
        if kind == "shader-source":
            value = normalize_shader_source(
                value, registration.get("identity", "provider")
            )
            if value == source or source.endswith("/" + value):
                return True
        if kind == "feature" and value in normalized_features:
            return True
    return False


def canonical_compatibility_requirement_for_shader(
    registrations: list[dict[str, Any]],
    shader_family: str,
    shader_source: str,
    features: set[str] | None = None,
) -> str:
    return canonical_compatibility_requirement_set(
        applicable_compatibility_registrations(
            registrations, shader_family, shader_source, features
        )
    )


def applicable_compatibility_registrations(
    registrations: list[dict[str, Any]],
    shader_family: str,
    shader_source: str,
    features: set[str] | None = None,
) -> list[dict[str, Any]]:
    return sorted(
        (
            registration
            for registration in registrations
            if compatibility_registration_applies(
                registration,
                shader_family,
                shader_source,
                features or set(),
            )
        ),
        key=lambda registration: registration["identity"],
    )


def sha256_hex(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def validate_shader_pack(
    path: Path,
    expected_lane: int,
    expected_pack_set_id: str | None = None,
    *,
    inspect_record: Callable[[str, str, str, bytes], None] | None = None,
) -> dict[str, int | str]:
    """Validate the exact committed pack format consumed by the C++ runtime."""
    if expected_pack_set_id is not None and not valid_pack_set_id(
        expected_pack_set_id
    ):
        raise SystemExit("expected pack-set identity is invalid or reserved")
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"failed to read shader pack {path}: {exc}") from exc
    if len(data) < 80:
        raise SystemExit(f"shader pack is shorter than its file header: {path}")
    magic, version, lane, generation, pack_set_id, reserved, header_hash = struct.unpack_from(
        "<8sIIQ16sQ32s", data, 0
    )
    if (
        magic != b"CSXSPK1\0"
        or version != PACK_FORMAT_VERSION
        or lane != expected_lane
        or pack_set_id == b"\0" * 16
        or reserved
        or (
            expected_pack_set_id is not None
            and pack_set_id.hex() != expected_pack_set_id
        )
        or header_hash != hashlib.sha256(data[:48]).digest()
    ):
        raise SystemExit(f"shader pack has an invalid file header: {path}")
    offset = 80
    records = 0
    previous_sequence = 0
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
            or sequence <= previous_sequence
            or sequence == 0xFFFFFFFFFFFFFFFF
            or not logical_size
            or not exact_size
            or not bytecode_size
            or payload_size > PACK_MAX_RECORD_SIZE
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
        if inspect_record is not None:
            exact_offset = logical_size + exact_size
            bytecode_offset = exact_offset + metadata_size
            try:
                inspect_record(
                    payload[:logical_size].decode("utf-8"),
                    payload[logical_size:exact_offset].decode("utf-8"),
                    payload[exact_offset:bytecode_offset].decode("utf-8"),
                    payload[bytecode_offset:],
                )
            except UnicodeError as exc:
                raise SystemExit(f"shader pack record text is not UTF-8: {path}") from exc
        records += 1
        previous_sequence = sequence
        offset += total_size
    return {
        "lane": lane,
        "valid": True,
        "generation": generation,
        "recordCount": records,
        "packSetId": pack_set_id.hex(),
    }


def valid_pack_set_id(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{32}", value) is not None
        and value != "0" * 32
    )


def validate_pack_manifest_contract(
    pack_manifest: object,
    expected_runtime: str,
    pack_stats: dict[str, dict[str, int | str]],
    *,
    expected_shader_cache_abi: str | None = None,
    required_compatibility_variants: tuple[str, ...] = (),
) -> dict[str, Any]:
    """Validate the canonical manifest/file contract used by all packagers."""
    if not isinstance(pack_manifest, dict):
        raise SystemExit("managed pack manifest must be an object")

    # JSON booleans and floats are not version integers even though Python's
    # ordinary equality would make True == 1 and 2.0 == 2.
    def manifest_count(value: object, label: str) -> int:
        if (
            isinstance(value, bool)
            or not isinstance(value, int)
            or value < 0
            or value > 0xFFFFFFFFFFFFFFFF
        ):
            raise SystemExit(
                f"managed pack manifest {label} must be a nonnegative integer"
            )
        return value

    pack_set_id = pack_manifest.get("packSetId")
    variants = pack_manifest.get("compatibilityVariants")
    schema_version = manifest_count(
        pack_manifest.get("schemaVersion"), "schemaVersion"
    )
    format_version = manifest_count(
        pack_manifest.get("formatVersion"), "formatVersion"
    )
    if (
        pack_manifest.get("schema") != "csx.shader-cache.pack-manifest"
        or schema_version != PACK_MANIFEST_SCHEMA_VERSION
        or format_version != PACK_FORMAT_VERSION
        or pack_manifest.get("fileStateSemantics") != "installation-baseline-v1"
        or pack_manifest.get("hashAlgorithm") != "sha256"
        or pack_manifest.get("runtime") != expected_runtime
        or not isinstance(pack_manifest.get("shaderCacheABI"), str)
        or not pack_manifest["shaderCacheABI"]
        or not valid_pack_set_id(pack_set_id)
        or not isinstance(variants, list)
        or not variants
        or any(not isinstance(value, str) or not value for value in variants)
        or len(set(variants)) != len(variants)
        or "default" not in variants
    ):
        raise SystemExit("managed pack manifest metadata is invalid")

    if (
        expected_shader_cache_abi is not None
        and pack_manifest["shaderCacheABI"] != expected_shader_cache_abi
    ):
        raise SystemExit("managed pack manifest disagrees with Info.ini ShaderCacheABI")
    if not set(required_compatibility_variants).issubset(variants):
        raise SystemExit("managed pack manifest is missing required compatibility variants")

    if set(pack_stats) != set(PACK_FILE_NAMES):
        raise SystemExit("managed pack validation requires exactly four fixed pack files")

    optimized_count = manifest_count(
        pack_manifest.get("optimizedRecordCount"), "optimizedRecordCount"
    )
    developer_count = manifest_count(
        pack_manifest.get("developerRecordCount"), "developerRecordCount"
    )
    manifest_files = pack_manifest.get("files")
    if not isinstance(manifest_files, dict) or set(manifest_files) != set(
        PACK_FILE_NAMES
    ):
        raise SystemExit(
            "managed pack manifest must describe exactly four fixed pack files"
        )
    for file_name, expected_lane in PACK_LANES.items():
        entry = manifest_files.get(file_name)
        if not isinstance(entry, dict) or set(entry) != {
            "lane",
            "generation",
            "recordCount",
        }:
            raise SystemExit(
                f"managed pack manifest has an invalid entry for {file_name}"
            )
        lane = manifest_count(entry.get("lane"), f"{file_name}.lane")
        manifest_count(entry.get("generation"), f"{file_name}.generation")
        manifest_count(entry.get("recordCount"), f"{file_name}.recordCount")
        if lane != expected_lane:
            raise SystemExit(
                f"managed pack manifest has the wrong lane for {file_name}"
            )

    def adjacent_generations(first: int, second: int) -> bool:
        return abs(first - second) == 1

    for first, second in (
        ("Optimized.A.csxpack", "Optimized.B.csxpack"),
        ("Developer.A.csxpack", "Developer.B.csxpack"),
    ):
        first_generation = manifest_count(
            manifest_files[first]["generation"], f"{first}.generation"
        )
        second_generation = manifest_count(
            manifest_files[second]["generation"], f"{second}.generation"
        )
        if not adjacent_generations(first_generation, second_generation):
            raise SystemExit(
                f"managed pack manifest has ambiguous A/B baseline generations: {first}, {second}"
            )

    if (
        optimized_count
        != sum(
            manifest_count(
                manifest_files[name]["recordCount"], f"{name}.recordCount"
            )
            for name in PACK_FILE_NAMES
            if name.startswith("Optimized")
        )
        or developer_count
        != sum(
            manifest_count(
                manifest_files[name]["recordCount"], f"{name}.recordCount"
            )
            for name in PACK_FILE_NAMES
            if name.startswith("Developer")
        )
    ):
        raise SystemExit("managed pack manifest disagrees with its pack files")
    for file_name, stats in pack_stats.items():
        expected_lane = PACK_LANES[file_name]
        actual_lane = manifest_count(
            stats.get("lane", expected_lane), f"actual {file_name}.lane"
        )
        actual_generation = manifest_count(
            stats.get("generation"), f"actual {file_name}.generation"
        )
        actual_record_count = manifest_count(
            stats.get("recordCount"), f"actual {file_name}.recordCount"
        )
        baseline = manifest_files[file_name]
        baseline_generation = manifest_count(
            baseline["generation"], f"{file_name}.generation"
        )
        baseline_record_count = manifest_count(
            baseline["recordCount"], f"{file_name}.recordCount"
        )
        if stats.get("valid", True) is not True or actual_lane != expected_lane:
            raise SystemExit("managed pack manifest disagrees with its pack files")
        if stats.get("packSetId") != pack_set_id:
            raise SystemExit("managed pack manifest disagrees with its pack files")
        if actual_generation < baseline_generation or (
            actual_generation == baseline_generation
            and actual_record_count < baseline_record_count
        ):
            raise SystemExit("managed pack manifest disagrees with its pack files")
    for first, second in (
        ("Optimized.A.csxpack", "Optimized.B.csxpack"),
        ("Developer.A.csxpack", "Developer.B.csxpack"),
    ):
        if int(pack_stats[first]["generation"]) == int(
            pack_stats[second]["generation"]
        ):
            raise SystemExit(
                f"managed cache lane has ambiguous A/B generations: {first}, {second}"
            )
    return pack_manifest


def shader_pack_record_identity(
    relative: str,
    content_contract: str,
    registrations: list[dict[str, Any]],
) -> dict[str, str]:
    family = relative.split("/", 1)[0].lower()
    applicable = applicable_compatibility_registrations(
        registrations, family, f"Shaders/{family}.hlsl"
    )
    requirement = canonical_compatibility_requirement_set(applicable)
    domain = canonical_compatibility_domain_set(applicable)
    logical_key = f"{relative}|compat-domain={sha256_hex(domain)}"
    return {
        "logicalKey": logical_key,
        "exactKey": (
            f"{logical_key}|content={content_contract}|compat={sha256_hex(requirement)}"
        ),
        "metadata": json.dumps(
            {
                "schemaVersion": 3,
                "contentContract": content_contract,
                "compatibilityDomain": domain,
                "compatibilityRequirementSet": requirement,
                "compatibilityRanges": [
                    {
                        field: registration[field]
                        for field in (
                            "identity", "contractMajor", "currentMinor",
                            "minimumCompatibleMinor", "maximumCompatibleMinor",
                        )
                    }
                    for registration in applicable
                ],
            },
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ),
    }


class PackagedCompatibilityInventory:
    """Verify exported record identities and the coverage of declared variants."""

    def __init__(self, source_root: Path, declared_variants: list[str]) -> None:
        variants = compatibility_variant_manifest(source_root)
        if (
            not isinstance(declared_variants, list)
            or not declared_variants
            or any(not isinstance(name, str) or name not in variants for name in declared_variants)
        ):
            raise SystemExit("packaged cache declares an unknown compatibility variant")
        self.variants = {name: variants[name] for name in declared_variants}
        self.records: dict[str, dict[str, tuple[str, str, str | None, bytes]]] = {}

    def inspector(self, file_name: str) -> Callable[[str, str, str, bytes], None]:
        records = self.records.setdefault(file_name, {})

        def inspect(logical: str, exact: str, metadata_text: str, bytecode: bytes) -> None:
            error = None
            try:
                validate_dxbc(bytecode, Path(logical.split("|", 1)[0]).suffix)
            except ValueError as exc:
                error = str(exc)
            # Only visible records participate in admission; obsolete generations can be ignored.
            records[exact] = (logical, metadata_text, error, hashlib.sha256(bytecode).digest())

        return inspect

    def identify(self, logical: str, exact: str, metadata_text: str, bytecode_error: str | None) -> tuple[set[str], tuple[str, str]]:
        relative = logical.split("|", 1)[0]
        if not re.fullmatch(r"[A-Za-z0-9_-]+/[0-9A-F]{1,8}(?:_[0-9A-F]{8})?\.(?:pso|vso|cso)", relative):
            raise SystemExit(f"packaged shader record has an invalid cache path: {relative!r}")
        try:
            metadata = json.loads(metadata_text)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"packaged shader record has invalid metadata: {relative}") from exc
        content = metadata.get("contentContract") if isinstance(metadata, dict) else None
        if not isinstance(content, str) or not re.fullmatch(r"[0-9a-f]{32}", content):
            raise SystemExit(f"packaged shader record has invalid content identity: {relative}")
        if bytecode_error is not None:
            raise SystemExit(f"invalid packaged shader bytecode {relative}: {bytecode_error}")
        matching = set()
        for name, variant in self.variants.items():
            expected = shader_pack_record_identity(relative, content, variant["registrations"])
            if logical == expected["logicalKey"] and exact == expected["exactKey"] and metadata_text == expected["metadata"]:
                matching.add(name)
        if not matching:
            raise SystemExit(f"packaged shader record disagrees with its canonical compatibility identity: {relative}")
        return matching, (relative, content)

    def validate(self, pack_stats: dict[str, dict[str, int | str]]) -> None:
        names = ("Optimized.A.csxpack", "Optimized.B.csxpack")
        newest = max(int(pack_stats[name]["generation"]) for name in names)
        visible = {}
        for name in sorted(names, key=lambda value: int(pack_stats[value]["generation"])):
            generation = int(pack_stats[name]["generation"])
            if generation == newest or (generation != 0 and generation + 1 == newest):
                visible.update(self.records.get(name, {}))
        coverage: dict[str, dict[str, dict[str, bytes]]] = {name: {} for name in self.variants}
        exclusive = set()
        for exact, (logical, metadata, bytecode_error, digest) in visible.items():
            matching, (relative, content) = self.identify(logical, exact, metadata, bytecode_error)
            for name in matching:
                coverage[name].setdefault(relative, {})[content] = digest
            if len(matching) == 1:
                exclusive.update(matching)
        default = coverage.get("default", {})
        if not default:
            raise SystemExit("packaged cache has no default shader records")
        for name, artifacts in coverage.items():
            if artifacts.keys() != default.keys() or (len(coverage) > 1 and name not in exclusive):
                raise SystemExit(
                    f"packaged cache has incomplete compatibility record coverage for {name}: "
                    f"missing={sorted(default.keys() - artifacts.keys())[:5]}, unexpected={sorted(artifacts.keys() - default.keys())[:5]}"
                )
            changed = False
            for relative, contents in artifacts.items():
                shared = contents.keys() & default[relative].keys()
                source = f"{relative.split('/', 1)[0]}.hlsl"
                changes_defines = bool(self.variants[name].get("shaderDefinesBySource", {}).get(source))
                if not changes_defines and not shared:
                    raise SystemExit(f"packaged cache compatibility records disagree on content coverage: {name}/{relative}")
                if changes_defines:
                    if shared:
                        raise SystemExit(
                            f"packaged cache compatibility record does not reflect its shader defines: {name}/{relative}"
                        )
                    changed |= set(contents.values()) != set(default[relative].values())
                else:
                    changed |= any(contents[content] != default[relative][content] for content in shared)
            if name != "default" and not changed:
                raise SystemExit(f"packaged cache compatibility variant has no changed shader bytecode: {name}")


def write_shader_pack(
    path: Path,
    lane: int,
    generation: int,
    entries: list[dict[str, Any]],
    pack_set_id: str,
) -> None:
    if not valid_pack_set_id(pack_set_id):
        raise SystemExit(
            "pack-set identity must be 16 nonzero lower-case hexadecimal bytes"
        )
    file_prefix = struct.pack(
        "<8sIIQ16sQ",
        b"CSXSPK1\0",
        PACK_FORMAT_VERSION,
        lane,
        generation,
        bytes.fromhex(pack_set_id),
        0,
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
            if not logical or not exact or not bytecode or len(payload) > PACK_MAX_RECORD_SIZE:
                raise SystemExit(f"shader pack record exceeds runtime format limits: {entry.get('exactKey')!r}")
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
            registrations = (
                variants["legacy-horizon-fix"]["registrations"]
                if horizon_enabled
                else variants["default"]["registrations"]
            )
            identity = shader_pack_record_identity(relative, content_contract, registrations)
            exact_key = identity["exactKey"]
            bytecode = blob_path.read_bytes()
            previous = records.get(exact_key)
            if previous and previous["bytecode"] != bytecode:
                raise SystemExit(f"{runtime}: identical pack identity has differing bytecode: {relative}")
            records.setdefault(
                exact_key,
                {
                    **identity,
                    "bytecode": bytecode,
                },
            )
            count += 1
        variant_counts[variant_name] = count

    ordered = [records[key] for key in sorted(records)]
    pack_set_material = json.dumps(
        {
            "runtime": runtime,
            "shaderCacheABI": shader_cache_abi,
            "records": [entry["exactKey"] for entry in ordered],
        },
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    )
    pack_set_id = hashlib.sha256(pack_set_material.encode("utf-8")).hexdigest()[:32]
    pack_files = {
        "Optimized.A.csxpack": (1, 1, ordered),
        "Optimized.B.csxpack": (1, 0, []),
        "Developer.A.csxpack": (2, 1, []),
        "Developer.B.csxpack": (2, 0, []),
    }
    for file_name, (lane, generation, entries) in pack_files.items():
        write_shader_pack(
            standard_cache / file_name,
            lane,
            generation,
            entries,
            pack_set_id,
        )
    pack_manifest = {
        "schema": "csx.shader-cache.pack-manifest",
        "schemaVersion": PACK_MANIFEST_SCHEMA_VERSION,
        "formatVersion": PACK_FORMAT_VERSION,
        "fileStateSemantics": "installation-baseline-v1",
        "hashAlgorithm": "sha256",
        "packSetId": pack_set_id,
        "runtime": runtime,
        "shaderCacheABI": shader_cache_abi,
        "optimizedRecordCount": len(ordered),
        "developerRecordCount": 0,
        "compatibilityVariants": [
            "default",
            *(["legacy-horizon-fix"] if horizon_cache is not None else []),
        ],
        "files": {
            file_name: {
                "lane": lane,
                "generation": generation,
                "recordCount": len(entries),
            }
            for file_name, (lane, generation, entries) in pack_files.items()
        },
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
    (standard_cache / MANIFEST_FILE_NAME).unlink(missing_ok=True)
    if horizon_cache is not None:
        shutil.rmtree(horizon_cache)
    print(
        f"{runtime}: packed {len(ordered)} unique optimized records from "
        + ", ".join(f"{count} {name} blobs" for name, count in variant_counts.items())
    )
    return variant_counts


BASE_EXCLUDED_DEFINES = frozenset(DEBUG_PROFILE_DEFINES)
SHIPPED_CACHE_PROFILE = CacheProfile(
    name="shipped",
    display_name="Shipped",
    supported_runtimes=frozenset({TARGET_RUNTIME}),
    disabled_features=frozenset({"HorizonFix"}),
    excluded_defines=BASE_EXCLUDED_DEFINES,
    global_defines=("UNIFIED_WATER",),
    file_defines={
        "Lighting.hlsl": ("WETTERNESS",),
        "Water.hlsl": ("WETTERNESS",),
        "RunGrass.hlsl": ("PBR_GRASS=1", "GRASS_OPTIMIZATIONS", "WETTERNESS"),
    },
)

CACHE_PROFILES = {SHIPPED_CACHE_PROFILE.name: SHIPPED_CACHE_PROFILE}

IMAGESPACE_DIRS = {
    0: 'WorldMap',
    1: 'Refraction',
    2: 'ISFXAA',
    3: 'DepthOfField',
    5: 'RadialBlur',
    6: 'FullScreenBlur',
    7: 'GetHit',
    8: 'Map',
    9: 'Blur3',
    10: 'Blur5',
    11: 'Blur7',
    12: 'Blur9',
    13: 'Blur11',
    14: 'Blur13',
    15: 'Blur15',
    16: 'BlurNonHDR3',
    17: 'BlurNonHDR5',
    18: 'BlurNonHDR7',
    19: 'BlurNonHDR9',
    20: 'BlurNonHDR11',
    21: 'BlurNonHDR13',
    22: 'BlurNonHDR15',
    23: 'BlurBrightPass3',
    24: 'BlurBrightPass5',
    25: 'BlurBrightPass7',
    26: 'BlurBrightPass9',
    27: 'BlurBrightPass11',
    28: 'BlurBrightPass13',
    29: 'BlurBrightPass15',
    30: 'HDR',
    31: 'WaterDisplacement',
    32: 'VolumetricLighting',
    33: 'Noise',
    34: 'ISCopy',
    35: 'ISCopyDynamicFetchDisabled',
    36: 'ISCopyScaleBias',
    37: 'ISCopyCustomViewport',
    38: 'ISCopyGrayScale',
    39: 'ISRefraction',
    40: 'ISDoubleVision',
    41: 'ISCopyTextureMask',
    42: 'ISMap',
    43: 'ISWorldMap',
    44: 'ISWorldMapNoSkyBlur',
    45: 'ISDepthOfField',
    46: 'ISDepthOfFieldFogged',
    47: 'ISDepthOfFieldMaskedFogged',
    49: 'ISDistantBlur',
    50: 'ISDistantBlurFogged',
    51: 'ISDistantBlurMaskedFogged',
    52: 'ISRadialBlur',
    53: 'ISRadialBlurMedium',
    54: 'ISRadialBlurHigh',
    55: 'ISHDRTonemapBlendCinematic',
    56: 'ISHDRTonemapBlendCinematicFade',
    57: 'ISHDRDownSample16',
    58: 'ISHDRDownSample4',
    59: 'ISHDRDownSample16Lum',
    60: 'ISHDRDownSample4RGB2Lum',
    61: 'ISHDRDownSample4LumClamp',
    62: 'ISHDRDownSample4LightAdapt',
    63: 'ISHDRDownSample16LumClamp',
    64: 'ISHDRDownSample16LightAdapt',
    65: 'ISBlur3',
    66: 'ISBlur5',
    67: 'ISBlur7',
    68: 'ISBlur9',
    69: 'ISBlur11',
    70: 'ISBlur13',
    71: 'ISBlur15',
    72: 'ISNonHDRBlur3',
    73: 'ISNonHDRBlur5',
    74: 'ISNonHDRBlur7',
    75: 'ISNonHDRBlur9',
    76: 'ISNonHDRBlur11',
    77: 'ISNonHDRBlur13',
    78: 'ISNonHDRBlur15',
    79: 'ISBrightPassBlur3',
    80: 'ISBrightPassBlur5',
    81: 'ISBrightPassBlur7',
    82: 'ISBrightPassBlur9',
    83: 'ISBrightPassBlur11',
    84: 'ISBrightPassBlur13',
    85: 'ISBrightPassBlur15',
    86: 'ISWaterDisplacementClearSimulation',
    87: 'ISWaterDisplacementTexOffset',
    88: 'ISWaterDisplacementWadingRipple',
    89: 'ISWaterDisplacementRainRipple',
    90: 'ISWaterWadingHeightmap',
    91: 'ISWaterRainHeightmap',
    92: 'ISWaterBlendHeightmaps',
    93: 'ISWaterSmoothHeightmap',
    94: 'ISWaterDisplacementNormals',
    95: 'ISNoiseScrollAndBlend',
    96: 'ISNoiseNormalmap',
    97: 'ISVolumetricLighting',
    98: 'ISLocalMap',
    99: 'ISAlphaBlend',
    100: 'ISLensFlare',
    101: 'ISLensFlareVisibility',
    102: 'ISApplyReflections',
    103: 'ISApplyVolumetricLighting',
    104: 'ISBasicCopy',
    105: 'ISBlur',
    106: 'ISVolumetricLightingBlurHCS',
    107: 'ISVolumetricLightingBlurVCS',
    108: 'ISReflectionBlurHCS',
    109: 'ISReflectionBlurVCS',
    110: 'ISParallaxMaskBlurHCS',
    111: 'ISParallaxMaskBlurVCS',
    112: 'ISDepthOfFieldBlurHCS',
    113: 'ISDepthOfFieldBlurVCS',
    114: 'ISCompositeVolumetricLighting',
    115: 'ISCompositeLensFlare',
    116: 'ISCompositeLensFlareVolumetricLighting',
    117: 'ISCopySubRegionCS',
    118: 'ISDebugSnow',
    119: 'ISDownsample',
    120: 'ISDownsampleIgnoreBrightest',
    121: 'ISDownsampleCS',
    122: 'ISDownsampleIgnoreBrightestCS',
    123: 'ISExp',
    124: 'ISIBLensFlares',
    125: 'ISLightingComposite',
    126: 'ISLightingCompositeNoDirectionalLight',
    127: 'ISLightingCompositeMenu',
    128: 'ISPerlinNoiseCS',
    129: 'ISPerlinNoise2DCS',
    130: 'ReflectionsRayTracing',
    131: 'ISReflectionsDebugSpecMask',
    132: 'ISSAOBlurH',
    133: 'ISSAOBlurV',
    134: 'ISSAOBlurHCS',
    135: 'ISSAOBlurVCS',
    136: 'ISSAOCameraZ',
    137: 'ISSAOCameraZAndMipsCS',
    138: 'ISSAOCompositeSAO',
    139: 'ISSAOCompositeFog',
    140: 'ISSAOCompositeSAOFog',
    141: 'ISMinify',
    142: 'ISMinifyContrast',
    143: 'ISSAORawAO',
    144: 'ISSAORawAONoTemporal',
    145: 'ISSAORawAOCS',
    146: 'ISSILComposite',
    147: 'ISSILRawInd',
    148: 'ISSimpleColor',
    149: 'ISDisplayDepth',
    150: 'ISSnowSSS',
    151: 'ISTemporalAA',
    152: 'ISTemporalAA_UI',
    153: 'ISTemporalAA_Water',
    154: 'ISUpsampleDynamicResolution',
    155: 'ISWaterBlend',
    156: 'ISUnderwaterMask',
    157: 'ISWaterFlow',
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
        abi_match = FEATURE_SHADER_ABI_PATTERN.search(contents)
        shader_abi = abi_match.group(1) if abi_match else ("1" if shader_define else None)
        contract = FeatureContract(
            short_name, packages[short_name], shader_define, shader_abi
        )
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
    return {TARGET_RUNTIME: source_root / ".github/configs/shader-validation.yaml"}


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
        frozenset()
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
    additional_file_defines: dict[str, tuple[str, ...]] | None = None,
    add_cross_modlist_variants: bool = False,
) -> object:
    profile_file_defines = dict(profile.file_defines)
    for file_name, defines in (additional_file_defines or {}).items():
        profile_file_defines[file_name] = (
            *profile_file_defines.get(file_name, ()),
            *defines,
        )

    # Captures may contain older global scopes; reapply only the selected files.
    scoped_defines = {
        define for defines in profile_file_defines.values() for define in defines
    }
    excluded_defines = {
        normalized_define_name(define)
        for define in (
            profile.excluded_defines | additional_excluded_defines | scoped_defines
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
    """Validate inventory size while retaining capture provenance after normalization."""
    expected = config.get(CAPTURED_VARIANT_COUNT_KEY)
    normalized = config.get(NORMALIZED_VARIANT_COUNT_KEY)
    if expected is None:
        if NORMALIZED_VARIANT_COUNT_KEY in config:
            raise SystemExit(f"normalized shader config requires its captured count: {config_path}")
        return
    if isinstance(expected, bool) or not isinstance(expected, int) or expected < 1:
        raise SystemExit(
            f"shader config {CAPTURED_VARIANT_COUNT_KEY} must be a positive integer: "
            f"{config_path}"
        )

    captured = expected
    if NORMALIZED_VARIANT_COUNT_KEY in config:
        if isinstance(normalized, bool) or not isinstance(normalized, int) or not 0 < normalized <= captured:
            raise SystemExit(
                f"shader config {NORMALIZED_VARIANT_COUNT_KEY} must be a positive integer "
                f"no greater than its captured count: {config_path}"
            )
        expected = normalized

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
            f"but its inventory metadata declares {expected}: {config_path}"
        )
    if normalized is not None:
        print(f"shader config: validated {actual} normalized variants from {captured} captured entries: {config_path}")
    else:
        print(f"shader config: validated {actual} captured variants from {config_path}")


def character_category_flags(source_root: Path) -> dict[str, int]:
    """Read reserved descriptor bits from the runtime's category accessor."""
    header = (source_root / "src/ShaderCache.h").read_text(encoding="utf-8")
    accessor = re.search(
        r"GetCharacterCategoryFlag\([^)]*\)\s*\{(.*?)\n\t\t\}",
        header,
        re.DOTALL,
    )
    if accessor is None:
        raise SystemExit("cannot locate runtime character category flags")
    flags: dict[str, int] = {}
    for cases, bit_text in re.findall(
        r"((?:\s*case RE::BSShader::Type::\w+:)+)\s*return 1u << (\d+);",
        accessor[1],
    ):
        bit = int(bit_text)
        if bit >= 32:
            raise SystemExit("character category bit exceeds shader descriptor")
        for family in re.findall(r"case RE::BSShader::Type::(\w+):", cases):
            if family in flags:
                raise SystemExit("duplicate runtime character category family")
            flags[family] = 1 << bit
    families = re.findall(r"case RE::BSShader::Type::(\w+):", accessor[1])
    if not flags or set(families) != set(flags):
        raise SystemExit("unsupported runtime character category flag layout")
    return flags


def append_character_category_variants(
    config: dict[str, Any], source_root: Path
) -> int:
    """Cover both deferred output layouts without changing captured provenance."""
    flags = character_category_flags(source_root)
    added = 0
    for shader in config["shaders"]:
        source = shader["file"]
        family = "Grass" if source == "RunGrass.hlsl" else Path(source).stem
        if family not in flags:
            continue
        stage = shader["configs"].get("PSHADER")
        if stage is None:
            continue
        shared_defines = [
            *config.get("common_defines", []),
            *config.get("common_pshader_defines", []),
            *config.get("file_common_defines", {}).get(source, {}).get("PSHADER", []),
            *stage.get("common_defines", []),
        ]
        shared_names = {normalized_define_name(value) for value in shared_defines}
        if "CHARACTER_CATEGORY" in shared_names:
            raise SystemExit("character category must be scoped to a pixel entry")
        entries = stage["entries"]
        by_key = {entry["entry"]: entry for entry in entries}
        if len(by_key) != len(entries):
            raise SystemExit(f"duplicate pixel entries for {source}")
        for entry in tuple(entries):
            defines = entry.get("defines", [])
            names = {normalized_define_name(value) for value in defines}
            # Grass defines its deferred output layout inside the source.
            if family != "Grass" and "DEFERRED" not in names | shared_names:
                continue
            match = re.fullmatch(
                rf"{family}:Pixel:([0-9A-F]{{1,8}})", entry["entry"]
            )
            if match is None:
                raise SystemExit(f"invalid deferred pixel entry for {source}")
            descriptor = int(match[1], 16)
            flag = flags[family]
            if bool(descriptor & flag) != ("CHARACTER_CATEGORY" in names):
                raise SystemExit(f"category flag/macros disagree: {entry['entry']}")
            other_descriptor = descriptor ^ flag
            other_defines = [
                value for value in defines
                if normalized_define_name(value) != "CHARACTER_CATEGORY"
            ]
            if other_descriptor & flag:
                other_defines.append("CHARACTER_CATEGORY")
            other_key = f"{family}:Pixel:{other_descriptor:X}"
            existing = by_key.get(other_key)
            if existing is not None:
                def identity(defines: list[str]) -> dict[tuple[str, str], str]:
                    return compile_task_defines([(
                        source, "PSHADER", other_key,
                        [value if "=" in value else value + "=" for value in defines],
                    )])
                if identity(existing.get("defines", [])) != identity(other_defines):
                    raise SystemExit(f"conflicting category variant: {other_key}")
                continue
            other = {**entry, "entry": other_key, "defines": other_defines}
            entries.append(other)
            by_key[other_key] = other
            added += 1
    return added


def filter_profile_defines(
    config_path: Path,
    out_path: Path,
    yaml: Any,
    profile: CacheProfile,
    *,
    additional_excluded_defines: frozenset[str] = frozenset(),
    additional_file_defines: dict[str, tuple[str, ...]] | None = None,
    add_cross_modlist_variants: bool = False,
    source_root: Path = REPO,
) -> Path:
    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise SystemExit(f"shader config must be a YAML mapping: {config_path}")

    validate_captured_variant_count(config, config_path)
    config = apply_cache_profile_defines(
        config,
        profile,
        additional_excluded_defines=additional_excluded_defines,
        additional_file_defines=additional_file_defines,
        add_cross_modlist_variants=add_cross_modlist_variants,
    )
    category_count = append_character_category_variants(config, source_root)
    print(f"shader config: added {category_count} deferred character category variants")
    prepare_fxc_defines(config)
    out_path.write_text(
        yaml.safe_dump(config, sort_keys=False),
        encoding="utf-8",
    )
    return out_path


def remap_imagespace_dirs(cache_dir: Path, runtime: str) -> dict[str, str]:
    """Move ImageSpace blobs to the technique directories used by the runtime.

    Return the destination-to-source mapping required when hashing those blobs.
    """
    by_descriptor = IMAGESPACE_DIRS
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
    compile_tasks: list[tuple[str, str, str, list[str]]],
) -> int:
    """Hash source/include content and the resolved macros for every blob."""
    global_defines_state = f"ShaderCacheABI={shader_cache_abi};"
    info = configparser.ConfigParser(interpolation=None)
    try:
        with (cache_dir / INFO_FILE_NAME).open("r", encoding="utf-8-sig") as stream:
            info.read_file(stream)
    except (configparser.Error, OSError, UnicodeError) as exc:
        raise SystemExit(
            f"cannot derive feature shader ABI state from {cache_dir / INFO_FILE_NAME}: {exc}"
        ) from exc
    feature_abis = sorted(
        (section, info.get(section, "ShaderCacheABI"))
        for section in info.sections()
        if section != "Cache"
        and info.getboolean(section, "Enabled", fallback=False)
        and info.has_option(section, "ShaderCacheABI")
    )
    global_defines_state += "".join(
        f"FeatureShaderABI={name}:{abi};" for name, abi in feature_abis
    )
    count = write_manifest(
        cache_dir,
        shader_root,
        global_defines_state,
        cache_dir / MANIFEST_FILE_NAME,
        resolve_source_name=lambda name: imagespace_remap.get(name, name),
        compile_tasks=compile_tasks,
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
    source_root: Path,
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
        frozenset()
        if excluded_features is None
        else frozenset(excluded_features)
    )
    enabled_overrides = enabled_overrides or {}
    contracts = feature_contracts(source_root)

    for ini_path in sorted((stage / "Features").glob("*.ini")):
        stem = ini_path.stem
        if stem in excluded_features:
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
        lines += [f"[{stem}]", f"Enabled = {enabled}", f"Version = {version}"]
        if contract := contracts.get(stem):
            if contract.shader_abi:
                lines.append(f"ShaderCacheABI = {contract.shader_abi}")
        lines += ["", ""]
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
            validate_dxbc(blob_path.read_bytes(), blob_path.suffix)
        except (OSError, ValueError) as exc:
            invalid_blobs.append(f"{relative_path}: {exc}")

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
    compatibility_tag = validate_csx_plugin_version(plugin_version)
    return compatibility_tag if profile.name == SHIPPED_CACHE_PROFILE.name else f"{compatibility_tag}-{profile.name}"


def require_compile_tools() -> tuple[tuple[str, ...], Any, Callable[..., int]]:
    try:
        import yaml
        from hlslkit import compile_shaders
        from hlslkit.shader_digest import SCHEMA_VERSION as SOURCE_DIGEST_SCHEMA
        from shader_cache_manifest import write_manifest
    except (ImportError, ModuleNotFoundError) as exc:
        raise SystemExit(
            "PyYAML and the pinned hlslkit revision are required; see "
            "tools/shader-cache-requirements.txt and "
            "docs/development/prebuilt-shader-cache.md"
        ) from exc

    if SOURCE_DIGEST_SCHEMA != 1:
        raise SystemExit(
            "shader digest schemas do not match this builder: "
            f"source={SOURCE_DIGEST_SCHEMA}, expected=1"
        )

    # Running the adapter through this interpreter guarantees the compiler and
    # manifest writer come from the same hlslkit installation. A PATH command
    # can otherwise point at a different version and silently break the digest
    # contract with the runtime.
    return (sys.executable, str(TOOLS_DIRECTORY / "shader_cache_compile.py")), yaml, write_manifest


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

    staging_owned = False
    try:
        if source.is_dir():
            staging.mkdir()
            staging_owned = True
            shutil.copytree(source, staging, dirs_exist_ok=True)
        else:
            with staging.open("xb"):
                staging_owned = True
            shutil.copy2(source, staging)
    except OSError as exc:
        if staging_owned:
            discard_publication_staging(staging)
        raise SystemExit(f"failed to stage {label} for publication: {staging}") from exc


def publish_runtime_cache(candidate_root: Path, out_root: Path, runtime: str) -> Path:
    """Publish a fully validated cache while preserving the previous cache on failure."""
    destination = runtime_output_destination(out_root, runtime)
    staging = out_root / f".{runtime}.publishing"
    recovery_root = out_root / f".{runtime}.previous"
    if path_entry_exists(recovery_root):
        raise SystemExit(
            f"refusing to publish while a previous {runtime} cache recovery "
            f"path exists: {recovery_root}"
        )
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

    try:
        recovery_root.mkdir()
    except OSError as exc:
        discard_publication_staging(staging)
        raise SystemExit(
            f"could not reserve the {runtime} cache recovery path: {recovery_root}"
        ) from exc
    backup = recovery_root / runtime

    try:
        destination.replace(backup)
    except OSError as exc:
        discard_publication_staging(staging)
        discard_publication_staging(recovery_root)
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
                f"retained at {staging}; previous cache retained at {backup}"
            ) from restore_error
        discard_publication_staging(recovery_root)
        raise SystemExit(
            f"failed to publish {runtime} cache: {destination}; "
            f"validated staging retained at {staging} ({exc})"
        ) from exc

    try:
        remove_publication_staging(recovery_root)
    except OSError as exc:
        print(
            f"warning: published {runtime} cache but could not remove previous "
            f"cache recovery path {recovery_root}: {exc}"
        )
    return destination


def cache_variants_for(profile: CacheProfile) -> tuple[CacheVariant, ...]:
    """Return the one managed install cache."""
    return (STANDARD_CACHE_VARIANT,)


def compile_variants_for(
    profile: CacheProfile, include_horizon_fix: bool = True
) -> tuple[CacheVariant, ...]:
    """Return loose variants required as inputs to the managed pack."""
    if include_horizon_fix and profile.name == SHIPPED_CACHE_PROFILE.name:
        return CACHE_VARIANTS
    return (STANDARD_CACHE_VARIANT,)


def validate_cache_archive(
    archive: Path,
    cmake: str,
    runtime: str,
    plugin_version: str,
    *,
    horizon_variants: bool | None = None,
    source_root: Path = REPO,
) -> None:
    """Verify the raw runtime archive preserves one complete managed cache."""
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
    variants = (STANDARD_CACHE_VARIANT,)
    required_entries = {
        *(
            f"{variant.directory}/{metadata}"
            for variant in variants
            for metadata in (INFO_FILE_NAME,)
        ),
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

    unexpected_cache_files = sorted(
        entry
        for entry in entries
        if entry not in required_entries
        and entry.rstrip("/") != CACHE_DIRECTORY
    )
    if unexpected_cache_files:
        raise SystemExit(
            f"packaged {runtime} cache contains unexpected managed-cache files: "
            f"{', '.join(unexpected_cache_files)}"
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

        validate_managed_cache(
            extract_root / CACHE_DIRECTORY, runtime, plugin_version,
            source_root=source_root, horizon_variants=horizon_variants,
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
    source_root: Path = REPO,
    include_horizon_fix: bool = True,
) -> Path:
    """Create a validated candidate archive without changing published output."""
    variants = cache_variants_for(profile)
    horizon_variants = HORIZON_FIX_CACHE_VARIANT in compile_variants_for(profile, include_horizon_fix)
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
        source_root=source_root,
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
    include_horizon_fix: bool = True,
) -> tuple[Path, dict[str, int], int]:
    runtime_root = workspace / runtime
    shader_contract = shader_contract_identity(
        source_root, DEFAULT_SHADER_CONTRACT_FILES
    )
    shader_cache_abi = sha256_bytes(canonical_bytes(shader_contract))
    variants = compile_variants_for(profile, include_horizon_fix)
    compatibility_variants = compatibility_variant_manifest(source_root)
    has_horizon_variant = HORIZON_FIX_CACHE_VARIANT in variants
    if has_horizon_variant and distribution_profile is None:
        raise SystemExit("shipped cache variants require the Horizon Fix contract")

    build_results: dict[str, tuple[Path, int, int, dict[str, bool]]] = {}
    for variant in variants:
        cache_dir = runtime_root / variant.directory
        cache_dir.mkdir(parents=True, exist_ok=True)

        additional_excluded_defines = frozenset()
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
            additional_excluded_defines |= distribution_profile.excluded_defines
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
            additional_file_defines=additional_file_defines,
            add_cross_modlist_variants=add_cross_modlist,
            source_root=source_root,
        )
        from hlslkit.compile_shaders import parse_shader_configs
        compile_tasks = parse_shader_configs(str(filtered_config))
        compile_task_defines(compile_tasks)
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
        section_count = write_info_ini(
            cache_dir,
            stage,
            source_root,
            plugin_version,
            runtime,
            profile,
            shader_cache_abi,
            excluded_features=excluded_features,
            enabled_overrides=enabled_overrides,
        )
        write_shader_cache_manifest(
            cache_dir,
            stage,
            runtime,
            imagespace_remap,
            write_manifest,
            shader_cache_abi,
            compile_tasks,
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


def ordinary_tree_files(root: Path) -> dict[str, str]:
    """Inventory regular payload files without following links or junctions."""
    if not root.is_dir() or root.is_symlink() or getattr(root, "is_junction", lambda: False)():
        raise SystemExit(f"payload must be an ordinary directory: {root}")
    result: dict[str, str] = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or getattr(path, "is_junction", lambda: False)():
            raise SystemExit(f"payload contains a linked path: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise SystemExit(f"payload contains a non-regular file: {path}")
        with path.open("rb") as stream:
            result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result


def validate_managed_cache(
    cache_dir: Path,
    runtime: str,
    plugin_version: str,
    *,
    horizon_variants: bool | None = None,
    source_root: Path = REPO,
    expected_shader_cache_abi: str | None = None,
) -> configparser.ConfigParser:
    """Validate one complete managed pack set before publication or assembly."""
    expected_files = {*PACK_FILE_NAMES, INFO_FILE_NAME, PACK_MANIFEST_FILE_NAME}
    files = ordinary_tree_files(cache_dir)
    if set(files) != expected_files or any(path.is_dir() for path in cache_dir.iterdir()):
        raise SystemExit(
            f"managed cache must contain exactly six root files: {cache_dir}; "
            f"missing={sorted(expected_files - files.keys())}, "
            f"unexpected={sorted(files.keys() - expected_files)}"
        )
    info = configparser.ConfigParser(interpolation=None)
    try:
        with (cache_dir / INFO_FILE_NAME).open("r", encoding="utf-8-sig") as stream:
            info.read_file(stream)
    except (configparser.Error, OSError, UnicodeError) as exc:
        raise SystemExit(f"managed cache has invalid {INFO_FILE_NAME}: {exc}") from exc
    actual_version = info.get("Cache", "PluginVersion", fallback=None)
    if actual_version != plugin_version:
        raise SystemExit(
            f"managed cache plugin version is {actual_version!r}; expected {plugin_version!r}"
        )
    shader_abi = info.get("Cache", "ShaderCacheABI", fallback=None)
    if not shader_abi:
        raise SystemExit("managed cache has no ShaderCacheABI")
    if expected_shader_cache_abi is not None and shader_abi != expected_shader_cache_abi:
        raise SystemExit("managed cache ShaderCacheABI does not match the source contract")
    try:
        manifest = json.loads((cache_dir / PACK_MANIFEST_FILE_NAME).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit("managed cache has an invalid pack manifest") from exc
    if not isinstance(manifest, dict):
        raise SystemExit("managed cache pack manifest must be an object")
    inventory = PackagedCompatibilityInventory(source_root, manifest.get("compatibilityVariants"))
    pack_stats = {
        name: validate_shader_pack(
            cache_dir / name, lane, manifest.get("packSetId"),
            inspect_record=inventory.inspector(name) if lane == 1 else None,
        )
        for name, lane in PACK_LANES.items()
    }
    validate_pack_manifest_contract(
        manifest, runtime, pack_stats,
        expected_shader_cache_abi=shader_abi,
        required_compatibility_variants=(
            ("default", "legacy-horizon-fix") if horizon_variants else ("default",)
        ),
    )
    inventory.validate(pack_stats)
    if horizon_variants is False and "legacy-horizon-fix" in manifest["compatibilityVariants"]:
        raise SystemExit("packaged cache contains excluded Horizon Fix records")
    return info


def validate_aio_inputs(
    core_root: Path,
    cache_root: Path,
    plugin_version: str,
    compatibility_tag: str,
    *,
    source_root: Path = REPO,
    include_horizon_fix: bool = True,
) -> dict[str, str]:
    """Reject partial or mismatched core/cache trees before staging an AIO."""
    core_files = ordinary_tree_files(core_root)
    required = {
        "SKSE/Plugins/CommunityShaders.dll",
        f"SKSE/Plugins/CommunityShaders/{compatibility_tag}.marker",
    }
    if missing := required - core_files.keys():
        raise SystemExit("AIO core is missing required files: " + ", ".join(sorted(missing)))
    for name in (AIO_CORE_DIRECTORY, AIO_CACHE_DIRECTORY, CACHE_DIRECTORY, HORIZON_FIX_CACHE_DIRECTORY, FOMOD_DIRECTORY):
        if path_entry_exists(core_root / name):
            raise SystemExit(f"AIO core already contains integrated staging content: {core_root / name}")
    ordinary_tree_files(cache_root)
    if {path.name for path in cache_root.iterdir()} != {CACHE_DIRECTORY}:
        raise SystemExit("AIO cache input must contain only the managed ShaderCache directory")
    shader_cache_abi = sha256_bytes(canonical_bytes(shader_contract_identity(source_root, DEFAULT_SHADER_CONTRACT_FILES)))
    info = validate_managed_cache(
        cache_root / CACHE_DIRECTORY, TARGET_RUNTIME, plugin_version,
        horizon_variants=include_horizon_fix, source_root=source_root,
        expected_shader_cache_abi=shader_cache_abi,
    )
    core_feature_versions: dict[str, str] = {}
    for path in sorted((core_root / "Shaders/Features").glob("*.ini")):
        parser = configparser.ConfigParser(interpolation=None)
        try:
            with path.open("r", encoding="utf-8-sig") as stream:
                parser.read_file(stream)
        except (configparser.Error, OSError, UnicodeError) as exc:
            raise SystemExit(f"cannot read AIO feature metadata: {path}") from exc
        version = parser.get("Info", "Version", fallback="").strip()
        if not version:
            raise SystemExit(f"AIO feature metadata has no version: {path}")
        core_feature_versions[path.stem] = version
    if not core_feature_versions:
        raise SystemExit("AIO core has no feature metadata")
    cache_features = set(info.sections()) - {"Cache"}
    if cache_features != core_feature_versions.keys():
        raise SystemExit(
            "cache does not match the AIO feature set; "
            f"missing={sorted(core_feature_versions.keys() - cache_features)}, "
            f"unexpected={sorted(cache_features - core_feature_versions.keys())}"
        )
    for feature, version in core_feature_versions.items():
        if info.get(feature, "Version", fallback="").strip() != version:
            raise SystemExit(f"cache does not match the AIO {feature} version")
        try:
            enabled = info.getboolean(feature, "Enabled", fallback=None)
        except ValueError as exc:
            raise SystemExit(f"cache has invalid {feature}/Enabled metadata") from exc
        if enabled is None:
            raise SystemExit(f"cache has no {feature}/Enabled metadata")
    return core_files


def validate_fomod_installer(fomod_dir: Path) -> None:
    """Require one full core and an explicit optional managed-cache mapping."""
    try:
        root = ET.parse(fomod_dir / FOMOD_CONFIG_FILE_NAME).getroot()
        info = ET.parse(fomod_dir / FOMOD_INFO_FILE_NAME).getroot()
    except (ET.ParseError, OSError) as exc:
        raise SystemExit(f"invalid AIO FOMOD metadata: {exc}") from exc
    if root.find("./moduleDependencies") is not None or root.findall(".//fileDependency") or root.findall(".//gameDependency"):
        raise SystemExit("AIO FOMOD must leave cache installation to the user")
    folders = root.findall("./requiredInstallFiles/folder")
    if len(folders) != 1 or folders[0].get("source") != AIO_CORE_DIRECTORY or folders[0].get("destination") != "":
        raise SystemExit("AIO FOMOD must install exactly one complete core tree")
    steps = root.findall("./installSteps/installStep")
    if len(steps) != 1:
        raise SystemExit("AIO FOMOD must contain one cache-or-no-cache page")
    group = steps[0].find("./optionalFileGroups/group")
    if group is None or group.get("type") != "SelectExactlyOne":
        raise SystemExit("AIO FOMOD must require an explicit cache-or-no-cache choice")
    plugins = group.findall("./plugins/plugin")
    types = [plugin.find("./typeDescriptor/type") for plugin in plugins]
    if len(plugins) != 2 or any(value is None for value in types) or [value.get("name") for value in types] != ["Recommended", "Optional"]:
        raise SystemExit("AIO FOMOD must default to installing the optional cache")
    cache_folders = plugins[0].findall("./files/folder")
    if len(cache_folders) != 1 or cache_folders[0].get("source") != AIO_CACHE_DIRECTORY or cache_folders[0].get("destination") != CACHE_DIRECTORY:
        raise SystemExit("AIO cache must install directly at Data/ShaderCache")
    if plugins[1].find("./files") is not None or len(root.findall(".//folder")) != 2:
        raise SystemExit("AIO no-cache choice must install only the required core")
    if info.findtext("./Website") != FOMOD_WEBSITE:
        raise SystemExit("AIO FOMOD is missing its documentation link")


def write_fomod_installer(package_root: Path, label: str, compatibility_tag: str) -> None:
    """Offer the managed cache without changing any SE core payload mapping."""
    directory = package_root / FOMOD_DIRECTORY
    if path_entry_exists(directory):
        raise SystemExit(f"refusing to replace existing FOMOD metadata: {directory}")
    directory.mkdir()
    config = ET.Element("config", {
        "xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
        "xsi:noNamespaceSchemaLocation": "http://qconsulting.ca/fo3/ModConfig5.0.xsd",
    })
    ET.SubElement(config, "moduleName").text = f"CSX AIO - {compatibility_tag}"
    required = ET.SubElement(config, "requiredInstallFiles")
    ET.SubElement(required, "folder", source=AIO_CORE_DIRECTORY, destination="", priority="0")
    steps = ET.SubElement(config, "installSteps", order="Explicit")
    step = ET.SubElement(steps, "installStep", name="Choose shader-cache installation")
    groups = ET.SubElement(step, "optionalFileGroups", order="Explicit")
    group = ET.SubElement(groups, "group", name="Prebuilt shader cache", type="SelectExactlyOne")
    plugins = ET.SubElement(group, "plugins", order="Explicit")
    for install_cache, title, description in (
        (True, "Install the prebuilt shader cache", "Matching shaders load from the managed cache; missing or changed permutations compile locally. Horizon Fix compatibility is selected automatically at runtime."),
        (False, "Do not install a prebuilt shader cache", "Install the complete core and compile all required shader permutations locally in game."),
    ):
        plugin = ET.SubElement(plugins, "plugin", name=title)
        ET.SubElement(plugin, "description").text = description
        if install_cache:
            files = ET.SubElement(plugin, "files")
            ET.SubElement(files, "folder", source=AIO_CACHE_DIRECTORY, destination=CACHE_DIRECTORY, priority="0")
        descriptor = ET.SubElement(plugin, "typeDescriptor")
        ET.SubElement(descriptor, "type", name="Recommended" if install_cache else "Optional")
    info = ET.Element("fomod")
    for name, value in (
        ("Name", f"CSX AIO - {compatibility_tag}"), ("Author", "Community Shaders Team"),
        ("Version", safe_label(label)), ("Website", FOMOD_WEBSITE),
        ("Description", "Installs the complete SE core with an optional managed prebuilt shader cache. Matching entries load from disk; other permutations compile locally."),
    ):
        ET.SubElement(info, name).text = value
    for path, root in ((directory / FOMOD_CONFIG_FILE_NAME, config), (directory / FOMOD_INFO_FILE_NAME, info)):
        ET.indent(root)
        ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)
    validate_fomod_installer(directory)


def validate_aio_archive(
    archive: Path,
    cmake: str,
    expected_files: dict[str, str],
    plugin_version: str,
    *,
    source_root: Path = REPO,
    include_horizon_fix: bool = True,
) -> None:
    """Verify every original core file and the complete cache survived packaging."""
    result = subprocess.run([cmake, "-E", "tar", "tf", str(archive)], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"failed to inspect packaged AIO (exit {result.returncode})")
    entries = [entry.strip().replace("\\", "/") for entry in result.stdout.splitlines() if entry.strip()]
    allowed_directories = {
        parent.as_posix() for name in expected_files for parent in Path(name).parents if parent != Path(".")
    }
    if any(entry.rstrip("/") not in expected_files and entry.rstrip("/") not in allowed_directories for entry in entries):
        raise SystemExit("packaged AIO contains an unexpected install path")
    if any(entries.count(name) != 1 for name in expected_files):
        raise SystemExit("packaged AIO has missing or duplicate payload files")
    with tempfile.TemporaryDirectory(prefix=".shader-cache-aio-archive-") as temporary:
        extracted = Path(temporary)
        result = subprocess.run([cmake, "-E", "tar", "xf", str(archive)], cwd=extracted, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(f"failed to extract packaged AIO (exit {result.returncode})")
        if ordinary_tree_files(extracted) != expected_files:
            raise SystemExit("packaged AIO differs from its validated core/cache payload")
        validate_fomod_installer(extracted / FOMOD_DIRECTORY)
        validate_managed_cache(
            extracted / AIO_CACHE_DIRECTORY, TARGET_RUNTIME, plugin_version,
            source_root=source_root, horizon_variants=include_horizon_fix,
        )


def prepare_aio_archive(
    core_root: Path,
    cache_root: Path,
    workspace: Path,
    runtime: str,
    label: str,
    plugin_version: str,
    compatibility_tag: str,
    cmake: str,
    *,
    source_root: Path = REPO,
    include_horizon_fix: bool = True,
) -> Path:
    """Stage the complete SE core with one optional managed cache payload."""
    core_files = validate_aio_inputs(
        core_root, cache_root, plugin_version, compatibility_tag,
        source_root=source_root, include_horizon_fix=include_horizon_fix,
    )
    package_root = workspace / "aio-fomod"
    package_root.mkdir()
    shutil.copytree(core_root, package_root / AIO_CORE_DIRECTORY)
    if ordinary_tree_files(package_root / AIO_CORE_DIRECTORY) != core_files:
        raise SystemExit("AIO core changed while staging the archive")
    shutil.copytree(cache_root / CACHE_DIRECTORY, package_root / AIO_CACHE_DIRECTORY)
    validate_managed_cache(
        package_root / AIO_CACHE_DIRECTORY, runtime, plugin_version,
        source_root=source_root, horizon_variants=include_horizon_fix,
    )
    write_fomod_installer(package_root, label, compatibility_tag)
    expected_files = ordinary_tree_files(package_root)
    archive = workspace / f"CSX_AIO-{safe_label(label)}.7z"
    command = [cmake, "-E", "tar", "cf", str(archive), "--format=7zip", AIO_CORE_DIRECTORY, AIO_CACHE_DIRECTORY, FOMOD_DIRECTORY]
    result = subprocess.run(command, cwd=package_root)
    if result.returncode or not archive.is_file() or not archive.stat().st_size:
        raise SystemExit(f"failed to package AIO (exit {result.returncode})")
    validate_aio_archive(
        archive, cmake, expected_files, plugin_version,
        source_root=source_root, include_horizon_fix=include_horizon_fix,
    )
    print(f"{runtime}: prepared {archive.name} ({archive.stat().st_size} bytes)")
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", choices=[TARGET_RUNTIME], default=TARGET_RUNTIME, help="Target the SE/AE shader ABI.")
    parser.add_argument("--profile", choices=sorted(CACHE_PROFILES), default=SHIPPED_CACHE_PROFILE.name, help="Shipped SE feature profile.")
    parser.add_argument("--include-horizon-fix", action=argparse.BooleanOptionalAction, default=True, help="Include scoped Horizon Fix water records (default: enabled).")
    parser.add_argument("--source-root", help="Repo checkout to take shaders/configs/version from (default: this repo).")
    parser.add_argument("--out", default="dist/shader-cache", help="Output root for validated caches and optional archives.")
    parser.add_argument("--plugin-version", help="Override the Info.ini version using 'CSX <major>.<minor>-SE'.")
    parser.add_argument("--fxc", help="Path to fxc.exe (default: PATH or Windows SDK).")
    parser.add_argument("--jobs", type=positive_int, default=os.cpu_count() or 4, help="Parallel compile jobs.")
    parser.add_argument("--package", action="store_true", help="Create a raw managed-cache .7z after validation.")
    parser.add_argument("--package-label", help="Archive label (default: the CSX compatibility tag).")
    parser.add_argument("--assemble-aio", metavar="CACHE_ROOT", help="Combine a validated managed cache with --aio-root without compiling shaders.")
    parser.add_argument("--aio-root", help="Extracted AIO-Release root used with --assemble-aio.")
    args = parser.parse_args()
    if bool(args.assemble_aio) != bool(args.aio_root):
        raise SystemExit("--assemble-aio and --aio-root must be supplied together")
    if args.assemble_aio and args.package:
        raise SystemExit("--assemble-aio always packages; do not also pass --package")
    source_root = Path(args.source_root).resolve() if args.source_root else REPO
    if not source_root.is_dir():
        raise SystemExit(f"source root does not exist: {source_root}")
    plugin_version = args.plugin_version or default_plugin_version(source_root)
    validate_ini_value(plugin_version, "SE plugin version")
    compatibility_tag = validate_csx_plugin_version(plugin_version)
    out_root = Path(args.out).resolve()
    if source_root == out_root or source_root.is_relative_to(out_root):
        raise SystemExit("--out must not be the source root or one of its parents")
    for shader_root in shader_source_roots(source_root):
        shader_root = shader_root.resolve()
        if out_root == shader_root or out_root.is_relative_to(shader_root):
            raise SystemExit(f"--out must not be inside a shader source directory: {shader_root}")
    out_root.mkdir(parents=True, exist_ok=True)
    if args.assemble_aio:
        cmake = shutil.which("cmake")
        if not cmake:
            raise SystemExit("cmake is required to assemble an AIO archive")
        core_root, cache_root = Path(args.aio_root).resolve(), Path(args.assemble_aio).resolve()
        for input_root in (core_root, cache_root):
            if out_root == input_root or out_root.is_relative_to(input_root):
                raise SystemExit("--out must not be inside an AIO assembly input")
        with tempfile.TemporaryDirectory(prefix=".aio-assembly-", dir=out_root) as temporary:
            archive = prepare_aio_archive(
                core_root, cache_root, Path(temporary), TARGET_RUNTIME,
                args.package_label or compatibility_tag, plugin_version, compatibility_tag, cmake,
                source_root=source_root, include_horizon_fix=args.include_horizon_fix,
            )
            archive_output_destination(out_root, archive)
            publish_cache_archive(archive, out_root, "SE AIO")
        return 0
    profile = CACHE_PROFILES[args.profile]
    validate_cache_profile(profile, [TARGET_RUNTIME])
    config_path = configs_for(source_root)[TARGET_RUNTIME]
    if not config_path.is_file():
        raise SystemExit(f"missing SE validation config: {config_path}")
    distribution = derive_distribution_profile(source_root)
    print("SE AIO profile: excluding hidden features " + (", ".join(sorted(distribution.excluded_short_names)) or "(none)"))
    compiler, yaml, write_manifest = require_compile_tools()
    cmake = shutil.which("cmake") if args.package else None
    if args.package and not cmake:
        raise SystemExit("cmake is required when --package is enabled")
    fxc = locate_fxc(args.fxc)
    print(f"using fxc.exe: {fxc}")
    with tempfile.TemporaryDirectory(prefix=".shader-cache-build-", dir=out_root) as temporary:
        workspace = Path(temporary)
        stage = workspace / "staged-shaders-SE"
        stage_merged_shaders(source_root, stage, distribution.excluded_packages)
        candidate, blob_counts, sections = build_runtime(
            source_root, stage, workspace, TARGET_RUNTIME, config_path, plugin_version,
            args.jobs, fxc, compiler, yaml, write_manifest, profile, distribution,
            include_horizon_fix=args.include_horizon_fix,
        )
        validate_managed_cache(
            candidate / CACHE_DIRECTORY, TARGET_RUNTIME, plugin_version,
            horizon_variants=args.include_horizon_fix, source_root=source_root,
        )
        archive = None
        if args.package:
            archive = prepare_cache_archive(
                candidate, workspace, TARGET_RUNTIME,
                args.package_label or default_package_label(profile, plugin_version), plugin_version, cmake,
                profile=profile, source_root=source_root, include_horizon_fix=args.include_horizon_fix,
            )
        runtime_output_destination(out_root, TARGET_RUNTIME)
        if archive:
            archive_output_destination(out_root, archive)
        published = publish_runtime_cache(candidate, out_root, TARGET_RUNTIME)
        print(f"SE: {blob_counts}, {sections} feature sections -> {published}")
        if archive:
            publish_cache_archive(archive, out_root, TARGET_RUNTIME)
    return 0


if __name__ == "__main__":
    sys.exit(main())
