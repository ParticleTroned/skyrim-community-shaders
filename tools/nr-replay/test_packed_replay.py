"""Exercise campaign admission and stop behavior without invoking a GPU provider."""

import argparse
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import packed_replay as replay


class PackedReplay(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nr-packed-controller-")
        self.root = Path(self.temporary.name)
        self.path = self.root / "campaign.json"
        self.source = self.root / "source.json"
        self.executable = self.root / "replay.exe"
        self.runtime = self.root / "runtime.dll"
        self.executable.write_bytes(b"never executed")
        self.runtime.write_bytes(b"never loaded")
        replay.write(self.source, {"runtime": {"sha256": replay.digest(self.runtime).upper()}})
        self.campaign = {
            "schema": "csx-nr-packed-campaign-v1", "status": "prepared",
            "sourceManifest": {"path": str(self.source), "sha256": replay.digest(self.source)},
            "replayExecutable": {"path": str(self.executable), "sha256": replay.digest(self.executable)},
            "toolSources": {name: replay.digest(Path(replay.__file__).parent / name) for name in replay.TOOL_SOURCES},
            "repeats": 1, "cases": [
                {"id": name, "kind": name, "manifest": str(self.source),
                 "manifestSha256": replay.digest(self.source), "rects": [[0, 0, 64, 64]],
                 "resultDirectory": "runs/" + name}
                for name in ("separate", "enclosing")],
        }
        self.args = SimpleNamespace(campaign=self.path, command="run", runtime=self.runtime,
                                    samples=1, warmup=1, seconds=10)
        self.report = Mock(return_value={"scope": "test_only"})

    def tearDown(self):
        self.temporary.cleanup()

    def save(self):
        replay.write(self.path, self.campaign)

    def execute_campaign(self, result=None):
        self.save()

        def complete(arguments, output, seconds):
            output.mkdir(parents=True)
            replay.write(output / "results.json", result or {
                "status": "complete", "sessionClosed": True,
                "cases": [{"status": "complete", "samples": [{"success": True}]}],
            })

        with patch.dict(sys.modules, {"packed_report": SimpleNamespace(summarize=self.report)}), \
                patch.object(replay, "require_idle_game"), \
                patch.object(replay, "invoke", side_effect=complete) as invoke:
            replay.execute_campaign(self.args)
            return invoke.call_args_list

    def test_rectangle_parser_rejects_unbounded_or_tiny_work(self):
        self.assertEqual(replay.rectangle("1,2,64,128"), [1, 2, 64, 128])
        for text in ("0,0,31,128", "-1,0,64,64", "0,0,64", "0,0,64,64,2", "a,0,64,64", "0,0,16385,64"):
            with self.subTest(text=text), self.assertRaises(argparse.ArgumentTypeError):
                replay.rectangle(text)

    def test_source_executable_case_and_tool_identity_are_required(self):
        self.save()
        self.assertEqual(replay.load_campaign(self.path)["status"], "prepared")
        for key in ("sourceManifest", "replayExecutable"):
            original = self.campaign[key]["sha256"]
            self.campaign[key]["sha256"] = "0" * 64
            self.save()
            with self.assertRaisesRegex(ValueError, "identity changed"):
                replay.load_campaign(self.path)
            self.campaign[key]["sha256"] = original
        self.campaign["cases"][0]["manifestSha256"] = "0" * 64
        self.save()
        with self.assertRaisesRegex(ValueError, "manifest changed"):
            replay.load_campaign(self.path)
        self.campaign["cases"][0]["manifestSha256"] = replay.digest(self.source)
        self.campaign["toolSources"]["packed_replay.py"] = "0" * 64
        self.save()
        with self.assertRaisesRegex(ValueError, "tool source changed"):
            replay.load_campaign(self.path)

    def test_failed_preparation_and_escaping_outputs_are_rejected(self):
        self.campaign["status"] = "failed_preparation"
        self.save()
        with self.assertRaisesRegex(ValueError, "prepared"):
            replay.load_campaign(self.path)
        self.campaign["status"] = "prepared"
        self.campaign["cases"][0]["resultDirectory"] = "../outside"
        self.save()
        with self.assertRaisesRegex(ValueError, "escapes"):
            replay.load_campaign(self.path)

    def test_missing_tool_source_identity_is_rejected(self):
        del self.campaign["toolSources"]["packed_input.py"]
        self.save()
        with self.assertRaisesRegex(ValueError, "identity set"):
            replay.load_campaign(self.path)

    def test_mutated_campaign_bounds_and_case_identity_fail_closed(self):
        original = copy.deepcopy(self.campaign)
        for value in (0, 6, True, 1.5):
            self.campaign = copy.deepcopy(original)
            self.campaign["repeats"] = value
            self.save()
            with self.subTest(repeats=value), self.assertRaisesRegex(ValueError, "bounds"):
                replay.load_campaign(self.path)
        for field, value, reason in (
            ("id", "../outside", "identity"),
            ("id", "enclosing", "identity|kind"),
            ("kind", "atlas", "kind"),
            ("rects", [[0, 0, 1, 1]], "rectangles"),
            ("rects", [[0, 0, True, 128]], "rectangles"),
            ("rects", [[0, 0, 128, 128]] * 5, "rectangles"),
            ("resultDirectory", "runs/enclosing", "duplicated"),
        ):
            self.campaign = copy.deepcopy(original)
            self.campaign["cases"][0][field] = value
            self.save()
            with self.subTest(field=field, value=value), self.assertRaisesRegex(ValueError, reason):
                replay.load_campaign(self.path)
        self.campaign = copy.deepcopy(original)
        self.campaign["cases"] = []
        self.save()
        with self.assertRaisesRegex(ValueError, "bounds"):
            replay.load_campaign(self.path)

    def test_process_inventory_rejects_game_or_another_replay(self):
        for name in ("SkyrimVR.exe", "SkyrimSE.exe", "csx_nr_replay.exe"):
            with self.subTest(name=name), patch.object(subprocess, "run", return_value=SimpleNamespace(stdout=f'"{name}","1"\n')):
                with self.assertRaisesRegex(RuntimeError, "running"):
                    replay.require_idle_game()
        with patch.object(subprocess, "run", return_value=SimpleNamespace(stdout='"python.exe","1"\n')):
            replay.require_idle_game()
        with patch.object(subprocess, "run", return_value=SimpleNamespace(stdout="ERROR: unavailable")):
            with self.assertRaisesRegex(RuntimeError, "inventory"):
                replay.require_idle_game()

    def test_separate_cases_can_capture_into_distinct_journals(self):
        self.args.command = "capture"
        self.args.renderdoc = self.executable
        for name in ("separate", "enclosing"):
            self.args.case = name
            self.assertEqual(len(self.execute_campaign()), 1)
            self.assertEqual(replay.read(self.root / f"capture-{name}.json")["status"], "complete")
        self.report.assert_not_called()

    def test_failed_native_sample_stops_before_next_job_and_retains_reason(self):
        self.save()
        result = {"status": "complete", "sessionClosed": True,
                  "cases": [{"status": "complete", "samples": [{"success": False}]}]}
        with self.assertRaisesRegex(RuntimeError, "failed admission"):
            self.execute_campaign(result)
        journal = replay.read(self.root / "run.json")
        self.assertEqual(journal["status"], "failed")
        self.assertEqual(len(journal["jobs"]), 1)
        self.assertEqual(journal["jobs"][0]["status"], "failed")
        self.assertEqual(journal["jobs"][0]["reason"], journal["reason"])
        self.assertFalse((self.root / "runs/enclosing").exists())
        self.assertIn("failed admission", journal["reason"])
        self.report.assert_called_once()

    def test_clean_complete_runs_reverse_order_and_refuse_overwrite(self):
        self.campaign["repeats"] = 2
        calls = self.execute_campaign()
        journal = replay.read(self.root / "run.json")
        self.assertEqual(journal["status"], "complete")
        self.assertEqual([job["case"] for job in journal["jobs"]], ["separate", "enclosing", "enclosing", "separate"])
        self.assertEqual(len(calls), 4)
        before = (self.root / "run.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "Existing run"):
            self.execute_campaign()
        self.assertEqual((self.root / "run.json").read_bytes(), before)

    def test_invoke_preserves_logs_when_process_times_out(self):
        output = self.root / "timeout-case"
        with patch.object(subprocess, "run", side_effect=subprocess.TimeoutExpired("fake", 61)):
            with self.assertRaises(subprocess.TimeoutExpired):
                replay.invoke(["not-executed"], output, 1)
        self.assertTrue(output.with_suffix(".stdout.txt").is_file())
        self.assertTrue(output.with_suffix(".stderr.txt").is_file())
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
