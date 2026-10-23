"""Run ranked ISL subtraction captures with one shared, owned DevBench collector."""

from __future__ import annotations

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import subprocess
import time
import uuid

HERE = Path(__file__).resolve().parent
REFLEX = HERE.parent / "reflex-measurement"
sys.path.insert(0, str(REFLEX))
_spec = importlib.util.spec_from_file_location("csx_reflex_runner", REFLEX / "runner.py")
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)
_spec = importlib.util.spec_from_file_location("csx_isl_evidence", HERE / "evidence.py")
evidence = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(evidence)
require = evidence.require
ISL_TOOL = "communityshaders.isl_diagnostics"
NR_TOOL = "communityshaders.neural_rendering"
NORMAL = {"luminanceMode": "normal", "rendererMode": "normal", "particleDetectionMode": "normal",
          "editorOverrideMode": "normal", "observationLevel": "counters"}
PLANS = {
    "core": ["R", "L", "L", "R", "V", "V", "R"],
    "luminance-cost": ["R", "C", "C", "R", "L", "L", "R"],
    "both": ["R", "B", "B", "R", "L", "V", "R"],
    "particle": ["R", "S", "S", "R", "P", "P", "R"],
    "shadow": ["R", "O", "O", "R", "O", "O", "R"],
    "editor": ["R", "E", "E", "R", "E", "E", "R"],
    "conversion": ["R", "D", "D", "R", "D", "D", "R"],
    "observer": ["R", "X", "X", "R", "T", "T", "R"],
    "prune": ["R", "Q", "Q", "R", "Q", "Q", "R"],
    "mountains": ["R", "R", "R"],
    "batch": ["R", "R", "R"],
}


def plan(config, selected="core"):
    require(config["schema"] == 1 and selected in PLANS, "invalid schema or plan")
    require(5 <= config["settleSeconds"] <= 60, "settle must be 5..60 seconds")
    require(type(config["durationMs"]) is int and 1000 <= config["durationMs"] <= 300000, "invalid duration")
    require(type(config["capacity"]) is int and 16 <= config["capacity"] <= 262144, "invalid capacity")
    require(type(config["admissionDelayMs"]) is int and 1000 <= config["admissionDelayMs"] <= 30000, "invalid admission delay")
    require(config["sceneTime"] == "preserve" and config["pacing"] in ("preserve", "off_unlimited"), "explicit pacing and scene policy required")
    require(config["observationLevel"] in ("off", "counters", "timed"), "invalid observer")
    sequence = config["sequence"] if selected == "core" else PLANS[selected]
    require(sequence == PLANS["core"] if selected == "core" else True, "core order must remain R L L R V V R")
    result = []
    for index, label in enumerate(sequence):
        changes = dict(config["conditions"].get(label, {}))
        if selected == "conversion" and label == "R":
            changes = dict(config["conditions"]["U"])
        if selected == "mountains":
            changes["islEnabled"] = False
        gates = NORMAL | {"observationLevel": config["observationLevel"]} | {k: v for k, v in changes.items() if k in NORMAL}
        if label == "X":
            gates["observationLevel"] = "off"
        if label == "T":
            gates["observationLevel"] = "timed"
        result.append({"name": f"{index + 1:02}-{label}", "label": label, "gates": gates,
                       "shadowCulling": changes.get("shadowCulling"), "islEnabled": changes.get("islEnabled", True),
                       "pruneFrames": 120 if label == "Q" else None})
    return result


