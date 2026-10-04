"""Build immutable, stateless C replay atlases without resampling captured input.

This is an offline experiment, not a qualified native batching contract. Each
eye gets its own atlas. Contexts may overlap in the source; output ownership may
not. Padding repeats the last context row of its own tile and never another tile.
"""

from __future__ import annotations

import copy
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path, PureWindowsPath
import struct


BUNDLE_BUDGET = 512 * 1024 * 1024
MANIFEST_BUDGET = 8 * 1024 * 1024
FORMATS = {2: 16, 10: 8, 26: 4, 28: 4, 34: 4, 41: 4}
ROLES = ("color", "depth", "motion", "output")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def uint(value: object, maximum: int = 16384) -> bool:
    return type(value) is int and 0 <= value <= maximum


def _finite_tree(value: object) -> None:
    if isinstance(value, float):
        require(math.isfinite(value), "nonfinite manifest value")
    elif isinstance(value, dict):
        for child in value.values():
            _finite_tree(child)
    elif isinstance(value, list):
        for child in value:
            _finite_tree(child)


def _read(path: Path, maximum: int) -> bytes:
    require(path.is_file() and path.stat().st_size <= maximum, "file exceeds bounded replay budget or is not a file")
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum, "file exceeds bounded replay budget")
    return data


@dataclass(frozen=True)
class Texture:
    width: int
    height: int
    format: int
    row_bytes: int
    data: bytes


def load_texture(value: dict, root: Path, maximum: int = BUNDLE_BUDGET) -> Texture:
    """Admit one tightly packed, hash-identified resource inside its bundle."""
    width, height, fmt = value["width"], value["height"], value["format"]
    require(uint(width) and width > 0 and uint(height) and height > 0, "invalid capture extent")
    require(type(fmt) is int and fmt in FORMATS, "unsupported capture format")
    row_bytes = width * FORMATS[fmt]
    require(type(value["rowBytes"]) is int and value["rowBytes"] == row_bytes, "capture rows are not tightly packed")
    filename = value["file"]
    require(isinstance(filename, str) and filename, "capture resource must be relative")
    relative = Path(filename)
    windows = PureWindowsPath(filename)
    require(not relative.is_absolute() and not windows.root and not windows.drive,
            "capture resource must be relative")
    require(".." not in relative.parts and ".." not in windows.parts, "capture resource escapes bundle")
    resolved_root = root.resolve(strict=True)
    path = (resolved_root / relative).resolve(strict=True)
    require(path.is_relative_to(resolved_root), "capture resource resolves outside bundle")
    data = _read(path, maximum)
    require(len(data) == row_bytes * height, "capture resource length mismatch")
    expected = value["sha256"]
    require(isinstance(expected, str) and digest(data) == expected.lower(), "capture resource hash mismatch")
    return Texture(width, height, fmt, row_bytes, data)


def _rect(rect: list[int], width: int, height: int) -> None:
    require(isinstance(rect, (list, tuple)) and len(rect) == 4
            and all(uint(v) for v in rect), "rectangle must contain four bounded integers")
    x, y, w, h = rect
    require(w > 0 and h > 0 and x + w <= width and y + h <= height, "rectangle outside captured texture")


def crop_bytes(texture: Texture, rect: list[int]) -> bytes:
    """Extract exact pixel bytes using an x, y, width, height rectangle."""
    _rect(rect, texture.width, texture.height)
    x, y, width, height = rect
    stride = FORMATS[texture.format]
    return b"".join(texture.data[row * texture.row_bytes + x * stride:
                                 row * texture.row_bytes + (x + width) * stride]
                    for row in range(y, y + height))


