#!/usr/bin/env python3
"""Stage the release AIO with one managed shader cache per runtime.

The caller supplies extracted AIO, SE, and VR archive roots. Each runtime
archive must contain one ``ShaderCache`` whose managed pack contains all
supported compatibility variants. The staged result installs the AIO
unconditionally and uses one manual FOMOD page to select a runtime.

No game version, DLL, marker, settings file, or mod-manager state is inspected.
"""

from __future__ import annotations

import argparse
import configparser
import importlib.util
import json
import re
import shutil
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


CORE_DIRECTORY = "Core"
CACHE_DIRECTORY = "ShaderCache"
FOMOD_DIRECTORY = "fomod"
MODULE_CONFIG_FILE = "ModuleConfig.xml"
INFO_FILE = "info.xml"
MANIFEST_FILE = "Manifest.json"
CACHE_INFO_FILE = "Info.ini"

RUNTIME_FLAG = "CSXRuntime"
RUNTIME_VR = "VR"
RUNTIME_SE_AE = "SE-AE"
RUNTIME_NONE = "None"

MODULE_NAME = "Community Shaders Expanded AIO"
MODULE_AUTHOR = "Community Shaders Expanded Contributors"
MODULE_WEBSITE = (
    "https://github.com/ParticleTroned/skyrim-community-shaders"
)


@dataclass(frozen=True)
class CacheVariant:
    runtime: str
    staging_directory: str


CACHE_VARIANTS = (
    CacheVariant(
        RUNTIME_VR,
        "ShaderCache-VR",
    ),
    CacheVariant(
        RUNTIME_SE_AE,
        "ShaderCache-SE-AE",
    ),
)


