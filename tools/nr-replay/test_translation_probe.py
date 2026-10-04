"""CPU-only translation and fail-closed orchestration checks."""

import copy
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import translation_probe as probe


class TranslationProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nr-translation-")
        self.root = Path(self.temp.name)
        source = self.root / "source"
        source.mkdir()
        self.runtime, self.executable = self.root / "nr.dll", self.root / "replay.exe"
        self.runtime.write_bytes(b"runtime identity only; never loaded")
        self.executable.write_bytes(b"executable identity only; never executed")
        eyes = []
        for index in range(2):
            eye = {"featureUpscaling": False, "motionVectorScale": [128., 96.], "slot": index,
                   "nativeLayout": {"stale": True},
                   "outputSubrect": dict(baseX=0, baseY=0, width=128, height=96)}
            for role, fmt in (("color", 28), ("depth", 41), ("motion", 34), ("output", 28)):
                data = b"".join(struct.pack("<f", (x + y + 1) / 512) if fmt == 41 else
                                struct.pack("<2e", x / 256, -y / 256) if fmt == 34 else
                                bytes([x, y, index + (role == "output"), 255])
                                for y in range(96) for x in range(128))
                name = f"eye-{index}-{role}.bin"
                (source / name).write_bytes(data)
                eye[role] = dict(file=name, width=128, height=96, format=fmt,
                                 rowBytes=512, sha256=probe.digest(data))
            eyes.append(eye)
        self.manifest = {"schema": "csx-nr-replay-input-v1", "complete": True, "state": "complete",
                         "runtime": {"sha256": probe.file_digest(self.runtime)}, "execution": "stale",
                         "frames": [{"mode": 2, "sourceWorldFrame": 123, "eyes": eyes,
                                     "tuning": {"useAutoMask": True, "uiCorrection": False},
                                     "colorConfiguration": {"unchanged": True}}]}
        self.args = SimpleNamespace(manifest=source / "manifest.json", replay=self.executable,
                                    runtime=self.runtime, output=self.root / "campaign", roi=[16, 16, 64, 64],
                                    shifts="0,1,16,32", samples=2, warmup=1, repeats=2, seconds=15)
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        probe.write(self.args.manifest, self.manifest)

    def test_exact_owned_bytes_all_roles_and_cyclic_whole_rows(self):
        source_bytes = self.args.manifest.read_bytes()
        campaign = probe.prepare(self.args)
        self.assertEqual(source_bytes, self.args.manifest.read_bytes())
        self.assertEqual(source_bytes, (self.args.output / "source-manifest.json").read_bytes())
        for case in campaign["cases"]:
            manifest = probe.read(case["manifest"])
            self.assertNotIn("execution", manifest)
            self.assertEqual(manifest["capturedFrames"], 1)
            for original, moved in zip(self.manifest["frames"][0]["eyes"], manifest["frames"][0]["eyes"]):
                self.assertNotIn("slot", moved)
                self.assertNotIn("nativeLayout", moved)
                self.assertEqual(original["motionVectorScale"], moved["motionVectorScale"])
                for role in probe.ROLES:
                    a = probe.load_texture(original[role], self.args.manifest.parent)
                    b = probe.load_texture(moved[role], Path(case["manifest"]).parent)
                    self.assertEqual(probe.crop_bytes(a, self.args.roi), probe.crop_bytes(b, case["rects"][0]))
                    restored = probe.roll(b, (b.width - case["dx"]) % b.width)
                    self.assertEqual(a.data, restored.data)
        with self.assertRaises(FileExistsError):
            probe.prepare(self.args)

    def test_source_formats_dimensions_and_metadata_fail_closed(self):
        original = copy.deepcopy(self.manifest)
        for mutate in (
            lambda m: m["frames"][0]["eyes"][0].update(featureUpscaling=True),
            lambda m: m["frames"][0]["eyes"][0]["color"].update(format=10),
            lambda m: m["frames"][0]["eyes"][0]["motion"].update(width=127),
            lambda m: m["frames"][0]["eyes"][0].update(motionVectorScale=[0, 1]),
            lambda m: m["frames"][0]["eyes"][0]["outputSubrect"].update(width=64),
            lambda m: m["frames"][0]["eyes"][0]["color"].update(sha256="0" * 64),
            lambda m: m["frames"][0]["tuning"].update(useAutoMask=False),
        ):
            self.manifest = copy.deepcopy(original)
            mutate(self.manifest)
            self.save()
            with self.assertRaises(ValueError):
                probe.prepare(self.args)
            self.assertFalse(self.args.output.exists())

    def test_bounded_distinct_shifts_and_no_wrapped_owned_roi(self):
        for shifts in ("1,0", "0,0", "0,-1", "0,257", "0,64", "0"):
            self.args.shifts = shifts
            with self.subTest(shifts=shifts), self.assertRaises(ValueError):
                probe.prepare(self.args)
            self.assertFalse(self.args.output.exists())

    def execute_mocked(self, campaign, changed=False, fail=False, warmup=1):
        def result(case, *args):
            if fail:
                raise ValueError("native evidence rejected")
            pixel = bytes([11 if changed and case["dx"] else 10, 20, 30, 255])
            return {"resultSha256": "a" * 64, "checked": {"warmupSamples": warmup},
                    "steady": [{"iteration": i + 1, "nativeGpuMicroseconds": 100,
                                "crops": {(0, 0): pixel * 4096, (1, 0): pixel * 4096}}
                               for i in range(2)]}
        with patch.object(probe, "require_idle_game"), patch.object(probe, "invoke") as invoke, \
                patch.object(probe, "_repeat", side_effect=result):
            summary = probe.execute(campaign, self.args.output)
            return summary, invoke.call_args_list

    def test_isolated_serial_runs_reverse_and_preserve_same_owned_key(self):
        campaign = probe.prepare(self.args)
        result, calls = self.execute_mocked(campaign, changed=True)
        self.assertEqual(len(calls), 8)
        self.assertEqual([r["id"] for r in result["cases"]],
                         ["dx-0", "dx-1", "dx-16", "dx-32", "dx-32", "dx-16", "dx-1", "dx-0"])
        self.assertTrue(result["baselineRepeatable"])
        self.assertFalse(result["strictRgbEquivalent"])
        self.assertFalse(result["productionQualified"])
        for case in result["cases"]:
            for comparison in case["comparisons"]:
                self.assertEqual(comparison["ownedSourceRect"], self.args.roi)
        with self.assertRaisesRegex(ValueError, "existing run"):
            probe.execute(campaign, self.args.output)

    def test_native_failure_stops_and_retains_partial_receipt(self):
        campaign = probe.prepare(self.args)
        with self.assertRaisesRegex(ValueError, "native evidence rejected"):
            self.execute_mocked(campaign, fail=True)
        self.assertEqual(len(probe.read(self.args.output / "run.json")["jobs"]), 1)
        self.assertEqual(probe.read(self.args.output / "run.json")["jobs"][0]["status"], "failed")
        self.assertEqual(probe.read(self.args.output / "summary.json")["status"], "failed")

    def test_changed_executable_is_rejected_before_launch(self):
        campaign = probe.prepare(self.args)
        self.executable.write_bytes(b"changed identity")
        with patch.object(probe, "require_idle_game"), patch.object(probe, "invoke") as invoke:
            with self.assertRaisesRegex(ValueError, "identity changed"):
                probe.execute(campaign, self.args.output)
            invoke.assert_not_called()

    def test_campaign_structure_is_readmitted_before_launch(self):
        original = probe.prepare(self.args)
        mutations = (
            lambda c: c.update(schema="unknown"),
            lambda c: c.update(repeats=0),
            lambda c: c.update(samples=65),
            lambda c: c.update(warmup=True),
            lambda c: c.update(seconds=601),
            lambda c: c.update(cases=c["cases"][:1]),
            lambda c: c["cases"][1].update(id="../unowned"),
            lambda c: c["cases"][1].update(kind="atlas"),
            lambda c: c["cases"][1].update(dx=257),
            lambda c: c["cases"][1].update(dx=0),
            lambda c: c["cases"][1].update(rects=[[16, 16, 64, 64]]),
            lambda c: c["toolSources"].pop("packed_input.py"),
            lambda c: c["toolSources"].update(unrecognized="a" * 64),
            lambda c: c.update(ownedSourceRect=[0, 0, 1, 1]),
        )
        for index, mutate in enumerate(mutations):
            campaign = copy.deepcopy(original)
            mutate(campaign)
            probe.write(self.args.output / "campaign.json", campaign)
            with self.subTest(index=index), patch.object(probe, "invoke") as invoke:
                with self.assertRaises(ValueError):
                    probe.execute(campaign, self.args.output)
                invoke.assert_not_called()

    def test_different_native_warmup_count_fails_after_one_launch(self):
        campaign = probe.prepare(self.args)
        with self.assertRaisesRegex(ValueError, "sample/warmup count"):
            self.execute_mocked(campaign, warmup=2)
        journal = probe.read(self.args.output / "run.json")
        self.assertEqual(len(journal["jobs"]), 1)
        self.assertEqual(journal["jobs"][0]["status"], "failed")


if __name__ == "__main__":
    unittest.main()
