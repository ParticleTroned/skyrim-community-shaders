"""Release helper shaders using CSX's existing standalone cache identities."""

from __future__ import annotations

import json
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor
from itertools import groupby
from pathlib import Path, PurePosixPath

from shader_bytecode import validate_dxbc


RELEASE_FLAGS = 34818  # Strictness, optimization level 3, skip validation.
STAGES = {"cs_5_0": ("COMPUTESHADER", ".cso"), "ps_5_0": ("PSHADER", ".pso"), "vs_5_0": ("VSHADER", ".vso")}


def validate_source(source: object) -> str:
    """Accept only canonical paths below the merged shader directory."""
    if not isinstance(source, str) or not source or any(c in source for c in "\\:\0"):
        raise ValueError(f"invalid standalone source: {source!r}")
    path = PurePosixPath(source)
    if path.is_absolute() or path.as_posix() != source or ".." in path.parts or path.suffix != ".hlsl":
        raise ValueError(f"invalid standalone source: {source!r}")
    return source


def validate_pack_inputs(inputs: object) -> dict:
    """Validate the source/stage inventory used by offline pack admission."""
    if not isinstance(inputs, dict) or len(inputs) > 4096:
        raise ValueError("invalid standalone pack inventory")
    for relative, item in inputs.items():
        if not isinstance(relative, str) or not re.fullmatch(r"Standalone/[0-9a-f]{32}\.cso", relative):
            raise ValueError(f"invalid standalone cache path: {relative!r}")
        if not isinstance(item, dict) or set(item) != {"source", "profile"} or not isinstance(item["profile"], str) or item["profile"] not in STAGES:
            raise ValueError(f"invalid standalone pack input: {relative}")
        validate_source(item["source"])
    return inputs


def serialize_defines(defines: list[tuple[str, str | None]]) -> str:
    """Match ShaderDefines::Serialize, including its null/empty equivalence."""
    ordered = sorted(((name, value or "") for name, value in defines), key=lambda pair: pair[0])
    canonical = [pair for pair, _ in groupby(ordered)]
    return "".join(f"{len(text.encode('utf-8'))}:{text}" for pair in canonical for text in pair)


def compile_digest(request: dict, global_state: str) -> int:
    """Match BuildStandaloneCompileKey without changing existing runtime keys."""
    from hlslkit.shader_digest import combine_hashes, hash_string

    inputs = json.dumps([
        "standalone-v1", "Data/Shaders/" + request["source"],
        request["profile"], request["entry"], RELEASE_FLAGS,
    ], ensure_ascii=False, separators=(",", ":"))
    macros = combine_hashes(hash_string(global_state), hash_string(serialize_defines(request["defines"])))
    return combine_hashes(macros, hash_string(inputs))