class Campaign(base.Campaign):
    def __init__(self, args, config):
        super().__init__(args, config)
        self.args.build_id = None
        self.client = "isl-" + uuid.uuid4().hex
        self.rules = None
        self.original = None
        self.raw_unacked = None
        self.side_unacked = None
        self.raw_pages = []
        self.side_pages = []
        self.last_transport = None
        self.scene_reference = None
        self.nr_setup = {"state": "not_checked", "disableAuthorized": args.disable_neural_rendering,
                         "policy": "explicit NR-off setup is retained; capture settings are restored separately"}

    def controller(self, *args, **kwargs):
        result = super().controller(*args, **kwargs)
        self.last_transport = result
        return result

    def identity_parameters(self):
        # Capture ownership and live API guards do not require an on-disk DLL audit.
        return {"SkipRuntimeIdentityVerification": True}

    def performance_window(self, before, after, name):
        self.save(name + "-guard-before.json", before)
        self.save(name + "-guard-after.json", after)
        command = ["pwsh", "-NoProfile", "-File", str(HERE / "check-window.ps1"),
                   "-ModulePath", str(Path(self.args.controller).resolve().parent / "DevBenchControl.psm1"),
                   "-BeforePath", str(self.root / (name + "-guard-before.json")),
                   "-AfterPath", str(self.root / (name + "-guard-after.json"))]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
        require(result.returncode == 0, "performance-window qualification failed: " + result.stderr)
        return json.loads(result.stdout)

    def isl(self, action, **arguments):
        request = {"contractMajor": 1, "schema": 1, "expectedBuildId": self.args.build_id, "clientId": self.client,
                   "commandId": uuid.uuid4().hex, "sessionId": self.session, "action": action} | arguments
        return base.payload(self.controller("call", "isl-" + action, request, tool=ISL_TOOL))

    def nr(self, action, label, **arguments):
        require(self.args.build_id is not None or action == "nr_status", "read NR status before changing settings")
        request = {"action": action} | arguments
        if self.args.build_id is not None:
            request["expectedBuildId"] = self.args.build_id
        response = base.payload(self.controller("call", label, request, tool=NR_TOOL), None, action)
        if self.args.build_id is None:
            build_id = response.get("producer", {}).get("buildId")
            require(isinstance(build_id, str) and bool(build_id.strip()), "running diagnostic API did not expose its build ID")
            self.args.build_id = build_id
            self.save("connection.json", {"runtime": self.args.runtime, "producer": response["producer"],
                      "policy": "live API discovery; no artifact, manifest or external build expectation"})
        require("neuralRendering" in response, "tool response is missing neuralRendering")
        status = response["neuralRendering"]
        self.save(label + "-status.json", status)
        evidence.nr_preference(status)
        evidence.nr_counters(status)
        return status

    def prepare_nr(self):
        original = self.nr("nr_status", "nr-original")
        enabled = evidence.nr_preference(original)
        self.nr_setup.update(state="checked", originalRequested=enabled)
        if enabled:
            self.nr_setup["state"] = "requires_explicit_disable"
            require(self.args.disable_neural_rendering,
                    "NR's stored enable preference is ON; this does not prove NR is rendering. "
                    "The UI may show unavailable/off. See nr-original-status.json. "
                    "Rerun with -DisableNeuralRendering to explicitly set NR OFF and leave it off for this test.")
            print("Setting Neural Rendering OFF for this test; it will remain OFF afterward.", flush=True)
            self.nr_setup["state"] = "disable_dispatched_outcome_unconfirmed"
            self.save("nr-disable-intent.json", self.nr_setup)
            changed = self.nr("nr_configure", "nr-disable", enabled=False,
                              expectedConfigurationFingerprint=original["requestedConfigurationFingerprint"])
            self.nr_setup["state"] = "disable_acknowledged"
            expected = copy.deepcopy(original["requestedConfiguration"])
            expected["upscaling"]["neuralRenderingEnabled"] = False
            require(changed["requestedConfiguration"] == expected,
                    "NR-off setup changed additional configuration; stop and inspect nr-original/disable-status.json")
            verified = self.nr("nr_status", "nr-disabled-verified")
            require(verified["requestedConfiguration"] == expected,
                    "configuration changed during NR-off verification; no capture started")
        else:
            verified = original
        require(evidence.nr_preference(verified) is False, "NR-off setup was not verified; no capture started")
        self.nr_setup.update(state="off_verified", requestedAfter=False,
                             changedBySetup=enabled, fingerprint=verified["requestedConfigurationFingerprint"])
        self.save("nr-setup.json", self.nr_setup)
        print("Neural Rendering OFF verified. Detailed NR state will be saved around every window.", flush=True)

    def nr_condition(self, name, condition):
        status = self.nr("nr_status", name)
        require(evidence.nr_preference(status) is False and condition.get("nrRequested") is False,
                "NR-off condition changed or disagrees between APIs; see saved NR and ISL condition receipts")
        return status

    def collect(self, capture, side, label):
        pages, offset = [], 0
        call = self.isl if side else self.call
        while True:
            page = call("export", captureId=capture, offset=offset, limit=256)
            self.save(f"{label}-{'isl' if side else 'raw'}-page-{offset:08}.json", page)
            pages.append(page)
            if side:
                self.side_pages = pages
            else:
                self.raw_pages = pages
            following = page["nextOffset"]
            if following is None:
                break
            require(type(following) is int and following > offset, "non-progressing export")
            offset = following
        return pages

    def export_pair(self, capture, label):
        self.raw_pages = self.collect(capture, False, label)
        raw_validation = evidence.raw.validate(self.raw_pages, "B", self.args.build_id)
        self.side_pages = self.collect(capture, True, label)
        side_validation = evidence.sidecar(self.side_pages, self.raw_pages, self.args.build_id)
        # A complete export can be acknowledged even when its recorded quality is bad.
        self.save(label + "-validation.json", {"raw": raw_validation, "isl": side_validation})
        self.raw_unacked = self.raw_pages[0]["total"]
        self.side_unacked = self.side_pages[0]["total"]
        self.call("acknowledge_export", captureId=capture, total=self.raw_unacked)
        self.raw_unacked = None
        released = self.isl("acknowledge_export", captureId=capture, total=self.side_unacked)
        self.side_unacked = None
        require(released["released"], "joint capture not released after both acknowledgements")
        return raw_validation, side_validation

    def settle_isl(self):
        deadline = time.monotonic() + 30
        while True:
            condition = self.isl("condition")
            if condition["ready"]:
                break
            require(time.monotonic() < deadline, "ISL condition never became ready")
            time.sleep(0.25)
        time.sleep(self.config["settleSeconds"])
        condition = self.isl("condition")
        require(condition["ready"], "condition changed while settling")
        return condition

    def phase(self, phase):
        name = phase["name"]
        self.save(name + "-requested.json", phase)
        self.isl("configure", gates=phase["gates"], islEnabled=phase["islEnabled"],
                 shadowCulling=phase["shadowCulling"] or self.original["shadowCulling"]["settings"])
        before = self.settle_isl()
        nr_before = self.nr_condition(name + "-nr-before", before)
        if phase["pruneFrames"]:
            self.isl("arm_prune_deferral", frames=phase["pruneFrames"])
        print(f"Capturing {name} ({self.config['durationMs'] / 1000:g}s); keep the same viewpoint.", flush=True)
        self.pending_start = True
        started = self.call("start", mode="B", durationMs=self.config["durationMs"], capacity=self.config["capacity"],
                            admissionDelayMs=self.config["admissionDelayMs"], readback=False)
        transport_start = self.last_transport
        self.capture = started["captureId"]
        self.last_started = started
        self.last_capture_id = self.capture
        self.pending_start = False
        self.save(name + "-start.json", started)
        client_ready = base.qpc_now()
        # Local bounded sleeps only: no controller, file I/O, screenshot or polling inside admission.
        deadline = time.monotonic() + (started["admissionDelayMs"] + self.config["durationMs"] + 2250) / 1000
        while time.monotonic() < deadline:
            time.sleep(min(30, max(0, deadline - time.monotonic())))
        stopped = self.call("stop", captureId=self.capture, reason="completed")
        transport_stop = self.last_transport
        require(stopped["frozen"], "capture still draining; recovery manifest retains ownership")
        print(f"Exporting and validating {name}; keep the same viewpoint.", flush=True)
        raw_validation, side_validation = self.export_pair(self.capture, name)
        applied = evidence.experiment_applied(phase, side_validation, self.side_pages[0], self.original)
        self.capture = None
        after = self.isl("condition")
        nr_after = self.nr_condition(name + "-nr-after", after)
        nr_window = evidence.nr_window(nr_before, nr_after)
        raw_after = self.call("condition")
        guard = self.performance_window(transport_start, transport_stop, name)
        source = evidence.streams(self.raw_pages, "compositor")
        scene = evidence.scene_match(started["condition"], raw_after, source, self.scene_reference)
        if self.scene_reference is None:
            self.scene_reference = scene["reference"]
        if self.rules is None and phase["label"] == "R":
            try:
                self.rules = evidence.reference_rules(source)
                self.save("frozen-proxy-rules.json", self.rules)
            except evidence.EvidenceError as error:
                self.save("proxy-unavailable.json", {"reason": str(error)})
                self.rules = {"unavailable": str(error)}
        waveform = evidence.waveform(source, self.rules) if self.rules and "unavailable" not in self.rules else {
            "status": "UNAVAILABLE", "classification": "UNAVAILABLE", "reason": "reference could not define a valid proxy"}
        admission = self.raw_pages[0]["boundaries"]["applicationAdmissionStartQpc"]
        controller_clear = evidence.finite(admission) and admission > client_ready
        stable = all(before[k] == after[k] for k in ("pacing", "weather", "cell", "nrRequested", "effective", "shadowBatch"))
        stable &= before["shadowCulling"]["settings"] == after["shadowCulling"]["settings"]
        expected_pacing = self.original["pacing"] if self.config["pacing"] == "preserve" else before["pacing"]
        stable &= before["pacing"] == expected_pacing
        result = {"name": name, "condition": phase, "before": before, "after": after,
                  "validation": {"raw": raw_validation, "isl": side_validation}, "waveform": waveform,
                  "statistics": evidence.raw.summarize(self.raw_pages), "controllerOutsideAdmission": controller_clear,
                  "settingsAndSceneStable": stable, "clientReadyQpc": client_ready,
                  "performanceWindow": guard, "sceneMatch": scene, "experimentApplied": applied, "neuralRendering": nr_window,
                  "valid": raw_validation["complete"] and side_validation["complete"] and controller_clear and stable and guard["valid"] and scene["valid"] and applied['valid'] and nr_window['valid'],
                  "qpcAssociations": evidence.correlate_qpc(self.raw_pages, self.side_pages),
                  "evidencePrefix": name, "sceneTimePolicy": "preserved; elapsed game time is recorded, not reset"}
        self.save(name + "-summary.json", result)
        self.completed.append(result)
        require(result["valid"], f"{name}: incomplete or contaminated capture; evidence preserved, conclusions withheld")

    def restore(self):
        try:
            stop = self.call("recover_and_stop", reason="isl_runner_finally")
            self.session = stop["sessionId"]
            self.cleanup["stop"] = stop
            capture = stop.get("captureId")
            if capture and not stop.get("released"):
                require(stop.get("frozen"), "worker has not drained; retry stop before export/restore")
                # Lost ACK replies are recovered using a fresh export attempt; server refuses reused buffers.
                if self.raw_unacked is not None:
                    self.call("acknowledge_export", captureId=capture, total=self.raw_unacked)
                    self.raw_unacked = None
                if self.side_unacked is not None:
                    released = self.isl("acknowledge_export", captureId=capture, total=self.side_unacked)
                    self.side_unacked = None
                    require(released["released"], "joint recovery acknowledgement did not release the capture")
                    capture = None
                if capture and self.raw_unacked is None and self.side_unacked is None:
                    self.export_pair(capture, "interrupted")
        except (Exception, KeyboardInterrupt) as error:
            self.cleanup["stopOrExportFailure"] = str(error)
        try:
            self.cleanup["restoreRequest"] = self.call("restore")
            deadline = time.monotonic() + 15
            while True:
                raw_receipt = self.call("restoration")
                isl_receipt = self.isl("restore") if "islRestoreRequest" not in self.cleanup else self.isl("condition")
                if "islRestoreRequest" not in self.cleanup:
                    self.cleanup["islRestoreRequest"] = isl_receipt
                self.cleanup["rawRestoration"] = raw_receipt
                self.cleanup["remainingISL"] = isl_receipt
                lease = raw_receipt["restoration"]
                live_receipt = isl_receipt.get("restoration", isl_receipt)
                live = live_receipt.get("liveRestored", False) and not live_receipt.get("conflict", False)
                restored = lease.get("requestedRestored") and (lease.get("acceptedRestored") or lease.get("unchangedReleased")) and not lease.get("conflict")
                if (restored and live) or lease.get("conflict") or time.monotonic() >= deadline:
                    break
                time.sleep(0.25)
            require(restored and live, "RESTORATION UNCONFIRMED; inspect preserved receipts")
        except (Exception, KeyboardInterrupt) as error:
            self.cleanup["restorationFailure"] = str(error)
        self.save("recovery-manifest.json", {"clientId": self.client, "sessionId": self.session, "captureId": self.last_capture_id,
                  "buildId": self.args.build_id, "runtime": self.args.runtime, "rawUnacknowledgedTotal": self.raw_unacked,
                  "sideUnacknowledgedTotal": self.side_unacked, "cleanup": self.cleanup,
                  "recovery": "same owner: recover_and_stop; wait for frozen; export and validate both journals; acknowledge both; restore; inspect restoration and restart-required flags"})
        self.save("restoration.json", self.cleanup)

    def run(self):
        phases = plan(self.config, self.args.plan)
        self.save("scenario.json", {"config": self.config, "plan": self.args.plan, "phases": phases, "clientId": self.client})
        try:
            print("Checking DevBench connection and diagnostic tools...", flush=True)
            catalog = self.controller("list", "catalog")
            tools = catalog["data"]["tools"]
            require({base.TOOL, ISL_TOOL} <= {tool["name"] for tool in tools}, "combined diagnostic tools not registered")
            nr_tool = next((tool for tool in tools if tool["name"] == NR_TOOL), {})
            properties = nr_tool.get("inputSchema", {}).get("properties", {})
            require({"nr_status", "nr_configure"} <= set(properties.get("action", {}).get("enum", [])) and
                    {"enabled", "expectedBuildId", "expectedConfigurationFingerprint"} <= properties.keys(),
                    "required NR status and guarded configuration API unavailable; no capture started")
            self.prepare_nr()
            self.prepare_dispatched = True
            prepared = self.call("prepare", purpose="isl")
            self.session = prepared["sessionId"]
            self.save("environment.json", prepared)
            self.original = self.isl("condition")
            self.save("original-condition.json", self.original)
            require(self.original["isl"]["loaded"] or self.args.plan == "mountains", "ISL must be loaded for subtraction controls")
            require(self.original["editor"]["clean"], "close/reset the Light Editor before this campaign")
            if self.args.plan == "conversion":
                require(self.original["startup"]["active"] == "suppress_from_startup", "conversion plan needs the staged one-launch suppression and restart")
            else:
                require(self.original["startup"]["active"] == "normal", "use a normal-radius process for this plan")
            if self.config["pacing"] == "off_unlimited":
                self.call("configure", reflexMode="off", capFps=0)
            for phase in phases:
                print(f"Preparing {phase['name']}; checking and settling the test condition.", flush=True)
                self.phase(phase)
        except (Exception, KeyboardInterrupt) as error:
            self.failures.append({"type": type(error).__name__, "message": str(error)})
            print(f"TEST FAILED: {type(error).__name__}: {error}", file=sys.stderr, flush=True)
        finally:
            if self.prepare_dispatched:
                print("Finalizing captures and restoring settings; keep Skyrim running...", flush=True)
                self.restore()
            self.save("nr-setup-final.json", self.nr_setup)
            self.save("campaign.json", {"completed": self.completed, "failures": self.failures, "cleanup": self.cleanup,
                      "buildId": self.args.build_id, "plan": self.args.plan, "rules": self.rules, "sceneTime": "preserved",
                      "neuralRenderingSetup": self.nr_setup})
            self.save("report.json", evidence.comparison(self.completed))
            with (self.root / 'report.txt').open('x', encoding='utf-8') as output:
                output.write(evidence.readable_report(self.completed, self.failures, self.cleanup))
                output.write("\nNeural Rendering setup: " + json.dumps(self.nr_setup) + "\n")
        failed = bool(self.failures or "restorationFailure" in self.cleanup or "stopOrExportFailure" in self.cleanup)
        if failed:
            print(f"TEST FAILED / INCOMPLETE: {len(self.completed)}/{len(phases)} windows processed. See report.txt.",
                  file=sys.stderr, flush=True)
        else:
            print(f"TEST FINISHED: {len(self.completed)}/{len(phases)} windows completed; capture settings restored.", flush=True)
            print("Collection succeeded; the diagnosis may still be inconclusive. See report.txt.", flush=True)
        if self.nr_setup["state"] == "off_verified":
            print("NR was verified OFF before testing; this toolkit never re-enables it. See nr-setup-final.json.", flush=True)
        elif self.nr_setup["state"] in ("disable_dispatched_outcome_unconfirmed", "disable_acknowledged"):
            print("NR setup did not finish verification; inspect NR receipts before retrying. No capture started.", file=sys.stderr, flush=True)
        print(f"Send the ENTIRE output folder: {self.root}", flush=True)
        return int(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", default=str(HERE / "core.json"))
    parser.add_argument("--plan", choices=PLANS, default="core")
    parser.add_argument("--observation", choices=("off", "counters", "timed"))
    parser.add_argument("--off-unlimited", action="store_true", help="Explicitly change pacing once; restore accepted settings afterward")
    parser.add_argument("--disable-neural-rendering", action="store_true",
                        help="Explicitly set NR OFF before the campaign and leave it off; preserve other configuration")
    for name in ("output", "controller", "runtime"):
        parser.add_argument("--" + name, required=True)
    for name in ("build-id", "artifact", "artifact-sha256"):
        parser.add_argument("--" + name, help=argparse.SUPPRESS)
    args = parser.parse_args()
    config = json.loads(Path(args.scenario).read_text(encoding="utf-8-sig"))
    if args.observation:
        config["observationLevel"] = args.observation
    if args.off_unlimited:
        config["pacing"] = "off_unlimited"
    try:
        return Campaign(args, config).run()
    except (Exception, KeyboardInterrupt) as error:
        print(f"TEST FAILED: {type(error).__name__}: {error}", file=sys.stderr, flush=True)
        print(f"Preserve any output already written to: {Path(args.output).resolve()}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
