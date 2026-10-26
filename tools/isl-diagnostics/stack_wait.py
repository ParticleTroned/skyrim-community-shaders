"""Coordinate the maintained owned WPR worker; never decode traces on the tester."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from stack_evidence import closed_trace, recording_ready


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def control(action, recorder, wpr=None, directory=None):
    command = ["pwsh", "-NoProfile", "-File", str(Path(__file__).with_name("trace-control.ps1")),
               "-Action", action, "-RecorderDirectory", str(recorder)]
    if wpr:
        command += ["-WprPath", str(wpr)]
    if directory:
        command += ["-Directory", str(directory), "-OwnerProcessId", str(os.getpid())]
    # The detached worker can inherit pipe handles and prevent communicate() from seeing EOF.
    # File-backed output keeps the launcher deadline independent of the worker's lifetime.
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        try:
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                    timeout=30, check=False)
        finally:
            stdout.seek(0)
            stderr.seek(0)
            out, err = stdout.read(), stderr.read()
            if directory and Path(directory).exists():
                (Path(directory) / "launcher.stdout.txt").write_bytes(out)
                (Path(directory) / "launcher.stderr.txt").write_bytes(err)
    if result.returncode:
        raise RuntimeError(err.decode("utf-8-sig", errors="replace").strip() or
                           f"Trace controller exited {result.returncode}")
    return json.loads(out.decode("utf-8-sig"))


class OwnedTrace:
    def __init__(self, root, recorder, wpr):
        self.directory = Path(root) / "stack-wait"
        self.recorder, self.wpr = recorder, wpr
        self.requested = False
        self.stop_requested = False
        self.ready = None

    def start(self):
        self.requested = True
        print("Starting Windows CPU stack and scheduling recording...", flush=True)
        control("start", self.recorder, self.wpr, self.directory)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            self.ensure_active()
            ready = self.directory / "trace-ready.json"
            if ready.exists():
                self.ready = read_json(ready)
                if not recording_ready(self.ready):
                    raise RuntimeError("Windows trace readiness receipt is incomplete; preserve the output")
                return
            time.sleep(.1)
        raise RuntimeError("Windows recorder did not become ready within 20 seconds; preserve the output")

    def ensure_active(self):
        result = self.directory / "trace-result.json"
        if result.exists():
            value = read_json(result)
            raise RuntimeError(f"Windows trace stopped before the comparisons finished: {value.get('state')}; "
                               f"{value.get('stopReason')}; {value.get('errors')}")

    def finish(self, timeout=120):
        if not self.requested:
            return {"state": "not_started", "filesSaved": False}
        if not self.directory.exists():
            return {"state": "start_failed", "filesSaved": False, "reason": "trace directory was not created"}
        (self.directory / "stop-trace").touch(exist_ok=True)
        self.stop_requested = True
        print("RECORDING STOP REQUESTED. Saving Windows trace; keep this terminal open...", flush=True)
        deadline, next_notice = time.monotonic() + timeout, time.monotonic() + 5
        path = self.directory / "trace-result.json"
        while not path.exists() and time.monotonic() < deadline:
            if time.monotonic() >= next_notice:
                print("Still saving the Windows trace. No analysis is running on your computer.", flush=True)
                next_notice = time.monotonic() + 5
            time.sleep(.2)
        if not path.exists():
            return {"state": "pending", "filesSaved": False,
                    "stopRequested": self.stop_requested,
                    "reason": f"trace finalization exceeded {timeout:g} seconds; owned worker retained; do not rerun",
                    "receipt": str(path)}
        result = read_json(path)
        etl = self.directory / "cpu-stack-wait.etl"
        saved = closed_trace(self.ready, result) and etl.is_file() and etl.stat().st_size > 0
        return {"state": result.get("state"), "filesSaved": bool(saved), "trace": result,
                "stopRequested": self.stop_requested,
                "bytes": etl.stat().st_size if etl.is_file() else 0,
                "analysis": "pending offline: event loss, per-window coverage, stacks and symbols"}
