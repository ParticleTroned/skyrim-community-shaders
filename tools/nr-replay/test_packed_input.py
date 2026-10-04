"""Byte-level and admission tests for the offline C atlas experiment."""

import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import packed_input as packed


class PackedInput(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nr-packed-input-")
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.manifest_path = self.source / "manifest.json"
        self.rects = [[1, 1, 3, 3], [16, 12, 4, 3]]
        eyes = []
        self.originals = []
        for eye_index in range(2):
            eye = {"slot": eye_index, "featureUpscaling": True,
                   "motionVectorScale": [16.0, 12.0],
                   "outputSubrect": {"baseX": 0, "baseY": 0, "width": 24, "height": 18}}
            originals = {}
            for role in packed.ROLES:
                width, height = (16, 12) if role in ("depth", "motion") else (24, 18)
                fmt = 41 if role == "depth" else 34 if role == "motion" else 28
                values = []
                for y in range(height):
                    for x in range(width):
                        if role == "depth":
                            pixel = struct.pack("<f", (x + y * width + 1) / 1024)
                        elif role == "motion":
                            pixel = struct.pack("<2e", x / 256, -y / 256)
                        else:
                            pixel = bytes([x + 3 * eye_index + (role == "output"), y, x + y, 255])
                        values.append(pixel)
                data = b"".join(values)
                filename = f"eye-{eye_index}-{role}.bin"
                (self.source / filename).write_bytes(data)
                eye[role] = {"file": filename, "width": width, "height": height,
                             "format": fmt, "rowBytes": width * 4,
                             "sha256": hashlib.sha256(data).hexdigest()}
                originals[role] = packed.Texture(width, height, fmt, width * 4, data)
            self.originals.append(originals)
            eyes.append(eye)
        self.frame = {"mode": 2, "sourceWorldFrame": 79, "generation": 3,
                      "tuning": {"useAutoMask": True, "uiCorrection": False},
                      "eyes": eyes, "colorConfiguration": {"retained": "metadata"}}
        self.manifest = {"schema": "csx-nr-replay-input-v1", "complete": True,
                         "state": "complete", "frames": [self.frame],
                         "runtime": {"sha256": "retained-runtime-identity"}}
        self.serial = 0

    def tearDown(self):
        self.temporary.cleanup()

    def prepare(self, rects=None, halo=2, reverse=False):
        self.serial += 1
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.destination = self.root / f"packed-{self.serial}"
        receipt = packed.prepare(self.manifest_path, self.destination,
                                 self.rects if rects is None else rects, halo, reverse)
        manifest = json.loads((self.destination / "manifest.json").read_text(encoding="utf-8"))
        return receipt, manifest

    def assert_rejected(self, pattern, **kwargs):
        with self.assertRaisesRegex(ValueError, pattern):
            self.prepare(**kwargs)
        self.assertFalse(self.destination.exists(), "invalid inputs must not leave a derived bundle")

    def test_exact_contexts_ownership_guide_phase_and_padding(self):
        receipt, result = self.prepare()
        self.assertEqual(receipt["guidePhaseAlignment"], [3, 3])
        self.assertEqual(receipt["atlasExtent"], [18, 9])
        self.assertEqual(receipt["atlasGuideExtent"], [12, 6])
        self.assertEqual(receipt["enclosingSourceRect"], [1, 1, 19, 14])
        self.assertEqual(receipt["enclosingContextRect"], [0, 0, 24, 18])
        self.assertEqual(receipt["tiles"][1]["atlasOwnedRect"], [10, 3, 4, 3])
        for index, eye in enumerate(result["frames"][0]["eyes"]):
            self.assertEqual(eye["motionVectorScale"], [16, 12])
            for role in packed.ROLES:
                atlas = packed.load_texture(eye[role], self.destination)
                source = self.originals[index][role]
                guides = role in ("depth", "motion")
                for tile in receipt["tiles"]:
                    context = tile["contextRect"]
                    destination = tile["atlasContextRect"]
                    if guides:
                        context = [v * 2 // 3 for v in context]
                        destination = [v * 2 // 3 for v in destination]
                    self.assertEqual(packed.crop_bytes(source, context), packed.crop_bytes(atlas, destination))
                    for y in range(destination[3], atlas.height):
                        self.assertEqual(packed.crop_bytes(atlas, [destination[0], y, destination[2], 1]),
                                         packed.crop_bytes(source, [context[0], context[1] + context[3] - 1, context[2], 1]))
                    if not guides:
                        self.assertEqual(packed.crop_bytes(source, tile["sourceRect"]),
                                         packed.crop_bytes(atlas, tile["atlasOwnedRect"]))
        self.assertNotEqual(result["frames"][0]["eyes"][0]["color"]["sha256"],
                            result["frames"][0]["eyes"][1]["color"]["sha256"])

    def test_reverse_order_preserves_exact_owned_bytes(self):
        forward, first = self.prepare()
        reverse, second = self.prepare(reverse=True)
        self.assertEqual([t["sourceIndex"] for t in reverse["tiles"]], [1, 0])
        self.assertEqual(forward["atlasExtent"], reverse["atlasExtent"])
        self.assertNotEqual(first["frames"][0]["eyes"][0]["color"]["sha256"],
                            second["frames"][0]["eyes"][0]["color"]["sha256"])
        atlas = packed.load_texture(second["frames"][0]["eyes"][0]["color"], self.destination)
        for tile in reverse["tiles"]:
            self.assertEqual(packed.crop_bytes(atlas, tile["atlasOwnedRect"]),
                             packed.crop_bytes(self.originals[0]["color"], tile["sourceRect"]))

    def test_halo_clips_at_edges_and_owned_boundaries_can_touch(self):
        receipt, _ = self.prepare(rects=[[0, 0, 12, 18], [12, 0, 12, 18]], halo=0)
        self.assertEqual(receipt["atlasExtent"], [24, 18])
        self.assertEqual(receipt["tiles"][0]["contextRect"], [0, 0, 12, 18])
        receipt, _ = self.prepare(rects=[[0, 0, 1, 1], [23, 17, 1, 1]], halo=16384)
        self.assertEqual(receipt["atlasExtent"], [48, 18])
        self.assertTrue(all(t["contextRect"] == [0, 0, 24, 18] for t in receipt["tiles"]))

    def test_first_frame_only_and_immutable_provenance(self):
        self.manifest["capturedFrames"] = 2
        self.manifest["payloadBytes"] = 999999
        self.frame["execution"] = {"regions": [{"nativeLayout": "old source grid"}]}
        self.frame["eyes"][0]["viewport"] = {"fullInput": {"width": 24, "height": 18}}
        self.manifest["frames"].append(copy.deepcopy(self.frame))
        before = copy.deepcopy(self.manifest)
        receipt, result = self.prepare()
        self.assertEqual(self.manifest, before)
        self.assertEqual(len(result["frames"]), 1)
        self.assertEqual(result["capturedFrames"], 1)
        self.assertEqual(result["payloadBytes"], receipt["resourceBytes"])
        self.assertNotIn("execution", result["frames"][0])
        self.assertNotIn("viewport", result["frames"][0]["eyes"][0])
        self.assertEqual((self.destination / "source-manifest.json").read_bytes(), self.manifest_path.read_bytes())
        self.assertEqual(receipt["sourceFrameCount"], 2)
        self.assertEqual(receipt["sourceManifestSha256"], packed.digest(self.manifest_path.read_bytes()))
        self.assertEqual(receipt["manifestSha256"], packed.digest((self.destination / "manifest.json").read_bytes()))
        self.assertEqual(result["runtime"], before["runtime"])
        self.assertEqual(result["frames"][0]["colorConfiguration"], before["frames"][0]["colorConfiguration"])
        self.assertEqual(receipt["historyPolicy"], "static_reset")
        self.assertEqual(sum(len((self.destination / eye[role]["file"]).read_bytes())
                             for eye in result["frames"][0]["eyes"] for role in packed.ROLES), receipt["resourceBytes"])

    def test_invalid_count_geometry_overlap_and_halo(self):
        for rects in ([[0, 0, 1, 1]], [[0, 0, 5, 5], [4, 4, 2, 2]],
                      [[0, 0, 0, 1], [5, 5, 1, 1]], [[23, 0, 2, 1], [5, 5, 1, 1]],
                      [[True, 0, 1, 1], [5, 5, 1, 1]]):
            self.assert_rejected("rectangles|rectangle|overlaps", rects=rects)
        for halo in (-1, 16385, True, 1.0):
            self.assert_rejected("halo", halo=halo)

    def test_mode_completion_and_input_contract(self):
        self.frame["mode"] = 1
        self.assert_rejected("stateless C")
        self.frame["mode"] = 2
        self.manifest["complete"] = False
        self.assert_rejected("incomplete")
        self.manifest["complete"] = True
        self.frame["eyes"][0]["motionVectorScale"] = [float("nan"), 1]
        self.assert_rejected("nonfinite")

    def test_hash_pitch_and_missing_bytes_rejected(self):
        descriptor = self.frame["eyes"][0]["color"]
        saved = copy.deepcopy(descriptor)
        descriptor["sha256"] = "0" * 64
        self.assert_rejected("hash")
        descriptor.update(saved, rowBytes=1)
        self.assert_rejected("tightly packed")
        descriptor.update(saved)
        path = self.source / descriptor["file"]
        path.write_bytes(path.read_bytes()[:-4])
        self.assert_rejected("length mismatch")

    def test_escaped_absolute_and_symlink_paths_rejected(self):
        descriptor = self.frame["eyes"][0]["color"]
        saved = descriptor["file"]
        for path in ("../outside.bin", "..\\outside.bin", "C:\\outside.bin", str(self.root / "outside.bin")):
            descriptor["file"] = path
            self.assert_rejected("relative|escapes")
        outside = self.root / "outside.bin"
        outside.write_bytes((self.source / saved).read_bytes())
        link = self.source / "link.bin"
        try:
            link.symlink_to(outside)
        except OSError as error:
            self.skipTest(f"symlink creation unavailable: {error}")
        descriptor["file"] = link.name
        self.assert_rejected("outside bundle")

    def test_nonfinite_guides_are_rejected_even_outside_owned_rects(self):
        for role, pixel in (("depth", struct.pack("<f", float("inf"))),
                            ("motion", struct.pack("<2e", float("nan"), 0))):
            descriptor = self.frame["eyes"][0][role]
            path = self.source / descriptor["file"]
            original = path.read_bytes()
            data = pixel + original[4:]
            path.write_bytes(data)
            descriptor["sha256"] = packed.digest(data)
            self.assert_rejected("nonfinite captured texture")
            path.write_bytes(original)
            descriptor["sha256"] = packed.digest(original)

    def test_no_overwrite_input_and_derived_byte_budgets(self):
        self.prepare()
        before = (self.destination / "manifest.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "no overwrite"):
            packed.prepare(self.manifest_path, self.destination, self.rects, 2)
        self.assertEqual(before, (self.destination / "manifest.json").read_bytes())
        with self.assertRaisesRegex(ValueError, "budget"):
            packed.load_texture(self.frame["eyes"][0]["color"], self.source, 1)
        source_size = sum(len(t.data) for eye in self.originals for t in eye.values())
        with patch.object(packed, "BUNDLE_BUDGET", source_size + 1):
            self.assert_rejected("packed resources exceed", halo=16384)

    def test_noncongruent_stereo_and_partial_initialized_domain_rejected(self):
        self.frame["eyes"][1]["featureUpscaling"] = False
        self.assert_rejected("stereo feature")
        self.frame["eyes"][1]["featureUpscaling"] = True
        self.frame["eyes"][0]["outputSubrect"]["baseX"] = 1
        self.assert_rejected("full initialized")

    def test_native_extent_and_format_rejected(self):
        with self.assertRaisesRegex(ValueError, "extent limit"):
            packed._layout([[0, 0, 4000, 2], [4000, 0, 4000, 2]], 16384,
                           10000, 2, 10000, 2, False)
        self.frame["eyes"][0]["color"]["format"] = 999
        self.assert_rejected("unsupported capture format")

    def test_read_allocates_for_file_size_and_rejects_concurrent_size_changes(self):
        class RecordedRead(io.BytesIO):
            def read(self, size):
                self.requested_size = size
                return super().read(size)

        for data in (b"abc", b"ab", b"abcd"):
            stream = RecordedRead(data)
            path = SimpleNamespace(is_file=lambda: True, stat=lambda: SimpleNamespace(st_size=3),
                                   open=lambda mode: stream)
            if len(data) == 3:
                self.assertEqual(packed._read(path, packed.BUNDLE_BUDGET), data)
            else:
                with self.assertRaisesRegex(ValueError, "size changed"):
                    packed._read(path, packed.BUNDLE_BUDGET)
            self.assertEqual(stream.requested_size, 4)


if __name__ == "__main__":
    unittest.main()
