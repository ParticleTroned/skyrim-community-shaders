#!/usr/bin/env python3
"""Build a distributable shader disk cache for this repo.

Produces the layout the runtime consumes at Data/ShaderCache/:
  ShaderCache/<ShaderName>/<descriptor:HEX>.{pso,vso,cso}
  ShaderCache/Info.ini

The generated cache targets this repo's shipped distribution profile:
  - the VR feature is omitted on SE
  - shipped features are treated as active
  - WetnessEffects is legacy, default-off, and not shipped

Usage:
  python tools/build-shader-cache.py --runtime SE
  python tools/build-shader-cache.py --runtime both --out dist/shader-cache
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent


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
NON_SHIPPED_DEFINES = {
    feature["define"]
    for feature in NON_SHIPPED_FEATURES.values()
}
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


def configs_for(source_root: Path) -> dict[str, Path]:
    return {
        "SE": source_root / ".github/configs/shader-validation.yaml",
        "VR": source_root / ".github/configs/shader-validation-vr.yaml",
    }


def stage_merged_shaders(source_root: Path, stage: Path) -> None:
    if stage.exists():
        shutil.rmtree(stage)

    package_shaders = source_root / "package/Shaders"
    features_root = source_root / "features"
    if not package_shaders.is_dir():
        raise SystemExit(f"missing package shader directory: {package_shaders}")
    if not features_root.is_dir():
        raise SystemExit(f"missing feature directory: {features_root}")

    shutil.copytree(package_shaders, stage)
    for feature_dir in sorted(features_root.iterdir()):
        if feature_dir.name in NON_SHIPPED_PACKAGES:
            continue
        shaders_dir = feature_dir / "Shaders"
        if shaders_dir.is_dir():
            shutil.copytree(shaders_dir, stage, dirs_exist_ok=True)


def append_missing_defines(defines: object, names: tuple[str, ...]) -> None:
    if not isinstance(defines, list):
        return

    existing = {
        define.split("=", 1)[0]
        for define in defines
        if isinstance(define, str)
    }
    for name in names:
        if name not in existing:
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
                    and value.split("=", 1)[0] in NON_SHIPPED_DEFINES
                )
            ]
        return node

    config = scrub(config)
    if not isinstance(config, dict):
        return config

    append_missing_defines(config.get("common_defines"), GLOBAL_SHIPPED_DEFINES)

    file_common_defines = config.get("file_common_defines")
    if isinstance(file_common_defines, dict):
        for file_name, defines_to_add in FILE_SHIPPED_DEFINES.items():
            stage_defines = file_common_defines.get(file_name)
            if isinstance(stage_defines, dict):
                for defines in stage_defines.values():
                    append_missing_defines(defines, defines_to_add)

    shaders = config.get("shaders")
    if isinstance(shaders, list):
        for shader in shaders:
            if not isinstance(shader, dict):
                continue

            defines_to_add = FILE_SHIPPED_DEFINES.get(shader.get("file"))
            if not defines_to_add:
                continue

            stage_configs = shader.get("configs")
            if not isinstance(stage_configs, dict):
                continue

            for stage_config in stage_configs.values():
                if isinstance(stage_config, dict):
                    append_missing_defines(
                        stage_config.get("common_defines"),
                        defines_to_add,
                    )

    return config


def filter_profile_defines(config_path: Path, out_path: Path) -> Path:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(
            "PyYAML is required to filter shader validation configs; install pyyaml or use --skip-compile"
        ) from exc

    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    config = apply_shipped_profile_defines(config)
    out_path.write_text(
        yaml.safe_dump(config, sort_keys=False),
        encoding="utf-8",
    )
    return out_path


def remap_imagespace_dirs(cache_dir: Path, runtime: str) -> None:
    index = 1 if runtime == "VR" else 0
    by_descriptor = {
        descriptor_pair[index]: name
        for descriptor_pair, name in IMAGESPACE_DIRS.items()
    }
    keep_suffixes = {".pso", ".vso", ".cso"}

    for directory in sorted(cache_dir.iterdir()):
        if not directory.is_dir() or not directory.name.startswith("IS"):
            continue

        for path in sorted(directory.iterdir()):
            if path.suffix.lower() not in keep_suffixes:
                continue

            try:
                descriptor = int(path.stem, 16)
            except ValueError:
                continue

            target_dir_name = by_descriptor.get(descriptor)
            if not target_dir_name or target_dir_name == directory.name:
                continue

            target_dir = cache_dir / target_dir_name
            target_dir.mkdir(exist_ok=True)
            path.replace(target_dir / path.name)

        if not any(directory.iterdir()):
            directory.rmdir()


def prune_non_cache_files(cache_dir: Path) -> None:
    keep_suffixes = {".pso", ".vso", ".cso"}

    for path in cache_dir.rglob("*"):
        if path.is_file() and path.suffix.lower() not in keep_suffixes and path.name != "Info.ini":
            path.unlink()

    for directory in sorted((path for path in cache_dir.rglob("*") if path.is_dir()), reverse=True):
        if not any(directory.iterdir()):
            directory.rmdir()


def write_info_ini(cache_dir: Path, stage: Path, plugin_version: str, runtime: str) -> int:
    lines = ["[Cache]", f"PluginVersion = {plugin_version}", "", ""]
    count = 0

    for ini_path in sorted((stage / "Features").glob("*.ini")):
        stem = ini_path.stem
        if stem in RUNTIME_EXCLUDED_FEATURES[runtime] or stem in NON_SHIPPED_FEATURES:
            continue

        config = configparser.ConfigParser()
        config.read(ini_path, encoding="utf-8-sig")
        version = config.get("Info", "Version", fallback=None)
        if not version:
            print(f"WARN: {ini_path.name} has no Info/Version; skipped", file=sys.stderr)
            continue

        lines += [f"[{stem}]", "Enabled = true", f"Version = {version}", "", ""]
        count += 1

    (cache_dir / "Info.ini").write_bytes(
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
        version = cache_variables.get("CS_PL_FORK_VERSION")
        if isinstance(version, str) and version:
            return version

    raise SystemExit(
        f"cannot derive {runtime} plugin version from CMakePresets.json; pass --plugin-version"
    )


def build_runtime(
    stage: Path,
    out_root: Path,
    runtime: str,
    config_path: Path,
    plugin_version: str,
    jobs: int,
    fxc: str | None,
    skip_compile: bool,
) -> None:
    runtime_root = out_root / runtime
    cache_dir = runtime_root / "ShaderCache"

    if runtime_root.exists():
        shutil.rmtree(runtime_root)
    cache_dir.mkdir(parents=True, exist_ok=True)

    if not skip_compile:
        filtered_config = filter_profile_defines(
            config_path,
            out_root / f"config-{runtime}.yaml",
        )
        command = [
            "hlslkit-compile",
            "--shader-dir",
            str(stage),
            "--output-dir",
            str(cache_dir),
            "--config",
            str(filtered_config),
            "--strip-debug-defines",
            "--optimization-level",
            "3",
            "--suppress-warnings",
            "X1519",
            "--max-warnings",
            "999999",
            "--jobs",
            str(jobs),
        ]
        if fxc:
            command.extend(["--fxc", fxc])

        print("run:", " ".join(command))
        result = subprocess.run(command)
        if result.returncode != 0:
            raise SystemExit(
                f"hlslkit-compile failed for {runtime} (exit {result.returncode})"
            )

        prune_non_cache_files(cache_dir)
        remap_imagespace_dirs(cache_dir, runtime)

    section_count = write_info_ini(cache_dir, stage, plugin_version, runtime)
    blob_count = sum(
        1
        for path in cache_dir.rglob("*")
        if path.suffix.lower() in (".pso", ".vso", ".cso")
    )
    print(
        f"{runtime}: {blob_count} cache blobs, Info.ini with {section_count} feature sections -> {cache_dir}"
    )


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
        help="Output root for staged shaders and built caches.",
    )
    parser.add_argument(
        "--plugin-version",
        help="Override the plugin version label written to Info.ini.",
    )
    parser.add_argument(
        "--fxc",
        help="Optional path to fxc.exe. If omitted, hlslkit-compile must locate it itself.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=os.cpu_count() or 4,
        help="Parallel compile jobs to pass to hlslkit-compile.",
    )
    parser.add_argument(
        "--skip-compile",
        action="store_true",
        help="Stage shaders and write Info.ini without compiling blobs.",
    )
    args = parser.parse_args()

    source_root = Path(args.source_root).resolve() if args.source_root else REPO
    configs = configs_for(source_root)
    for runtime, config_path in configs.items():
        if not config_path.is_file():
            raise SystemExit(f"missing validation config for {runtime}: {config_path}")

    out_root = Path(args.out).resolve()
    stage = out_root / "staged-shaders"
    stage_merged_shaders(source_root, stage)
    print(f"staged merged shader tree: {stage}")

    jobs = max(1, args.jobs)
    runtimes = ["SE", "VR"] if args.runtime == "both" else [args.runtime]
    for runtime in runtimes:
        plugin_version = args.plugin_version or default_plugin_version(source_root, runtime)
        build_runtime(
            stage=stage,
            out_root=out_root,
            runtime=runtime,
            config_path=configs[runtime],
            plugin_version=plugin_version,
            jobs=jobs,
            fxc=args.fxc,
            skip_compile=args.skip_compile,
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
