"""Verify canvas controls preserve bytes, population and ownership mappings."""

from collections import Counter
import json
from pathlib import Path
import struct
import tempfile
import unittest

from canvas_input import ARRANGEMENTS, expand, prepare_canvas
from packed_input import Texture, crop_bytes, digest, load_texture


class CanvasInput(unittest.TestCase):
    def test_arrangement_preserves_contexts_and_histograms(self):
        source = Texture(4, 2, 28, 16, b"".join(bytes([p, p + 1, p + 2, 255]) for p in range(8)))
        contexts = [[0, 0, 2, 2], [2, 0, 2, 2]]
        expected = Counter(crop_bytes(source, contexts[0]) + crop_bytes(source, contexts[1]))
        for arrangement, order in ARRANGEMENTS.items():
            canvas = expand(source, contexts, arrangement)
            self.assertEqual([canvas.width, canvas.height], [4, 4])
            self.assertEqual(Counter(canvas.data), expected + expected)
            for cell, tile in enumerate(order):
                self.assertEqual(crop_bytes(canvas, [(cell % 2) * 2, (cell // 2) * 2, 2, 2]),
                                 crop_bytes(source, contexts[tile]))

    def test_unsupported_or_inexact_layout_rejected(self):
        source = Texture(4, 2, 28, 16, bytes(32))
        for contexts, mode in (([[0, 0, 2, 2], [2, 0, 2, 1]], "horizontal"),
                               ([[0, 0, 2, 2], [2, 0, 2, 2]], "unknown"),
                               ([[0, 0, 1, 2], [2, 0, 1, 2]], "vertical")):
            with self.assertRaises(ValueError):
                expand(source, contexts, mode)

    def test_derived_bundle_maps_both_owned_regions_for_every_order(self):
        with tempfile.TemporaryDirectory(prefix="nr-canvas-test-") as directory:
            root = Path(directory)
            eye = {"featureUpscaling": False, "motionVectorScale": [8, 4],
                   "outputSubrect": dict(baseX=0, baseY=0, width=8, height=4)}
            originals = {}
            for role, fmt in (("color", 28), ("output", 28), ("depth", 41), ("motion", 34)):
                data = b"".join(struct.pack("<f", p / 128) if role == "depth"
                                else struct.pack("<2e", p / 256, 0) if role == "motion"
                                else bytes([p + (role == "output"), p, 0, 255]) for p in range(32))
                (root / (role + ".bin")).write_bytes(data)
                eye[role] = dict(file=role + ".bin", width=8, height=4, format=fmt,
                                 rowBytes=32, sha256=digest(data))
                originals[role] = Texture(8, 4, fmt, 32, data)
            manifest = {"schema": "csx-nr-replay-input-v1", "complete": True, "state": "complete",
                        "frames": [{"mode": 2, "sourceWorldFrame": 5,
                                    "tuning": {"useAutoMask": True, "uiCorrection": False}, "eyes": [eye]}]}
            path = root / "source.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            rects = [[0, 1, 2, 2], [6, 1, 2, 2]]
            for arrangement in ARRANGEMENTS:
                for reverse in (False, True):
                    destination = root / f"{arrangement}-{reverse}"
                    proof = prepare_canvas(path, destination, rects, 0, reverse, arrangement)
                    result = json.loads((destination / "manifest.json").read_text())
                    self.assertEqual(result["frames"][0]["eyes"][0]["motionVectorScale"], [8, 4])
                    self.assertEqual(proof["contextCopiesPerSource"], 2)
                    self.assertEqual(proof["canvasTileSourceOrder"],
                                     [1 - i if reverse else i for i in ARRANGEMENTS[arrangement]])
                    for role in originals:
                        canvas = load_texture(result["frames"][0]["eyes"][0][role], destination)
                        for tile in proof["tiles"]:
                            self.assertEqual(crop_bytes(canvas, tile["atlasOwnedRect"]),
                                             crop_bytes(originals[role], tile["sourceRect"]))
                    self.assertEqual(digest((destination / "manifest.json").read_bytes()), proof["manifestSha256"])
                    with self.assertRaisesRegex(ValueError, "no overwrite"):
                        prepare_canvas(path, destination, rects, 0, reverse, arrangement)


if __name__ == "__main__":
    unittest.main()