def release_requests(runtime: str, features: dict[str, bool], *, reverse_z_enabled: bool = False) -> list[dict]:
    """Cover shipped feature startup paths and bounded common quality variants.

    Other settings, custom defines and developer shaders retain runtime
    compile-on-miss behavior. No permutations are added to the live renderer.
    Depth macros follow active depth state, separately from feature loading.
    """
    if runtime not in {"SE", "VR"}:
        raise ValueError(f"invalid runtime: {runtime}")
    requests = []

    def add(source, defines=(), *, feature=None, profile="cs_5_0", entry="main"):
        if feature is not None and not features.get(feature, False):
            return
        macros = list(defines)
        if runtime == "VR":
            macros.append(("VR", ""))
        macros.extend(((STAGES[profile][0], ""), ("WINPC", ""), ("DX11", "")))
        if reverse_z_enabled and features.get("ReverseZ", False):
            macros.append(("REVERSE_Z", "1"))
        requests.append({"source": validate_source(source), "profile": profile, "entry": entry, "defines": macros})

    if runtime == "VR":
        for source, profile in (
            ("ReverseZ/OccluderDepthDownscalePS.hlsl", "ps_5_0"),
            ("ReverseZ/OccluderBoxVS.hlsl", "vs_5_0"),
            ("ReverseZ/HiddenAreaMeshVS.hlsl", "vs_5_0"),
        ):
            add(source, feature="ReverseZ", profile=profile)
    for source in ("CopyShadowDataCS.hlsl", "Common/DepthPyramidBaseCS.hlsl", "Common/DepthPyramidSPD.hlsl"):
        add(source)
    add("Common/DepthPyramidBaseCS.hlsl", [("BASE_REDUCTION", "4")])
    add("Common/Glints/noisegen.cs.hlsl")
    for source, profile, entry in (
        ("Menu/BackgroundBlurHorizontal.hlsl", "vs_5_0", "VS_Main"),
        ("Menu/BackgroundBlurHorizontal.hlsl", "ps_5_0", "PS_Main"),
        ("Menu/BackgroundBlurVertical.hlsl", "ps_5_0", "PS_Main"),
    ):
        add(source, profile=profile, entry=entry)
    for source in ("Upscaling/RCAS/RCAS.hlsl", "Upscaling/LumaSharpen/LumaSharpen.hlsl"):
        for macros in ((), (("MOTION_ADAPTIVE", "1"),)):
            add(source, macros, feature="Upscaling")
    for source, feature in (
        ("GrassCollision/CollisionUpdateCS.hlsl", "GrassCollision"),
        ("GrassOptimizations/GrassCullingCS.hlsl", "GrassOptimizations"),
        ("GrassOptimizations/DetectComplexCS.hlsl", "GrassOptimizations"),
        ("LightLimitFix/ClusterBuildingCS.hlsl", "LightLimitFix"),
        ("LightLimitFix/ClusterCullingCS.hlsl", "LightLimitFix"),
        ("Skylighting/UpdateProbesCS.hlsl", "Skylighting"),
        ("TerrainShadows/ShadowUpdate.cs.hlsl", "TerrainShadows"),
        ("TerrainBlending/DepthBlend.hlsl", "TerrainBlending"),
    ):
        add(source, feature=feature)
    add("Utility.hlsl", [("RENDER_DEPTH", ""), ("OFFSET_DEPTH", "")], feature="TerrainBlending", profile="vs_5_0")
    for name, variants in (
        ("DetectCaptureLightingCS.hlsl", [(), (("REFLECTIONS", ""),)]),
        ("UpdateCubemapCS.hlsl", [(), (("REFLECTIONS", ""),), (("FAKEREFLECTIONS", ""),)]),
        ("InferCubemapCS.hlsl", [(), (("REFLECTIONS", ""),), (("FAKEREFLECTIONS", ""),)]),
        ("SpecularIrradianceCS.hlsl", [()]), ("BC6HEncodeCS.hlsl", [()]),
    ):
        for macros in variants:
            add("DynamicCubemaps/" + name, macros, feature="DynamicCubemaps")
    for macros in ((), (("HORIZONTAL", ""),), (("BURLEY", ""),)):
        add("SubsurfaceScattering/SeparableSSSCS.hlsl", macros, feature="SubsurfaceScattering")
    for mip in ("DOWNSAMPLE_SHADOW_MIP0", "DOWNSAMPLE_SHADOW_MIP1"):
        for general in (False, True):
            macros = [(mip, None)] + ([("DOWNSAMPLE_SHADOW_GENERAL", None)] if general else [])
            add("VolumetricShadows/DownsampleShadowCS.hlsl", macros, feature="VolumetricShadows")
    for direction in ("BLUR_HORIZONTAL", "BLUR_VERTICAL"):
        add("VolumetricShadows/BlurShadowCS.hlsl", [(direction, None)], feature="VolumetricShadows")
    cubemaps = [("DYNAMIC_CUBEMAPS", None)] if features.get("DynamicCubemaps") else []
    add("IBL/DiffuseIBLCS.hlsl", cubemaps, feature="ImageBasedLighting")
    for interior in (False, True):
        for ao_only in (False, True) if features.get("ScreenSpaceGI") else (False,):
            macros = list(cubemaps)
            if interior:
                macros.append(("INTERIOR", None))
            elif features.get("Skylighting"):
                macros.append(("SKYLIGHTING", None))
            for feature, name in (("ScreenSpaceGI", "SSGI"), ("ImageBasedLighting", "IBL"), ("TerrainBlending", "TERRAIN_BLENDING")):
                if features.get(feature):
                    macros.append((name, None))
            if ao_only:
                macros.append(("SSGI_AO_ONLY", None))
            if runtime == "VR":
                macros.append(("FRAMEBUFFER", None))
            add("DeferredCompositeCS.hlsl", macros)
    if runtime == "VR":
        add("VR/StereoBlendCS.hlsl", [("FRAMEBUFFER", "")], feature="VR")
    for terrain in (False, True) if features.get("TerrainBlending") else (False,):
        depth = [("TERRAIN_BLENDING", "")] if terrain else []
        if runtime == "VR":
            for name in ("ShadowReprojectCS.hlsl", "StereoSyncCS.hlsl"):
                add("ScreenSpaceShadows/" + name, [*depth, ("FRAMEBUFFER", "")], feature="ScreenSpaceShadows")
        # VR uses integer per-eye counts; flat rendering quantizes counts to eight.
        for count in range(1, 129) if runtime == "VR" else range(8, 257, 8):
            for right in (False, True) if runtime == "VR" else (False,):
                macros = [*depth, ("SAMPLE_COUNT", str(count))] + ([("RIGHT", "")] if right else [])
                add("ScreenSpaceShadows/RaymarchCS.hlsl", macros, feature="ScreenSpaceShadows")
    # Mirror the default temporal/adaptive policy while covering all resolutions
    # and both AO-only/GI resource modes. Experimental specular/OCU remains on demand.
    for resolution in (None, "HALF_RES", "QUARTER_RES"):
        for gi in (False, True):
            specs = [
                ("prefilterDepths.cs.hlsl", [("LINEAR_FILTER", "")], True, True, True, False),
                ("prefilterNormal.cs.hlsl", [], True, True, True, False),
                ("radianceDisocc.cs.hlsl", [], True, True, gi, False),
                ("gi.cs.hlsl", [], True, True, gi, True),
                ("gi.cs.hlsl", [("CENTER_FULL_PASS", "")], False, False, gi, True),
                ("upsample.cs.hlsl", [], True, gi, gi, False),
                ("centerBlend.cs.hlsl", [], False, False, gi, False),
            ]
            if gi:
                specs.extend((name, [], True, True, True, False) for name in ("prefilterRadiance.cs.hlsl", "blur.cs.hlsl"))
            if runtime == "VR":
                gi_macro = [("GI", "")] if gi else []
                specs.extend([
                    ("gi.cs.hlsl", [*gi_macro, ("STEREO_EYE0_ONLY", ""), ("FRAMEBUFFER", "")], True, True, False, True),
                    ("reproject.cs.hlsl", [*gi_macro, ("FRAMEBUFFER", "")], True, False, False, False),
                    ("stereoSync.cs.hlsl", [("FRAMEBUFFER", "")], True, False, gi, False),
                    ("stereoSync.cs.hlsl", [("FRAMEBUFFER", ""), ("CENTER_DISPATCH", "")], False, False, gi, False),
                ])
            for name, macros, use_resolution, temporal, use_gi, adaptive in specs:
                macros = list(macros)
                if use_resolution and resolution:
                    macros.append((resolution, ""))
                if temporal and runtime == "SE":
                    macros.append(("TEMPORAL_DENOISER", ""))
                if use_gi and gi:
                    macros.append(("GI", ""))
                if adaptive:
                    macros.append(("ADAPTIVE_SAMPLING", ""))
                add("ScreenSpaceGI/" + name, macros, feature="ScreenSpaceGI")
    return requests


