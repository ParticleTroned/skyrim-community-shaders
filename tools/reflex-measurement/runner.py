"""Run a data-driven campaign through the maintained DevBench controller only."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

from evidence import EvidenceError, finite, functional_comparisons, mode_comparisons, observer_comparisons, require, summarize, validate

TOOL = "communityshaders.reflex_measurement"


def qpc_now():
    """Use the same host-wide Windows QPC domain as the loaded collector."""
    import ctypes
    require(hasattr(ctypes, "WinDLL"), "this scenario requires the local Windows game host")
    counter = ctypes.c_longlong()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    require(kernel.QueryPerformanceCounter(ctypes.byref(counter)) and counter.value > 0, "client QPC unavailable")
    return counter.value


def plan(config):
    require(config["schema"] == 1, "unsupported scenario schema")
    caps = config["capsFps"]
    require(len(caps) <= 8 and len(set(caps)) == len(caps) and all(finite(n) and 20 <= n <= 240 for n in caps), "distinct comparison caps in 20..240 required")
    require(config["settleSeconds"] >= 5, "settling must last at least five seconds")
    require(100 <= config["functionalMs"] <= 300000 and 100 <= config["comparisonMs"] <= 300000, "invalid phase duration")
    require(type(config["repeats"]) is int and 1 <= config["repeats"] <= 20, "invalid repeat count")
    require(20 <= config["functionalCapFps"] <= 240, "invalid functional cap")
    require(config["modes"] and len(set(config["modes"])) == len(config["modes"]) and set(config["modes"]) <= {"off", "on", "boost"}, "select distinct Off/On/Boost modes")
    for field, minimum, maximum in (("capacity", 16, 262144), ("admissionDelayMs", 1000, 30000), ("queryPeriodMs", 100, 10000)):
        require(type(config[field]) is int and minimum <= config[field] <= maximum, f"invalid {field}")
    require(finite(config.get("observerRelativeThreshold", 0.05)) and 0 < config.get("observerRelativeThreshold", 0.05) <= 1, "invalid observer relative threshold")
    require(finite(config.get("observerAbsoluteThresholdMs", 0.2)) and 0 < config.get("observerAbsoluteThresholdMs", 0.2) <= 100, "invalid observer absolute threshold")
    phases = [{"name": "readiness", "kind": "readiness", "reflexMode": "on", "capFps": 0,
               "mode": "B", "durationMs": config["functionalMs"], "probe": True},
              {"name": "readback-readiness", "kind": "optional_readiness", "reflexMode": "on", "capFps": 0,
               "mode": "C", "durationMs": config["functionalMs"], "probe": True}]
    for mode in config["modes"]:
        for cap in (0, config["functionalCapFps"]):
            phases.append({"name": f"functional-{mode}-{cap}", "kind": "functional", "reflexMode": mode,
                           "capFps": cap, "mode": "B", "durationMs": config["functionalMs"]})
    for repeat in range(config["repeats"]):
        # Reverse each matched repeat to balance condition order.
        modes = config["modes"] if repeat % 2 == 0 else list(reversed(config["modes"]))
        for observer in (["A", "B", "C"] if repeat % 2 == 0 else ["C", "B", "A"]):
            phases.append({"name": f"observer-{repeat}-{observer}", "kind": "observer", "reflexMode": "on",
                           "capFps": 0, "mode": observer, "durationMs": config["functionalMs"], "repeat": repeat})
        for cap in [0] + caps:
            for mode in modes:
                phases.append({"name": f"comparison-{repeat}-{mode}-{cap}", "kind": "comparison", "reflexMode": mode,
                               "capFps": cap, "mode": "B", "durationMs": config["comparisonMs"], "repeat": repeat})
    return phases


def payload(result, key="result", action=None):
    require(result.get("ok") and result.get("transportOk"), "DevBench transport/guard/semantic failure")
    contents = result["data"]["content"]
    require(len(contents) == 1, "expected one structured tool response")
    content = contents[0]
    if isinstance(content, dict) and content.get("type") == "text":
        content = json.loads(content["text"])
    require(isinstance(content, dict), "expected a structured tool payload")
    require(content.get("ok") is True, f"tool action failed: {content.get('error', 'success not confirmed')}")
    require(action is None or content.get("action") == action, "unexpected tool action response")
    require(key is None or key in content, f"tool response is missing {key}")
    return content if key is None else content[key]


class Campaign:
    def __init__(self, args, config):
        self.args, self.config = args, config
        self.root = Path(args.output).resolve()
        require(not self.root.exists(), f"Output folder already exists: {self.root}. Choose a NEW folder; preserve earlier results.")
        self.root.mkdir(parents=True, exist_ok=False)
        self.client = "reflex-" + uuid.uuid4().hex
        self.session, self.capture = None, None
        self.ordinal = 0
        self.completed, self.failures, self.cleanup = [], [], {}
        self.pending_start = False
        self.prepare_dispatched = False
        self.last_capture_id = 0
        self.current_mode = "B"
        self.readback_ready = False
        self.readback_status = {"status": "NOT_REQUESTED", "reason": "readiness has not run"}
        self.last_started = None

    def save(self, name, value):
        with (self.root / name).open("x", encoding="utf-8") as output:
            json.dump(value, output, indent=2, allow_nan=False)

    def identity_parameters(self):
        return {"ExpectedBuildId": self.args.build_id, "ArtifactPath": self.args.artifact,
                "ExpectedArtifactSha256": self.args.artifact_sha256}

    def controller(self, command, label, arguments=None, tool=TOOL):
        self.ordinal += 1
        prefix = f"{self.ordinal:04}-{label}"
        parameters = {"Command": command, "RuntimePath": self.args.runtime,
                      "EvidenceDirectory": str(self.root / "transport"), "EvidenceLabel": prefix,
                      "MaxTransientRetries": 0, "NoExit": True} | self.identity_parameters()
        # Discovery has no measurement/action outcome; all tool calls retain both guards.
        if command != "list":
            parameters.update(RequireSuccess=True, RequirePerformanceNeutral=True)
        if arguments is not None:
            parameters.update(Tool=tool, ArgumentsJson=json.dumps(arguments, separators=(",", ":")))
        self.save(prefix + "-request.json", {"command": command, "tool": tool if arguments else None, "arguments": arguments})
        self.save(prefix + "-parameters.json", parameters)
        command_line = ["pwsh", "-NoProfile", "-File", str(Path(__file__).with_name("invoke-controller.ps1")),
                        "-Controller", self.args.controller, "-ParametersPath", str(self.root / (prefix + "-parameters.json"))]
        try:
            result = subprocess.run(command_line, capture_output=True, timeout=60, check=False)
        except subprocess.TimeoutExpired as error:
            for suffix, value in (("stdout", error.stdout), ("stderr", error.stderr)):
                (self.root / (prefix + f".{suffix}.txt")).write_bytes(value or b"")
            raise
        # Persist failures too, before parsing or interpreting anything.
        (self.root / (prefix + ".stdout.txt")).write_bytes(result.stdout)
        (self.root / (prefix + ".stderr.txt")).write_bytes(result.stderr)
        require(result.returncode == 0, f"controller exited {result.returncode}; see {prefix}")
        parsed = json.loads(result.stdout.decode("utf-8-sig"))
        self.save(prefix + ".json", parsed)
        require(isinstance(parsed, dict) and parsed.get("ok") is True and parsed.get("transportOk") is True,
                f"DevBench {command} failed; see {prefix}.json: {parsed.get('errors') if isinstance(parsed, dict) else 'invalid envelope'}")
        return parsed

    def call(self, action, **arguments):
        request = {"contractMajor": 1, "schema": 1, "expectedBuildId": self.args.build_id,
                   "clientId": self.client, "action": action}
        if self.session is not None:
            request["sessionId"] = self.session
        request.update(arguments)
        return payload(self.controller("call", action, request))

    def export(self, capture, mode, label):
        pages, offset = [], 0
        while True:
            page = self.call("export", captureId=capture, offset=offset, limit=256)
            self.save(f"{label}-page-{offset:08}.json", page)
            pages.append(page)
            following = page["nextOffset"]
            if following is None:
                break
            require(type(following) is int and following > offset, "non-progressing export pagination")
            offset = following
        validation = validate(pages, mode, self.args.build_id)
        self.save(label + "-validation.json", validation)
        self.call("acknowledge_export", captureId=capture, total=pages[0]["total"])
        return pages, validation

    def settle(self, probe=False):
        deadline = time.monotonic() + 10
        while True:
            condition = self.call("condition")
            if probe or condition["acceptedCondition"] and not condition["frameGenerationConstraint"]:
                break
            require(time.monotonic() < deadline, "native settings were not accepted before settling")
            time.sleep(0.25)
        self.call("set_noon")
        time.sleep(self.config["settleSeconds"])
        condition = self.call("condition")
        require(condition["gameHour"] is not None and 12 <= condition["gameHour"] <= 12.05, "noon verification failed")
        require(probe or condition["acceptedCondition"] and not condition["frameGenerationConstraint"], "condition changed while settling")
        return condition

    def phase(self, phase):
        name, mode = phase["name"], phase["mode"]
        self.save(name + "-requested.json", phase)
        self.call("configure", reflexMode=phase["reflexMode"], capFps=phase["capFps"])
        self.settle(phase.get("probe", False))
        self.pending_start = True
        self.current_mode = mode
        started = self.call("start", mode=mode, durationMs=phase["durationMs"], capacity=self.config["capacity"],
                            admissionDelayMs=self.config["admissionDelayMs"], readback=mode == "C",
                            queryPeriodMs=self.config["queryPeriodMs"], probe=phase.get("probe", False))
        self.capture = started["captureId"]
        self.last_started = started
        self.last_capture_id = self.capture
        self.pending_start = False
        self.save(name + "-start.json", started)
        client_ready_qpc = qpc_now()
        # No tool calls, exports or JSON/file work inside this measured window.
        time.sleep((started["admissionDelayMs"] + phase["durationMs"] + 2250) / 1000)
        self.call("stop", captureId=self.capture, reason="completed")
        pages, validation = self.export(self.capture, mode, name)
        self.capture = None
        require(pages[0]["boundaries"]["applicationAdmissionStartQpc"] is not None and
                pages[0]["boundaries"]["applicationAdmissionStartQpc"] > client_ready_qpc,
                "controller/receipt work overlapped admission; increase admissionDelayMs and retain failed phase")
        try:
            after = self.call("condition")
        except (EvidenceError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            after = {"status": "FAILED", "reason": str(error)}
            validation["limitations"].append("post-window condition could not be read")
            validation["complete"] = False
        summary = {"condition": phase, "validation": validation, "statistics": summarize(pages),
                   "clientReadyQpc": client_ready_qpc, "clientClock": "same-host Windows QueryPerformanceCounter", "after": after}
        self.save(name + "-summary.json", summary)
        self.completed.append(summary)
        if mode != "C" and phase["kind"] != "readiness":
            require(validation["complete"], f"{name}: incomplete sources; full evidence retained")
        return summary

    def restore(self):
        # This also recovers a lost prepare/start response using the unique client owner.
        try:
            recovered = self.call("recover_and_stop", reason="runner_finally")
            self.session = recovered["sessionId"]
            self.cleanup["stop"] = recovered
            recovered_capture = recovered.get("captureId")
            interrupted = self.capture if self.capture is not None else recovered_capture if self.pending_start and recovered_capture and recovered_capture > self.last_capture_id else None
            if recovered.get("released"):
                self.cleanup["exportAlreadyAcknowledged"] = interrupted
                interrupted = None
            if interrupted:
                try:
                    self.export(interrupted, self.current_mode, "interrupted")
                except (EvidenceError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
                    self.cleanup["partialExportFailure"] = str(error)
        except (EvidenceError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            self.cleanup["stopFailure"] = str(error)
        try:
            self.cleanup["request"] = self.call("restore")
            deadline = time.monotonic() + 10
            while True:
                receipt = self.call("restoration")
                self.cleanup["confirmation"] = receipt
                if receipt["restoration"]["acceptedRestored"] or receipt["restoration"]["conflict"] or time.monotonic() >= deadline:
                    break
                time.sleep(0.25)
            require(receipt["restoration"]["requestedRestored"] and receipt["restoration"]["acceptedRestored"] and
                    not receipt["restoration"]["conflict"], "RESTORATION UNCONFIRMED: inspect requested and remaining accepted state")
        except (EvidenceError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            self.cleanup["restorationFailure"] = str(error)
            if "confirmation" not in self.cleanup:
                self.cleanup["remainingRequestedAndAccepted"] = {"status": "UNAVAILABLE", "reason": "no fresh restoration confirmation",
                    "lastKnownCondition": self.last_started.get("condition") if self.last_started else None, "lastKnownIsCurrent": False}
        self.save("restoration.json", self.cleanup)
        if "restorationFailure" in self.cleanup:
            print(json.dumps({"RESTORATION_UNCONFIRMED": self.cleanup}, indent=2), file=sys.stderr)

    def run(self):
        phases = plan(self.config)
        self.save("scenario.json", {"config": self.config, "phases": phases, "clientId": self.client})
        try:
            listing = self.controller("list", "catalog")
            require(listing.get("ok"), "catalog discovery failed")
            tools = listing["data"]["tools"]
            definition = next((tool for tool in tools if tool["name"] == TOOL), None)
            require(definition is not None, "measurement tool not registered in loaded build")
            require("recover_and_stop" in definition["inputSchema"]["properties"]["action"]["enum"], "loaded command schema is incomplete")
            self.prepare_dispatched = True
            prepared = self.call("prepare")
            self.session = prepared["sessionId"]
            self.save("environment.json", prepared)
            for phase in phases:
                if phase["mode"] == "C" and phase["kind"] != "optional_readiness" and not self.readback_ready:
                    self.save(phase["name"] + "-unavailable.json", {"status": self.readback_status["status"], "readinessItem": self.readback_status,
                              "reason": "optional native readback did not pass readiness; passive comparisons remain separate"})
                    continue
                # Binding comparisons require a faster uncapped baseline in the same mode.
                if phase["kind"] == "comparison" and phase["capFps"]:
                    baselines = [p for p in self.completed if p["condition"]["kind"] == "functional" and
                                 p["condition"]["reflexMode"] == phase["reflexMode"] and not p["condition"]["capFps"]]
                    require(baselines, "missing uncapped baseline for selected mode")
                    baseline = baselines[-1]["statistics"]["appIntervalMs"]
                    if not baseline or baseline["p95"] >= 0.95 * 1000 / phase["capFps"]:
                        self.save(phase["name"] + "-not-requested.json", {"status": "NOT_REQUESTED", "reason": "baseline not faster than requested cap", "baseline": baseline})
                        continue
                summary = self.phase(phase)
                if phase["kind"] == "readiness":
                    readiness = self.call("readiness", readback=False, latency=True)
                    self.save("readiness.json", readiness)
                    self.readback_ready = readiness["items"]["readback"]["status"] == "READY"
                    self.readback_status = readiness["items"]["readback"]
                    for name in ("build", "device", "captureGeneration", "nativeFunctions", "nativeExecution", "application", "compositor", "captureQuality", "capacity", "export", "restoration"):
                        require(readiness["items"][name]["status"] == "READY", f"readiness failed: {name}")
                    require(summary["validation"]["paginationComplete"], "readiness export not validated")
                    require(summary["validation"]["complete"], "readiness acquisition or condition validation failed")
                elif phase["kind"] == "optional_readiness":
                    readiness = self.call("readiness", readback=True, latency=True)
                    self.save("readback-readiness.json", readiness)
                    self.readback_status = readiness["items"]["readback"]
                    self.readback_ready = self.readback_status["status"] == "READY" and summary["validation"]["complete"]
                    if self.readback_status["status"] == "READY" and not summary["validation"]["complete"]:
                        self.readback_status = {"status": "FAILED", "reason": "getter returned a valid receipt but queried acquisition was incomplete",
                                                "readinessItem": self.readback_status}
                    if not self.readback_ready and self.readback_status["status"] != "UNSUPPORTED":
                        self.failures.append({"type": "OptionalReadbackFailed", "message": self.readback_status, "captureValidation": summary["validation"]})
                elif phase["mode"] == "C" and not summary["validation"]["complete"]:
                    self.failures.append({"type": "QueriedCaptureFailed", "message": phase["name"], "validation": summary["validation"]})
        except (Exception, KeyboardInterrupt) as error:
            self.failures.append({"type": type(error).__name__, "message": str(error)})
        finally:
            if self.prepare_dispatched:
                self.restore()
            else:
                self.cleanup = {"status": "NOT_REQUESTED", "reason": "preparation was not dispatched; no measurement preference writes"}
                self.save("restoration.json", self.cleanup)
            self.save("campaign.json", {"completed": self.completed, "failures": self.failures, "cleanup": self.cleanup,
                                       "latency": "UNSUPPORTED", "pre12Restoration": "NOT_MEASURED",
                                       "efficacy": "NOT_ASSESSED; compare matched per-condition evidence", "observerCostsSubtracted": False})
            self.save("observer-comparisons.json", observer_comparisons(self.completed, self.config.get("observerRelativeThreshold", 0.05), self.config.get("observerAbsoluteThresholdMs", 0.2)))
            self.save("functional-comparisons.json", functional_comparisons(self.completed))
            self.save("mode-comparisons.json", mode_comparisons(self.completed))
        return 1 if self.failures or "restorationFailure" in self.cleanup else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--output", required=True, help="New evidence directory; existing runs are never overwritten")
    parser.add_argument("--controller", required=True, help="Maintained Invoke-DevBenchControl.ps1 path")
    parser.add_argument("--runtime", required=True, help="Exact running DevBench runtime metadata")
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--artifact", required=True, help="Verified physical deployed DLL")
    parser.add_argument("--artifact-sha256", required=True)
    args = parser.parse_args()
    config = json.loads(Path(args.scenario).read_text(encoding="utf-8"))
    return Campaign(args, config).run()


if __name__ == "__main__":
    sys.exit(main())
