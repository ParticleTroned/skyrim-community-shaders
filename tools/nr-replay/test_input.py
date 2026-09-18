"""Exercise native input admission with synthetic parser fixtures, never timing."""

import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = Path(sys.argv.pop(1)).resolve()


class ReplayInput(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nr-replay-input-")
        self.root = Path(self.temporary.name)
        self.manifest_path = self.root / "manifest.json"
        self.case = 0
        self.eye = {"slot": 0, "featureUpscaling": True,
                    "motionVectorScale": [64.0, 64.0],
                    "outputSubrect": {"baseX": 0, "baseY": 0, "width": 3, "height": 5}}
        for role in ("color", "depth", "motion", "output"):
            width, height = (2, 3) if role in ("depth", "motion") else (3, 5)
            format_ = 41 if role == "depth" else (34 if role == "motion" else 28)
            pixels = bytes([20, 40, 60, 255] * (width * height))
            if role == "depth":
                pixels = struct.pack("<f", 0.5) * (width * height)
            elif role == "motion":
                pixels = bytes(width * height * 4)
            elif role == "output":
                pixels = bytes([21, 40, 60, 255] * (width * height))
            file = self.root / (role + ".bin")
            file.write_bytes(pixels)
            self.eye[role] = {"file": file.name, "width": width, "height": height,
                              "format": format_, "rowBytes": width * 4,
                              "sha256": hashlib.sha256(pixels).hexdigest()}
        self.frame = {"sourceWorldFrame": 10, "frame": 10, "generation": 1,
                      "mode": 0, "insertionPoint": 0, "colorRevision": 1,
                      "inputEpoch": 1, "colorConfiguration": {},
                      "tuning": {"useAutoMask": True, "uiCorrection": False},
                      "eyes": [self.eye]}
        self.manifest = {"schema": "csx-nr-replay-input-v1", "complete": True,
                         "state": "complete", "frames": [self.frame]}

    def tearDown(self):
        self.temporary.cleanup()

    def run_fixture(self, expected=0):
        self.case += 1
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        output = self.root / f"result-{self.case}"
        run = subprocess.run([str(EXECUTABLE), "--manifest", str(self.manifest_path),
                              "--output", str(output), "--validate-input"],
                             capture_output=True, text=True, timeout=15)
        self.assertEqual(run.returncode, expected, run.stdout + run.stderr)
        result = json.loads((output / "results.json").read_text(encoding="utf-8"))
        self.assertEqual(result["cases"], [])
        return result

    def test_scaled_odd_native_grids(self):
        result = self.run_fixture()
        self.assertEqual(result["status"], "input_validated_no_runtime_measurement")
        identity = result["buildIdentity"]
        self.assertEqual(identity["executableSha256"], hashlib.sha256(EXECUTABLE.read_bytes()).hexdigest())
        repository = Path(__file__).resolve().parents[2]
        for path, digest in identity["replaySourceSha256"].items():
            self.assertEqual(len(digest), 64)
            if not path.startswith("generated/"):
                self.assertEqual(digest, hashlib.sha256((repository / path).read_bytes()).hexdigest())

    def test_checksum_is_required_and_checked(self):
        self.eye["color"]["sha256"] = "0" * 64
        self.assertIn("hash", self.run_fixture(1)["reason"])
        del self.eye["color"]["sha256"]
        self.run_fixture(1)

    def test_partial_capture_rejected(self):
        self.manifest["complete"] = False
        self.assertIn("incomplete", self.run_fixture(1)["reason"])

    def test_payload_size_and_row_pitch(self):
        self.eye["color"]["rowBytes"] = 16
        self.assertIn("packed", self.run_fixture(1)["reason"])

    def test_path_escape_rejected(self):
        self.eye["color"]["file"] = "../outside.bin"
        self.assertIn("escapes", self.run_fixture(1)["reason"])

    def test_zero_edit_control_rejected(self):
        self.eye["output"] = copy.deepcopy(self.eye["color"])
        self.assertIn("nonzero", self.run_fixture(1)["reason"])

    def test_native_float_decoders_and_nonfinite_control(self):
        for format_, source, edited, nan in (
            (10, struct.pack("<4H", 0x3800, 0x3800, 0x3800, 0x3c00),
             struct.pack("<4H", 0x3801, 0x3800, 0x3800, 0x3c00),
             struct.pack("<4H", 0x7e01, 0x3800, 0x3800, 0x3c00)),
            (26, struct.pack("<I", 0x3c0 | (0x3c0 << 11) | (0x1e0 << 22)),
             struct.pack("<I", 0x3c1 | (0x3c0 << 11) | (0x1e0 << 22)),
             struct.pack("<I", 0x7c1 | (0x3c0 << 11) | (0x1e0 << 22))),
        ):
            for role, pixel in (("color", source), ("output", edited)):
                payload = pixel * 15
                (self.root / (role + ".bin")).write_bytes(payload)
                self.eye[role].update(format=format_, rowBytes=len(pixel) * 3,
                                      sha256=hashlib.sha256(payload).hexdigest())
            self.run_fixture()
            payload = nan * 15
            (self.root / "output.bin").write_bytes(payload)
            self.eye["output"]["sha256"] = hashlib.sha256(payload).hexdigest()
            self.assertIn("nonfinite", self.run_fixture(1)["reason"])

    def test_temporal_gap_and_repeat_rejected(self):
        second = copy.deepcopy(self.frame)
        self.manifest["frames"].append(second)
        self.run_fixture(1)
        second["sourceWorldFrame"] = 12
        self.run_fixture(1)
        second["sourceWorldFrame"] = 11
        self.run_fixture()

    def test_existing_output_preserved(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        target = self.root / "existing"
        target.mkdir()
        marker = target / "results.json"
        marker.write_bytes(b"user-owned-evidence")
        run = subprocess.run([str(EXECUTABLE), "--manifest", str(self.manifest_path),
                              "--output", str(target), "--validate-input"],
                             capture_output=True, text=True, timeout=15)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(marker.read_bytes(), b"user-owned-evidence")


if __name__ == "__main__":
    unittest.main()
