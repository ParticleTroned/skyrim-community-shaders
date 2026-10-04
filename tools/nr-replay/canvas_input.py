"""Equal-size atlas controls that preserve tile counts while changing adjacency."""

import copy
import json
from pathlib import Path

from packed_input import (BUNDLE_BUDGET, FORMATS, ROLES, Texture, crop_bytes,
                          derived_manifest, digest, load_texture, prepare, require)


ARRANGEMENTS = {
    "horizontal": (0, 1, 0, 1),
    "vertical": (0, 0, 1, 1),
    "diagonal": (0, 1, 1, 0),
}


def expand(texture, contexts, arrangement):
    """Place two equal square contexts twice each in an exactly filled canvas."""
    require(arrangement in ARRANGEMENTS, "unknown canvas arrangement")
    require(len(contexts) == 2 and contexts[0][2:] == contexts[1][2:]
            and contexts[0][2] == contexts[0][3], "canvas control needs two equal square contexts")
    side = contexts[0][2]
    require(0 < side <= 8192, "canvas exceeds native extent limit")
    stride = FORMATS[texture.format]
    require((side * 2) ** 2 * stride <= BUNDLE_BUDGET, "canvas exceeds replay byte budget")
    tiles = [crop_bytes(texture, context) for context in contexts]
    row_bytes = side * stride
    output = bytearray()
    order = ARRANGEMENTS[arrangement]
    for row in range(2 * side):
        offset = (row % side) * row_bytes
        for column in range(2):
            tile = tiles[order[(row // side) * 2 + column]]
            output.extend(tile[offset:offset + row_bytes])
    return Texture(side * 2, side * 2, texture.format, row_bytes * 2, bytes(output))


def prepare_canvas(manifest_path, output_root, rects, halo, reverse=False, arrangement="horizontal"):
    """Derive a fixed square page; duplicate context is a disclosed diagnostic control."""
    output_root = Path(output_root)
    require(not output_root.exists() and not output_root.is_symlink(), "output directory already exists; no overwrite")
    require(arrangement in ARRANGEMENTS, "unknown canvas arrangement")
    require(len(rects) == 2, "canvas control requires exactly two owned regions")
    # The base builder admits source identity, finite inputs and exact guide phase.
    proof = prepare(Path(manifest_path), output_root / "base", rects, halo, reverse)
    base = json.loads((output_root / "base/manifest.json").read_text(encoding="utf-8"))
    contexts = [tile["atlasContextRect"] for tile in proof["tiles"]]
    require(contexts[0][2:] == contexts[1][2:] and contexts[0][2] == contexts[0][3],
            "canvas control needs two equal square contexts; clipped unequal contexts are unsupported")
    require(proof["sourceExtent"] == proof["sourceGuideExtent"], "native canvas control requires equal guide grids")
    side = contexts[0][2]
    payload_bytes = len(base["frames"][0]["eyes"]) * (side * 2) ** 2 * sum(
        FORMATS[base["frames"][0]["eyes"][0][role]["format"]] for role in ROLES)
    require(payload_bytes <= BUNDLE_BUDGET, "canvas exceeds total replay byte budget")
    result = derived_manifest(base, payload_bytes, "offline_canvas_control", "offline_stateless_canvas_control")
    receipt = copy.deepcopy(proof)
    receipt.update(schema="csx-nr-canvas-input-v1", canvasArrangement=arrangement,
                   contextCopiesPerSource=2, canvasTileOrder=list(ARRANGEMENTS[arrangement]),
                   canvasTileSourceOrder=[proof["tiles"][i]["sourceIndex"] for i in ARRANGEMENTS[arrangement]],
                   atlasExtent=[2 * side, 2 * side], atlasGuideExtent=[2 * side, 2 * side],
                   paddingPolicy="no_padding_each_context_appears_twice", resourceBytes=payload_bytes)
    for position, tile in enumerate(receipt["tiles"]):
        cell = ARRANGEMENTS[arrangement].index(position)
        x, y = (cell % 2) * side, (cell // 2) * side
        previous = tile["atlasContextRect"]
        owned = tile["atlasOwnedRect"]
        tile["atlasOwnedRect"] = [x + owned[0] - previous[0], y + owned[1] - previous[1], *owned[2:]]
        tile["atlasContextRect"] = [x, y, side, side]
    receipt["sourceCapture"]["manifestFile"] = "base/source-manifest.json"
    receipt.pop("manifestSha256", None)
    receipt.pop("resourceSha256", None)
    result["packedRegionExperiment"] = copy.deepcopy(receipt)
    for index, original in enumerate(base["frames"][0]["eyes"]):
        eye = result["frames"][0]["eyes"][index]
        for role in ROLES:
            source = load_texture(original[role], output_root / "base")
            canvas = expand(source, contexts, arrangement)
            filename = f"eye-{index}-{role}.bin"
            (output_root / filename).write_bytes(canvas.data)
            eye[role] = {"file": filename, "bytes": len(canvas.data), "width": canvas.width,
                         "height": canvas.height, "format": canvas.format, "rowBytes": canvas.row_bytes,
                         "sha256": digest(canvas.data)}
        eye["outputSubrect"] = dict(baseX=0, baseY=0, width=2 * side, height=2 * side)
    encoded = (json.dumps(result, indent=2, allow_nan=False) + "\n").encode("utf-8")
    (output_root / "manifest.json").write_bytes(encoded)
    receipt["manifestSha256"] = digest(encoded)
    receipt["resourceSha256"] = [{role: eye[role]["sha256"] for role in ROLES} for eye in result["frames"][0]["eyes"]]
    (output_root / "packing.json").write_text(json.dumps(receipt, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return receipt