def load_shader_cache_contract():
    """Load the canonical pack reader used by the cache build itself."""
    tool_path = Path(__file__).with_name("build-shader-cache.py")
    spec = importlib.util.spec_from_file_location(
        "csx_build_shader_cache_contract", tool_path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load shader cache contract: {tool_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SHADER_CACHE_CONTRACT = load_shader_cache_contract()
PACK_MANIFEST_FILE = SHADER_CACHE_CONTRACT.PACK_MANIFEST_FILE_NAME
PACK_FILES = SHADER_CACHE_CONTRACT.PACK_FILE_NAMES
PACK_LANES = {
    "Optimized.A.csxpack": 1,
    "Optimized.B.csxpack": 1,
    "Developer.A.csxpack": 2,
    "Developer.B.csxpack": 2,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument(
        "--se-cache",
        required=True,
        type=Path,
        help="Extracted SE archive root containing one managed cache.",
    )
    parser.add_argument(
        "--vr-cache",
        required=True,
        type=Path,
        help="Extracted VR archive root containing one managed cache.",
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    return parser.parse_args()


def path_entry_exists(path: Path) -> bool:
    """Treat dangling links as occupied output paths."""
    return path.exists() or path.is_symlink()


def add_option(
    plugins: ET.Element,
    *,
    name: str,
    description: str,
    flag: str,
    value: str,
) -> None:
    plugin = ET.SubElement(plugins, "plugin", {"name": name})
    ET.SubElement(plugin, "description").text = description
    flags = ET.SubElement(plugin, "conditionFlags")
    ET.SubElement(flags, "flag", {"name": flag}).text = value
    type_descriptor = ET.SubElement(plugin, "typeDescriptor")
    ET.SubElement(type_descriptor, "type", {"name": "Optional"})


def add_selection_page(
    install_steps: ET.Element,
    *,
    name: str,
    group_name: str,
) -> tuple[ET.Element, ET.Element]:
    step = ET.SubElement(install_steps, "installStep", {"name": name})
    groups = ET.SubElement(step, "optionalFileGroups", {"order": "Explicit"})
    group = ET.SubElement(
        groups,
        "group",
        {"name": group_name, "type": "SelectExactlyOne"},
    )
    plugins = ET.SubElement(group, "plugins", {"order": "Explicit"})
    return step, plugins


def build_module_config() -> ET.ElementTree:
    root = ET.Element(
        "config",
        {
            "xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
            "xsi:noNamespaceSchemaLocation": (
                "http://qconsulting.ca/fo3/ModConfig5.0.xsd"
            ),
        },
    )
    ET.SubElement(root, "moduleName").text = MODULE_NAME

    required_files = ET.SubElement(root, "requiredInstallFiles")
    ET.SubElement(
        required_files,
        "folder",
        {"source": CORE_DIRECTORY, "destination": ".", "priority": "0"},
    )

    install_steps = ET.SubElement(root, "installSteps", {"order": "Explicit"})
    _, runtime_plugins = add_selection_page(
        install_steps,
        name="Choose the Skyrim runtime",
        group_name="Prebuilt shader cache",
    )
    add_option(
        runtime_plugins,
        name="Skyrim VR",
        description="Install the prebuilt shader cache compiled for Skyrim VR.",
        flag=RUNTIME_FLAG,
        value=RUNTIME_VR,
    )
    add_option(
        runtime_plugins,
        name="Skyrim SE/AE",
        description="Install the prebuilt shader cache compiled for Skyrim SE/AE.",
        flag=RUNTIME_FLAG,
        value=RUNTIME_SE_AE,
    )
    add_option(
        runtime_plugins,
        name="No prebuilt shader cache",
        description=(
            "Install the AIO without a cache and compile shaders locally when "
            "the game starts."
        ),
        flag=RUNTIME_FLAG,
        value=RUNTIME_NONE,
    )

    conditional_installs = ET.SubElement(root, "conditionalFileInstalls")
    patterns = ET.SubElement(conditional_installs, "patterns")
    for variant in CACHE_VARIANTS:
        pattern = ET.SubElement(patterns, "pattern")
        dependencies = ET.SubElement(pattern, "dependencies", {"operator": "And"})
        ET.SubElement(
            dependencies,
            "flagDependency",
            {"flag": RUNTIME_FLAG, "value": variant.runtime},
        )
        files = ET.SubElement(pattern, "files")
        ET.SubElement(
            files,
            "folder",
            {
                "source": f"{variant.staging_directory}\\{CACHE_DIRECTORY}",
                "destination": CACHE_DIRECTORY,
                "priority": "0",
            },
        )

    ET.indent(root, space="  ")
    return ET.ElementTree(root)


def build_info(version: str) -> ET.ElementTree:
    root = ET.Element("fomod")
    fields = (
        ("Name", MODULE_NAME),
        ("Author", MODULE_AUTHOR),
        ("Version", version),
        (
            "Description",
            "Community Shaders Expanded AIO with optional prebuilt shader caches.",
        ),
        ("Website", MODULE_WEBSITE),
    )
    for tag, value in fields:
        ET.SubElement(root, tag).text = value
    ET.indent(root, space="  ")
    return ET.ElementTree(root)


def validate_cache_source(cache_directory: Path, expected_runtime: str) -> None:
    manifest_path = cache_directory / MANIFEST_FILE
    pack_manifest_path = cache_directory / PACK_MANIFEST_FILE
    info_path = cache_directory / CACHE_INFO_FILE
    if not cache_directory.is_dir():
        raise SystemExit(f"missing shader cache directory: {cache_directory}")
    if not info_path.is_file():
        raise SystemExit(f"missing shader cache metadata: {info_path}")
    if not manifest_path.is_file():
        raise SystemExit(f"missing shader cache manifest: {manifest_path}")
    if not pack_manifest_path.is_file():
        raise SystemExit(f"missing managed pack manifest: {pack_manifest_path}")
    info = configparser.ConfigParser(interpolation=None)
    try:
        with info_path.open("r", encoding="utf-8-sig") as stream:
            info.read_file(stream)
    except (configparser.Error, OSError, UnicodeError) as exc:
        raise SystemExit(f"invalid shader cache metadata {info_path}: {exc}") from exc
    plugin_version = info.get("Cache", "PluginVersion", fallback=None)
    shader_cache_abi = info.get("Cache", "ShaderCacheABI", fallback=None)
    version_match = (
        SHADER_CACHE_CONTRACT.CSX_PLUGIN_VERSION_PATTERN.fullmatch(plugin_version)
        if plugin_version
        else None
    )
    contract_runtime = "SE" if expected_runtime == RUNTIME_SE_AE else "VR"
    if version_match is None or version_match.group("runtime") != contract_runtime:
        raise SystemExit(
            f"cache {cache_directory} plugin version does not identify "
            f"runtime {contract_runtime}: {plugin_version!r}"
        )
    if not shader_cache_abi:
        raise SystemExit(f"cache {cache_directory} has no ShaderCacheABI")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit(
            f"invalid shader cache manifest {manifest_path}: {exc}"
        ) from exc
    if (
        not isinstance(manifest, dict)
        or manifest.get("schemaVersion") != 1
        or not isinstance(manifest.get("entries"), dict)
    ):
        raise SystemExit(f"unsupported shader cache manifest: {manifest_path}")
    try:
        pack_manifest = json.loads(pack_manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid managed pack manifest {pack_manifest_path}: {exc}") from exc
    pack_set_id = pack_manifest.get("packSetId") if isinstance(pack_manifest, dict) else None
    variants = (
        pack_manifest.get("compatibilityVariants")
        if isinstance(pack_manifest, dict)
        else None
    )
    if (
        not isinstance(pack_manifest, dict)
        or pack_manifest.get("schema") != "csx.shader-cache.pack-manifest"
        or pack_manifest.get("schemaVersion")
        != SHADER_CACHE_CONTRACT.PACK_MANIFEST_SCHEMA_VERSION
        or pack_manifest.get("formatVersion")
        != SHADER_CACHE_CONTRACT.PACK_FORMAT_VERSION
        or pack_manifest.get("hashAlgorithm") != "sha256"
        or pack_manifest.get("runtime") != contract_runtime
        or pack_manifest.get("shaderCacheABI") != shader_cache_abi
        or not isinstance(pack_set_id, str)
        or re.fullmatch(r"[0-9a-f]{32}", pack_set_id) is None
        or not isinstance(variants, list)
        or not variants
        or any(not isinstance(value, str) or not value for value in variants)
        or len(set(variants)) != len(variants)
        or "default" not in variants
    ):
        raise SystemExit(
            f"managed pack manifest does not match its runtime metadata: "
            f"{pack_manifest_path}"
        )

    missing_packs = [
        name for name in PACK_FILES if not (cache_directory / name).is_file()
    ]
    if missing_packs:
        raise SystemExit(
            f"managed shader cache {cache_directory} is missing pack files: "
            + ", ".join(missing_packs)
        )

    pack_stats = {
        name: SHADER_CACHE_CONTRACT.validate_shader_pack(
            cache_directory / name,
            PACK_LANES[name],
            pack_set_id,
        )
        for name in PACK_FILES
    }
    expected_files = {
        name: {
            "lane": PACK_LANES[name],
            "generation": stats["generation"],
            "recordCount": stats["recordCount"],
        }
        for name, stats in pack_stats.items()
    }
    if (
        pack_manifest.get("files") != expected_files
        or pack_manifest.get("optimizedRecordCount")
        != sum(
            pack_stats[name]["recordCount"]
            for name in PACK_FILES
            if name.startswith("Optimized")
        )
        or pack_manifest.get("developerRecordCount")
        != sum(
            pack_stats[name]["recordCount"]
            for name in PACK_FILES
            if name.startswith("Developer")
        )
    ):
        raise SystemExit(
            f"managed pack manifest disagrees with its pack files: "
            f"{pack_manifest_path}"
        )
    for first, second in (
        ("Optimized.A.csxpack", "Optimized.B.csxpack"),
        ("Developer.A.csxpack", "Developer.B.csxpack"),
    ):
        if abs(pack_stats[first]["generation"] - pack_stats[second]["generation"]) != 1:
            raise SystemExit(
                f"managed cache lane has invalid A/B generations: {first}, {second}"
            )


def flag_pairs(element: ET.Element) -> tuple[tuple[str, str], ...]:
    return tuple(
        (dependency.get("flag", ""), dependency.get("value", ""))
        for dependency in element.findall("./flagDependency")
    )


def validate_module_config(config_path: Path) -> None:
    try:
        root = ET.parse(config_path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise SystemExit(f"invalid FOMOD config {config_path}: {exc}") from exc

    if root.tag != "config" or [child.tag for child in root] != [
        "moduleName",
        "requiredInstallFiles",
        "installSteps",
        "conditionalFileInstalls",
    ]:
        raise SystemExit("FOMOD config has an unexpected root structure")

    forbidden_tags = {
        "dependencyType",
        "fileDependency",
        "gameDependency",
        "moduleDependencies",
    }
    present_forbidden = sorted(
        {element.tag for element in root.iter() if element.tag in forbidden_tags}
    )
    if present_forbidden:
        raise SystemExit(
            "FOMOD config contains automatic detection: "
            + ", ".join(present_forbidden)
        )

    serialized = ET.tostring(root, encoding="unicode").casefold()
    forbidden_text = (
        "use_any_file",
        "mod organizer",
        "mo2",
        ".dll",
        ".marker",
        "nexus",
        "discord",
        "open shaders",
    )
    present_text = [text for text in forbidden_text if text in serialized]
    if present_text:
        raise SystemExit(
            "FOMOD config contains forbidden detection or branding text: "
            + ", ".join(present_text)
        )

    required_folder = root.find("./requiredInstallFiles/folder")
    if required_folder is None or required_folder.attrib != {
        "source": CORE_DIRECTORY,
        "destination": ".",
        "priority": "0",
    }:
        raise SystemExit("FOMOD must install the complete AIO Core directory")

    steps = root.findall("./installSteps/installStep")
    if len(steps) != 1:
        raise SystemExit("FOMOD must contain exactly one manual runtime page")

    expected_pages = (
        (
            "Choose the Skyrim runtime",
            (
                ("Skyrim VR", RUNTIME_FLAG, RUNTIME_VR),
                ("Skyrim SE/AE", RUNTIME_FLAG, RUNTIME_SE_AE),
                ("No prebuilt shader cache", RUNTIME_FLAG, RUNTIME_NONE),
            ),
        ),
    )
    for step, (expected_name, expected_options) in zip(steps, expected_pages):
        if step.get("name") != expected_name:
            raise SystemExit(f"unexpected FOMOD page: {step.get('name')!r}")
        group = step.find("./optionalFileGroups/group")
        plugins = step.findall("./optionalFileGroups/group/plugins/plugin")
        if group is None or group.get("type") != "SelectExactlyOne":
            raise SystemExit(f"FOMOD page {expected_name!r} must require one choice")
        actual_options = []
        for plugin in plugins:
            flags = plugin.findall("./conditionFlags/flag")
            option_types = plugin.findall("./typeDescriptor/type")
            if len(flags) != 1 or len(option_types) != 1:
                raise SystemExit(
                    f"FOMOD page {expected_name!r} has malformed manual choices"
                )
            if option_types[0].get("name") != "Optional":
                raise SystemExit(
                    "manual FOMOD choices must not be auto-recommended"
                )
            actual_options.append(
                (
                    plugin.get("name", ""),
                    flags[0].get("name", ""),
                    flags[0].text or "",
                )
            )
        actual_options = tuple(actual_options)
        if actual_options != expected_options:
            raise SystemExit(f"FOMOD page {expected_name!r} has wrong choices")
        if any(plugin.find("./files") is not None for plugin in plugins):
            raise SystemExit("manual FOMOD choices must set flags, not install files")

    patterns = root.findall("./conditionalFileInstalls/patterns/pattern")
    if len(patterns) != len(CACHE_VARIANTS):
        raise SystemExit("FOMOD must contain exactly two cache install patterns")
    actual_mappings: dict[tuple[tuple[str, str], ...], tuple[str, str, str]] = {}
    for pattern in patterns:
        dependencies = pattern.find("./dependencies")
        folder = pattern.find("./files/folder")
        if dependencies is None or dependencies.get("operator") != "And":
            raise SystemExit("cache install conditions must combine manual flags")
        if folder is None:
            raise SystemExit("cache install condition is missing its folder")
        dependency_flags = flag_pairs(dependencies)
        if dependency_flags in actual_mappings:
            raise SystemExit("FOMOD repeats a cache install condition")
        actual_mappings[dependency_flags] = (
            folder.get("source", "").replace("\\", "/"),
            folder.get("destination", ""),
            folder.get("priority", ""),
        )

    expected_mappings = {
        ((RUNTIME_FLAG, variant.runtime),): (
            f"{variant.staging_directory}/{CACHE_DIRECTORY}",
            CACHE_DIRECTORY,
            "0",
        )
        for variant in CACHE_VARIANTS
    }
    if actual_mappings != expected_mappings:
        raise SystemExit("FOMOD does not map both managed runtime caches")


def validate_staged_package(output: Path, version: str) -> None:
    if not (output / CORE_DIRECTORY).is_dir():
        raise SystemExit("staged FOMOD is missing its AIO Core directory")
    for variant in CACHE_VARIANTS:
        cache_directory = output / variant.staging_directory / CACHE_DIRECTORY
        validate_cache_source(cache_directory, variant.runtime)

    fomod_directory = output / FOMOD_DIRECTORY
    validate_module_config(fomod_directory / MODULE_CONFIG_FILE)
    try:
        info_root = ET.parse(fomod_directory / INFO_FILE).getroot()
    except (OSError, ET.ParseError) as exc:
        raise SystemExit(f"invalid FOMOD info metadata: {exc}") from exc
    expected_info = ["Name", "Author", "Version", "Description", "Website"]
    if (
        info_root.tag != "fomod"
        or [child.tag for child in info_root] != expected_info
        or info_root.findtext("./Version") != version
        or any(not (child.text or "").strip() for child in info_root)
    ):
        raise SystemExit("FOMOD info metadata is incomplete or inconsistent")


def stage_package(
    core: Path,
    se_cache: Path,
    vr_cache: Path,
    output: Path,
    version: str,
) -> None:
    if not core.is_dir():
        raise SystemExit(f"missing extracted AIO tree: {core}")
    if not version.strip() or "\n" in version or "\r" in version:
        raise SystemExit("--version must be a nonempty single-line value")
    if path_entry_exists(output):
        raise SystemExit(f"refusing to replace existing staging path: {output}")
    for input_root in (core, se_cache, vr_cache):
        if output == input_root or output.is_relative_to(input_root):
            raise SystemExit(
                f"FOMOD staging path must not be inside an input tree: {input_root}"
            )

    runtime_roots = {RUNTIME_SE_AE: se_cache, RUNTIME_VR: vr_cache}
    sources: dict[CacheVariant, Path] = {}
    for variant in CACHE_VARIANTS:
        source = runtime_roots[variant.runtime] / CACHE_DIRECTORY
        validate_cache_source(source, variant.runtime)
        sources[variant] = source

    try:
        output.mkdir(parents=True)
        shutil.copytree(core, output / CORE_DIRECTORY)
        for variant, source in sources.items():
            shutil.copytree(
                source,
                output / variant.staging_directory / CACHE_DIRECTORY,
            )

        fomod_directory = output / FOMOD_DIRECTORY
        fomod_directory.mkdir()
        build_module_config().write(
            fomod_directory / MODULE_CONFIG_FILE,
            encoding="utf-8",
            xml_declaration=True,
        )
        build_info(version).write(
            fomod_directory / INFO_FILE,
            encoding="utf-8",
            xml_declaration=True,
        )
        validate_staged_package(output, version)
    except (OSError, SystemExit):
        shutil.rmtree(output, ignore_errors=True)
        raise


def main() -> int:
    args = parse_args()
    stage_package(
        args.core.resolve(),
        args.se_cache.resolve(),
        args.vr_cache.resolve(),
        args.output.resolve(),
        args.version,
    )
    print(f"staged managed-cache FOMOD at {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