def resolve_tasks(runtime: str, features: dict[str, bool], global_state: str, *, reverse_z_enabled: bool = False) -> dict[str, dict]:
    from hlslkit.shader_digest import to_hex

    result = {}
    for request in release_requests(runtime, features, reverse_z_enabled=reverse_z_enabled):
        digest = compile_digest(request, global_state)
        relative = f"Standalone/{to_hex(digest)}.cso"
        if relative in result:
            previous = result[relative]
            if any(previous[key] != request[key] for key in ("source", "profile", "entry")) or serialize_defines(previous["defines"]) != serialize_defines(request["defines"]):
                raise ValueError(f"standalone cache key collision: {relative}")
            continue
        result[relative] = {**request, "compile_digest": digest}
    return result


def compile_tasks(tasks: dict[str, dict], shader_root: Path, cache_dir: Path, fxc: str, jobs: int) -> None:
    """Compile bounded requests; reject missing sources, failed FXC or bad DXBC."""
    if not 0 < len(tasks) <= 4096 or jobs < 1:
        raise ValueError("invalid standalone compile workload")
    for request in tasks.values():
        source = shader_root / request["source"]
        if not source.is_file() or not source.resolve().is_relative_to(shader_root.resolve()):
            raise ValueError(f"missing or escaped standalone source: {source}")
    (cache_dir / "Standalone").mkdir(exist_ok=True)

    def compile_one(item):
        relative, request = item
        destination = cache_dir / relative
        command = [fxc, "/nologo", "/T", request["profile"], "/E", request["entry"], "/O3", "/Ges", "/Vd", "/I", str(shader_root), "/Fo", str(destination)]
        for name, value in request["defines"]:
            command.extend(("/D", name if value is None else name + "=" + (value or " ")))
        command.append(str(shader_root / request["source"]))
        result = subprocess.run(command, capture_output=True, text=True, errors="replace")
        diagnostics = result.stdout + result.stderr
        if result.returncode:
            raise RuntimeError(f"standalone shader failed: {relative}\n{diagnostics}")
        if diagnostics.strip():
            print(f"{request['source']}: {diagnostics.strip()}")
        validate_dxbc(destination.read_bytes(), STAGES[request["profile"]][1])

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(compile_one, tasks.items()))