def _finite_texture(texture: Texture) -> None:
    if texture.format == 28:
        return
    if texture.format == 26:
        finite = all((pixel[0] & 0x7C0) != 0x7C0
                     and ((pixel[0] >> 11) & 0x7C0) != 0x7C0
                     and ((pixel[0] >> 22) & 0x3E0) != 0x3E0
                     for pixel in struct.iter_unpack("<I", texture.data))
    else:
        fmt = "<e" if texture.format in (10, 34) else "<f"
        finite = all(math.isfinite(item[0]) for item in struct.iter_unpack(fmt, texture.data))
    require(finite, "nonfinite captured texture cannot initialize finite atlas padding")


def _bounding(rects: list[list[int]]) -> list[int]:
    x, y = min(r[0] for r in rects), min(r[1] for r in rects)
    return [x, y, max(r[0] + r[2] for r in rects) - x,
            max(r[1] + r[3] for r in rects) - y]


def _scale_rect(rect: list[int], width: int, height: int,
                guide_width: int, guide_height: int) -> list[int]:
    factors = (guide_width, guide_height, guide_width, guide_height)
    divisors = (width, height, width, height)
    require(all(v * n % d == 0 for v, n, d in zip(rect, factors, divisors)), "context does not preserve guide phase")
    return [v * n // d for v, n, d in zip(rect, factors, divisors)]


def _layout(rects: list[list[int]], halo: int, width: int, height: int,
            guide_width: int, guide_height: int, reverse: bool) -> dict:
    require(isinstance(rects, list) and 2 <= len(rects) <= 4, "packing requires two to four owned rectangles")
    require(uint(halo), "halo must be a bounded nonnegative integer")
    for index, rect in enumerate(rects):
        _rect(rect, width, height)
        x, y, w, h = rect
        for other in rects[:index]:
            ox, oy, ow, oh = other
            require(x + w <= ox or ox + ow <= x or y + h <= oy or oy + oh <= y,
                    "source output ownership overlaps")
    alignment = [width // math.gcd(width, guide_width), height // math.gcd(height, guide_height)]
    tiles = []
    atlas_width, atlas_height = 0, 0
    indices = list(reversed(range(len(rects)))) if reverse else range(len(rects))
    for index in indices:
        x, y, w, h = rects[index]
        ax, ay = alignment
        left = max(0, x - halo) // ax * ax
        top = max(0, y - halo) // ay * ay
        right = min(width, ((x + w + halo + ax - 1) // ax) * ax)
        bottom = min(height, ((y + h + halo + ay - 1) // ay) * ay)
        context = [left, top, right - left, bottom - top]
        atlas_context = [atlas_width, 0, context[2], context[3]]
        atlas_owned = [atlas_width + x - left, y - top, w, h]
        tiles.append({"sourceIndex": index, "sourceRect": list(rects[index]),
                      "contextRect": context, "atlasContextRect": atlas_context,
                      "atlasOwnedRect": atlas_owned})
        atlas_width += context[2]
        atlas_height = max(atlas_height, context[3])
    require(0 < atlas_width <= 16384 and 0 < atlas_height <= 16384, "atlas exceeds native extent limit")
    guide_extent = _scale_rect([0, 0, atlas_width, atlas_height], width, height, guide_width, guide_height)[2:]
    require(all(0 < v <= 16384 for v in guide_extent), "atlas guides exceed native extent limit")
    return {"tiles": tiles, "sourceExtent": [width, height],
            "sourceGuideExtent": [guide_width, guide_height], "guidePhaseAlignment": alignment,
            "atlasExtent": [atlas_width, atlas_height], "atlasGuideExtent": guide_extent,
            "enclosingSourceRect": _bounding(rects),
            "enclosingContextRect": _bounding([tile["contextRect"] for tile in tiles])}


def _pack(texture: Texture, layout: dict, guides: bool) -> Texture:
    width, height = layout["atlasGuideExtent" if guides else "atlasExtent"]
    stride = FORMATS[texture.format]
    row_bytes = width * stride
    result = bytearray(row_bytes * height)
    for tile in layout["tiles"]:
        context, destination = tile["contextRect"], tile["atlasContextRect"]
        if guides:
            args = (*layout["sourceExtent"], *layout["sourceGuideExtent"])
            context = _scale_rect(context, *args)
            destination = _scale_rect(destination, *args)
        x, y, w, h = context
        for row in range(height):
            source_offset = (y + min(row, h - 1)) * texture.row_bytes + x * stride
            target_offset = row * row_bytes + destination[0] * stride
            result[target_offset:target_offset + w * stride] = texture.data[source_offset:source_offset + w * stride]
    return Texture(width, height, texture.format, row_bytes, bytes(result))


def prepare(manifest_path: Path, output_root: Path, rects: list[list[int]],
            halo: int, reverse: bool = False) -> dict:
    """Write a fresh atlas bundle and receipt for the first captured C frame.

    The caller must use reset-every-evaluation replay. Motion bytes and their
    explicit full-input-to-guide-pixel conversion remain unchanged: translation
    does not change displacement. Temporal tile transitions are not qualified.
    """
    require(type(reverse) is bool, "reverse must be boolean")
    require(not output_root.exists() and not output_root.is_symlink(), "output directory already exists; no overwrite")
    source_bytes = _read(manifest_path, MANIFEST_BUDGET)
    manifest = json.loads(source_bytes)
    _finite_tree(manifest)
    require(manifest["schema"] == "csx-nr-replay-input-v1", "unsupported native replay schema")
    require(manifest["complete"] is True and manifest["state"] == "complete", "native input capture is incomplete")
    frames = manifest["frames"]
    require(isinstance(frames, list) and 1 <= len(frames) <= 32, "capture must contain one to 32 frames")
    frame = frames[0]
    require(type(frame["mode"]) is int and frame["mode"] == 2, "packed input experiment supports only stateless C")
    require(frame["tuning"]["useAutoMask"] is True and frame["tuning"]["uiCorrection"] is False,
            "only native auto-mask is supported")
    require(isinstance(frame["eyes"], list) and 1 <= len(frame["eyes"]) <= 2, "invalid captured eye count")
    textures, source_hashes = [], []
    remaining = BUNDLE_BUDGET
    for eye in frame["eyes"]:
        require(type(eye["featureUpscaling"]) is bool, "invalid captured feature mode")
        scale = eye["motionVectorScale"]
        require(isinstance(scale, list) and len(scale) == 2
                and all(type(v) in (int, float) and math.isfinite(v) and v > 0 for v in scale), "invalid motion scale")
        resources = {}
        for role in ROLES:
            resources[role] = load_texture(eye[role], manifest_path.parent, remaining)
            remaining -= len(resources[role].data)
            _finite_texture(resources[role])
        color, depth, motion, output = (resources[r] for r in ROLES)
        require(color.format in (2, 10, 26, 28) and output.format == color.format,
                "unsupported or mismatched colour/output format")
        require(depth.format == 41 and motion.format == 34, "unsupported guide formats")
        require((depth.width, depth.height) == (motion.width, motion.height), "depth/motion guide grids differ")
        require((color.width, color.height) == (output.width, output.height), "colour/output grids differ")
        require(eye["outputSubrect"] == {"baseX": 0, "baseY": 0, "width": color.width, "height": color.height},
                "capture must contain full initialized native resource domain")
        if textures:
            require(eye["featureUpscaling"] == frame["eyes"][0]["featureUpscaling"], "stereo feature mode differs")
            for role in ROLES:
                a, b = resources[role], textures[0][role]
                require((a.width, a.height, a.format) == (b.width, b.height, b.format), "stereo resource grids differ")
        textures.append(resources)
        source_hashes.append({role: digest(resources[role].data) for role in ROLES})
    color, depth = textures[0]["color"], textures[0]["depth"]
    layout = _layout(rects, halo, color.width, color.height, depth.width, depth.height, reverse)
    estimated_bytes = len(textures) * sum(
        math.prod(layout["atlasGuideExtent" if role in ("depth", "motion") else "atlasExtent"])
        * FORMATS[textures[0][role].format] for role in ROLES)
    require(estimated_bytes <= BUNDLE_BUDGET, "packed resources exceed bounded replay budget")
    frame_keys = ("mode", "sourceWorldFrame", "frame", "generation", "inputEpoch",
                  "colorRevision", "insertionPoint", "tuning", "colorConfiguration", "characterSelection")
    derived_frame = {key: copy.deepcopy(frame[key]) for key in frame_keys if key in frame}
    derived_frame["stage"] = "offline_packed_native_input_fixture"
    derived_frame["eyes"] = [{key: copy.deepcopy(eye[key]) for key in
                              ("featureUpscaling", "motionVectorScale")} for eye in frame["eyes"]]
    result = {key: copy.deepcopy(manifest[key]) for key in ("schema", "runtime", "adapter") if key in manifest}
    result.update(complete=True, state="complete", frames=[derived_frame], capturedFrames=1,
                  requestedFrames=1, payloadBytes=estimated_bytes, byteBudget=BUNDLE_BUDGET,
                  byteBudgetAccounting="row_packed_derived_resources_excludes_driver_allocation_padding",
                  replayScope="offline_stateless_packed_input_not_captured_game_execution",
                  captureTimingIsPerformanceEvidence=False)
    receipt = {"schema": "csx-nr-packed-input-v1", "sourceManifestSha256": digest(source_bytes),
               "sourceManifest": str(manifest_path.resolve()), "sourceFrameIndex": 0,
               "sourceCapture": {"manifestFile": "source-manifest.json", "manifestSha256": digest(source_bytes),
                                 "metadataAppliesTo": "original_capture_only_not_derived_atlas"},
               "sourceFrameCount": len(frames), "sourceWorldFrame": frame["sourceWorldFrame"],
               "sourceResourceSha256": source_hashes, "halo": halo, "reverse": reverse,
               "historyPolicy": "static_reset", "paddingPolicy": "repeat_last_context_row_per_tile",
               "motionPolicy": "preserve_normalized_bytes_and_explicit_captured_guide_pixel_scale",
               "qualification": "offline_stateless_experiment_only", "eyes": len(textures),
               "resourceBytes": estimated_bytes, **layout}
    result["packedRegionExperiment"] = copy.deepcopy(receipt)
    output_root.mkdir(parents=True, exist_ok=False)
    with (output_root / "source-manifest.json").open("xb") as stream:
        stream.write(source_bytes)
    for index, resources in enumerate(textures):
        eye = result["frames"][0]["eyes"][index]
        for role in ROLES:
            atlas = _pack(resources[role], layout, role in ("depth", "motion"))
            filename = f"eye-{index}-{role}.bin"
            with (output_root / filename).open("xb") as stream:
                stream.write(atlas.data)
            eye[role] = {"file": filename, "bytes": len(atlas.data), "width": atlas.width, "height": atlas.height,
                         "format": atlas.format, "rowBytes": atlas.row_bytes, "sha256": digest(atlas.data)}
        eye["outputSubrect"] = {"baseX": 0, "baseY": 0, "width": layout["atlasExtent"][0], "height": layout["atlasExtent"][1]}
    derived_bytes = (json.dumps(result, indent=2, allow_nan=False) + "\n").encode("utf-8")
    with (output_root / "manifest.json").open("xb") as stream:
        stream.write(derived_bytes)
    receipt["manifestSha256"] = digest(derived_bytes)
    receipt["resourceSha256"] = [{role: eye[role]["sha256"] for role in ROLES} for eye in result["frames"][0]["eyes"]]
    with (output_root / "packing.json").open("x", encoding="utf-8") as stream:
        json.dump(receipt, stream, indent=2, allow_nan=False)
        stream.write("\n")
    return receipt
