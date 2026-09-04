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
PACK_MANIFEST_FILE = "PackManifest.json"
PACK_FILES = (
    "Optimized.A.csxpack",
    "Optimized.B.csxpack",
    "Developer.A.csxpack",
    "Developer.B.csxpack",
)
CACHE_INFO_FILE = "Info.ini"
CORE_BUILD_MANIFEST = Path("SKSE/Plugins/CSX.BuildManifest.json")
SHADER_CACHE_ABI_PATTERN = re.compile(r"[0-9a-f]{64}")

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


def core_shader_cache_abi(core: Path) -> str:
    manifest_path = core / CORE_BUILD_MANIFEST
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        shader_cache_abi = manifest["identity"]["shaderCache"]["abiId"]
    except (KeyError, OSError, TypeError, UnicodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid core build manifest: {manifest_path}") from exc
    if not isinstance(shader_cache_abi, str) or not SHADER_CACHE_ABI_PATTERN.fullmatch(
        shader_cache_abi
    ):
        raise SystemExit(f"invalid core shader-cache ABI: {manifest_path}")
    return shader_cache_abi


def validate_cache_source(
    cache_directory: Path, expected_shader_cache_abi: str
) -> None:
    manifest_path = cache_directory / MANIFEST_FILE
    pack_manifest_path = cache_directory / PACK_MANIFEST_FILE
    if not cache_directory.is_dir():
        raise SystemExit(f"missing shader cache directory: {cache_directory}")
    info_path = cache_directory / CACHE_INFO_FILE
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
    shader_cache_abi = info.get("Cache", "ShaderCacheABI", fallback=None)
    if shader_cache_abi != expected_shader_cache_abi:
        raise SystemExit(
            f"shader cache ABI does not match the core AIO: {cache_directory} "
            f"(core {expected_shader_cache_abi}, cache {shader_cache_abi!r})"
        )
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
    if (
        not isinstance(pack_manifest, dict)
        or pack_manifest.get("schema") != "csx.shader-cache.pack-manifest"
        or pack_manifest.get("schemaVersion") != 1
        or pack_manifest.get("formatVersion") != 1
        or pack_manifest.get("shaderCacheABI") != expected_shader_cache_abi
        or not isinstance(pack_manifest.get("compatibilityVariants"), list)
        or "default" not in pack_manifest["compatibilityVariants"]
    ):
        raise SystemExit(f"unsupported managed pack manifest: {pack_manifest_path}")
    missing_packs = [name for name in PACK_FILES if not (cache_directory / name).is_file()]
    if missing_packs:
        raise SystemExit(
            f"managed shader cache {cache_directory} is missing pack files: "
            + ", ".join(missing_packs)
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
    core = output / CORE_DIRECTORY
    if not core.is_dir():
        raise SystemExit("staged FOMOD is missing its AIO Core directory")
    shader_cache_abi = core_shader_cache_abi(core)
    for variant in CACHE_VARIANTS:
        cache_directory = output / variant.staging_directory / CACHE_DIRECTORY
        validate_cache_source(cache_directory, shader_cache_abi)

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
    shader_cache_abi = core_shader_cache_abi(core)
    sources: dict[CacheVariant, Path] = {}
    for variant in CACHE_VARIANTS:
        source = runtime_roots[variant.runtime] / CACHE_DIRECTORY
        validate_cache_source(source, shader_cache_abi)
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
