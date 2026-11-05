"""Prove real FXC work resumes after an abrupt worker termination."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "tools/release_worker.py"


@unittest.skipUnless(os.environ.get("CSX_TEST_FXC") and sys.platform == "win32", "Set CSX_TEST_FXC to run real Windows FXC recovery")
class CompilerResumeTests(unittest.TestCase):
    def test_interrupted_real_compile_resumes_and_changed_sources_miss(self):
        fxc = os.environ["CSX_TEST_FXC"]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspace = root / "workspace"
            stage = workspace / "Shaders"
            stage.mkdir(parents=True)
            (root / "core").mkdir()
            shader = stage / "CheckpointTest.hlsl"
            shader.write_text("float4 main() : SV_Target { return float4(ENTRY_NUM / 255.0, 0, 0, 1); }")
            config = workspace / "inputs.yaml"
            count = 48
            config.write_text(yaml.safe_dump({"shaders": [{"file": shader.name, "configs": {
                "PSHADER": {"common_defines": ["PSHADER="], "entries": [
                    {"entry": f"main:{index:X}", "defines": [f"ENTRY_NUM={index}"]}
                    for index in range(1, count + 1)]}}}]}))
            worker_config = {"repository": str(ROOT), "state": str(root / "state"),
                             "taskRoot": str(root), "workspace": str(workspace), "output": str(root / "output"),
                             "core": str(root / "core"), "fxc": fxc, "jobs": 1,
                             "sourceCommit": "a" * 40, "pluginVersion": "CSX 1.2.3-VR"}
            config_path = root / "worker.json"
            config_path.write_text(json.dumps(worker_config))
            arguments = ["--fxc", fxc, "--shader-dir", str(stage), "--output-dir", str(workspace / "cache"),
                         "--config", str(config), "--optimization-level", "3", "--jobs", "1", "--max-warnings", "0"]
            child_code = (
                "import sys; from release_worker import own_process_job,compile_mode; "
                f"job=own_process_job(); sys.argv=['compiler',*{arguments!r}]; "
                f"raise SystemExit(compile_mode({worker_config!r}))"
            )
            with (root / "interrupted.log").open("w") as log:
                process = subprocess.Popen([sys.executable, "-u", "-B", "-c", child_code],
                                           cwd=WORKER.parent, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        if len(list((root / "state/objects").glob("*.json"))) >= 3:
                            break
                        if process.poll() is not None:
                            self.fail((root / "interrupted.log").read_text())
                        time.sleep(0.01)
                    process.kill()
                    process.wait(timeout=5)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
            completed = list((root / "state/objects").glob("*.json"))
            self.assertGreaterEqual(len(completed), 3)
            self.assertLess(len(completed), count)
            old_records = {path.name: path.read_bytes() for path in completed}
            environment = {**os.environ, "CSX_RELEASE_WORKER_CONFIG": str(config_path)}
            result = subprocess.run([sys.executable, "-u", "-B", str(WORKER), "compile", *arguments],
                                    cwd=WORKER.parent, env=environment, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            counters = json.loads(re.search(r"Durable checkpoint results: (.*)", result.stdout)[1])
            self.assertEqual(counters["reused"], len(completed))
            self.assertEqual(counters["compiled"], count - len(completed))
            for path in completed:
                self.assertEqual(path.read_bytes(), old_records[path.name])
            warm = subprocess.run([sys.executable, "-u", "-B", str(WORKER), "compile", *arguments],
                                  env=environment, capture_output=True, text=True, timeout=30)
            self.assertEqual(warm.returncode, 0, warm.stdout + warm.stderr)
            counters = json.loads(re.search(r"Durable checkpoint results: (.*)", warm.stdout)[1])
            self.assertEqual(counters, {"reused": count, "compiled": 0})
            for path in list((root / "state/objects").glob("*.json"))[:4]:
                path.unlink()
            orphaned = subprocess.run([sys.executable, "-u", "-B", str(WORKER), "compile", *arguments],
                                      env=environment, capture_output=True, text=True, timeout=30)
            self.assertEqual(orphaned.returncode, 0, orphaned.stdout + orphaned.stderr)
            counters = json.loads(re.search(r"Durable checkpoint results: (.*)", orphaned.stdout)[1])
            self.assertEqual(counters, {"reused": count - 4, "compiled": 4})
            shader.write_text(shader.read_text().replace("0, 0, 1", "1, 0, 1"))
            changed = subprocess.run([sys.executable, "-u", "-B", str(WORKER), "compile", *arguments],
                                     cwd=WORKER.parent, env=environment, capture_output=True, text=True, timeout=30)
            self.assertEqual(changed.returncode, 0, changed.stdout + changed.stderr)
            counters = json.loads(re.search(r"Durable checkpoint results: (.*)", changed.stdout)[1])
            self.assertEqual(counters["reused"], 0)
            self.assertEqual(counters["compiled"], count)
            print(f"Real FXC interruption recovered {len(completed)} checkpoints; source change recompiled all {count}.")


if __name__ == "__main__":
    unittest.main()
