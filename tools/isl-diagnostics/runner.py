"""Run ranked ISL subtraction captures with one shared, owned DevBench collector."""

from __future__ import annotations

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import os
import shutil
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
    "engine": ["R", "R", "R", "M", "M", "M", "R", "R", "R"],
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
    require(config["observationLevel"] in ("off", "counters", "timed", "engine"), "invalid observer")
    if selected == "engine":
        require(config["observationLevel"] == "counters", "engine plan selects its observers automatically")
        return [{"name": f"engine-{index + 1:02}-{'R' if enabled else 'M'}", "label": "R" if enabled else "M",
                 "group": "engine", "durationMs": 30000 if index % 3 == 0 else 10000,
                 "gates": NORMAL | {"observationLevel": "counters" if index % 3 == 0 else "engine"},
                 "islEnabled": enabled, "shadowCulling": None, "pruneFrames": None}
                for index, enabled in enumerate(value for value in (True, False, True) for _ in range(3))]
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


def late_start_phases():
    """Long quiet observations and bounded timed probes have separate baselines."""
    phases = []
    for group, sequence, duration, observer in (
            ("mountains", (True, False, True, False, True), 60000, "counters"),
            ("timed", tuple(state for state in (True, False, True, False, True) for _ in range(3)), 10000, "engine")):
        for index, enabled in enumerate(sequence):
            label = "R" if enabled else "M"
            phases.append({"name": f"{group}-{index + 1:02}-{label}", "label": label, "group": group,
                           "durationMs": duration, "gates": NORMAL | {"observationLevel": observer},
                           "islEnabled": enabled, "shadowCulling": None, "pruneFrames": None})
    return phases


class Campaign(base.Campaign):
    def __init__(self, args, config):
        super().__init__(args, config)
        self.args.build_id = None
        self.client = "isl-" + uuid.uuid4().hex
        self.original = None
        self.raw_unacked = None
        self.side_unacked = None
        self.last_transport = None
        self.nr_owned_configuration = None
        self.nr_pending_configuration = None
        self.nr_comparison_touched = False
        self.nr_setup = {"state": "not_checked", "disableAuthorized": args.disable_neural_rendering,
                         "policy": "explicit NR-off setup is retained; capture settings are restored separately"}

    def controller(self, *args, **kwargs):
        result = super().controller(*args, **kwargs)
        self.last_transport = result
        return result

    def identity_parameters(self):
        # Capture ownership and live API guards do not require an on-disk DLL audit.
        return {"SkipRuntimeIdentityVerification": True}

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
        self.settle_nr(verified)
        self.nr_setup.update(state="off_verified", requestedAfter=False,
                             changedBySetup=enabled, fingerprint=verified["requestedConfigurationFingerprint"])
        self.save("nr-setup.json", self.nr_setup)
        print("Neural Rendering OFF verified. Detailed NR state will be saved around every window.", flush=True)

    def settle_nr(self, status, prefix="nr"):
        print("Waiting for NR cleanup and a quiet settling interval before capture...", flush=True)
        deadline = time.monotonic() + 140
        fingerprint = status["requestedConfigurationFingerprint"]
        previous = evidence.nr_counters(status)
        quiet_since = None
        ordinal = 0
        while True:
            require(evidence.nr_preference(status) is False and status["requestedConfigurationFingerprint"] == fingerprint,
                    "NR settings changed during cleanup; no capture started")
            retained = status.get("renderer", {}).get("memoryRetirement", {}).get("backendRetained")
            require(type(retained) is bool, "NR cleanup state unavailable; no capture started")
            counters = evidence.nr_counters(status)
            now = time.monotonic()
            if retained or counters != previous:
                quiet_since = None
            elif quiet_since is None:
                quiet_since = now
            if quiet_since is not None and now - quiet_since >= self.config["settleSeconds"]:
                self.save(prefix + "-settled.json", {"status": status, "quietSeconds": now - quiet_since})
                return
            require(now < deadline, "NR cleanup did not become quiet within 140s; see nr-settle status files")
            previous = counters
            time.sleep(2)
            ordinal += 1
            if ordinal % 5 == 0:
                print("Still waiting for NR cleanup/settling; keep Skyrim running.", flush=True)
            status = self.nr("nr_status", f"{prefix}-settle-{ordinal:03}")
            if self.session is not None:
                self.isl("condition")

    def nr_condition(self, name, condition, enabled=False):
        status = self.nr("nr_status", name)
        require(evidence.nr_preference(status) is enabled and condition.get("nrRequested") is enabled,
                "NR condition changed or disagrees between APIs; see saved NR and ISL condition receipts")
        if self.nr_owned_configuration is not None:
            require(status["requestedConfiguration"] == self.nr_owned_configuration,
                    "NR configuration changed outside the comparison; preserving user settings")
        return status

    def change_nr(self, enabled, name):
        current = self.nr("nr_status", name + "-current")
        require(current["requestedConfiguration"] == self.nr_owned_configuration,
                "NR configuration changed outside the comparison; preserving user settings")
        if evidence.nr_preference(current) is enabled:
            return current
        require(not enabled or not current["requestedConfiguration"]["upscaling"].get("periphery_taa_enable", False),
                "NR enable would also disable FOV + TAA; comparison stopped to preserve that preference")
        target = copy.deepcopy(self.nr_owned_configuration)
        target["upscaling"]["neuralRenderingEnabled"] = enabled
        self.nr_pending_configuration = target
        self.nr_comparison_touched = True
        self.save(name + "-intent.json", {"before": self.nr_owned_configuration, "target": target,
                                         "expectedConfigurationFingerprint": current["requestedConfigurationFingerprint"]})
        changed = self.nr("nr_configure", name + "-change", enabled=enabled,
                          expectedConfigurationFingerprint=current["requestedConfigurationFingerprint"])
        require(changed["requestedConfiguration"] == target,
                "NR transition changed additional configuration; inspect saved receipts")
        verified = self.nr("nr_status", name + "-verified")
        require(verified["requestedConfiguration"] == target, "NR configuration changed during verification")
        self.nr_owned_configuration = target
        self.nr_pending_configuration = None
        return verified

    def compare_nr(self, original):
        require(self.original["isl"]["enabled"] is True, "enable ISL at the reproducing viewpoint before the NR comparison")
        gates = {key: self.original["effective"][key] for key in NORMAL}
        require(all(gates[key] == "normal" for key in NORMAL if key != "observationLevel"),
                "NR comparison requires normal ISL diagnostic paths")
        self.nr_owned_configuration = copy.deepcopy(original["requestedConfiguration"])
        initial = evidence.nr_preference(original)
        self.nr_setup.update(state="comparison_started", originalRequested=initial,
                             policy="capture initial state, then NR off/on/off; leave NR off, preserve other settings")
        for index, (label, enabled) in enumerate((("as-found", initial), ("off", False), ("on", True), ("off", False))):
            name = f"NR-{index:02}-{label}"
            phase = {"name": name, "label": label, "group": "nr", "nrEnabled": enabled,
                     "gates": gates, "islEnabled": True, "shadowCulling": None, "pruneFrames": None}
            if index:
                print(f"Preparing {name}: setting NR {'ON' if enabled else 'OFF'}; keeping ISL and pacing unchanged.", flush=True)
                status = self.change_nr(enabled, name)
                if not enabled:
                    self.settle_nr(status, prefix=name)
            else:
                print("Capturing the initial state before changing NR, ISL or pacing.", flush=True)
            self.phase(phase)
        self.nr_setup.update(state="off_verified", requestedAfter=False, comparisonCompleted=True)

    def compare_interaction(self):
        print("Comparing ISL OFF / ON / OFF while NR renders; other NR settings stay unchanged.", flush=True)
        self.change_nr(True, "interaction-nr-on")
        for index, enabled in enumerate((False, True, False)):
            self.phase({"name": f"interaction-{index + 1:02}-{'ISL-on' if enabled else 'ISL-off'}",
                        "label": "R" if enabled else "M", "group": "interaction", "nrEnabled": True,
                        "gates": NORMAL.copy(), "islEnabled": enabled, "shadowCulling": None, "pruneFrames": None})
        status = self.change_nr(False, "interaction-nr-off")
        self.settle_nr(status, prefix="interaction-nr-off")

    def finish_nr_comparison(self):
        if not self.nr_comparison_touched:
            return
        try:
            require("stopOrExportFailure" not in self.cleanup,
                    "capture stop/export unconfirmed; NR left unchanged, inspect recovery receipts")
            current = self.nr("nr_status", "nr-final-current")
            require(current["requestedConfiguration"] in (self.nr_owned_configuration, self.nr_pending_configuration),
                    "NR configuration changed outside the comparison; preserving user settings")
            self.nr_owned_configuration = copy.deepcopy(current["requestedConfiguration"])
            status = self.change_nr(False, "nr-final-off")
            self.nr_setup.update(state="off_verified", requestedAfter=False,
                                 fingerprint=status["requestedConfigurationFingerprint"])
            self.cleanup["neuralRendering"] = {"offVerified": True, "configuration": status["requestedConfiguration"]}
        except (Exception, KeyboardInterrupt) as error:
            self.cleanup["nrFinalizationFailure"] = str(error)
            self.nr_setup["state"] = "final_state_unconfirmed"
            print("NR final state unconfirmed: " + str(error), file=sys.stderr, flush=True)

    def export_pair(self, capture, label):
        # The game persists frozen data once. Only copying and receipt checks run here.
        receipt = self.call("save_capture", captureId=capture)
        require(receipt.get("saved") is True and receipt.get("format") == "csx-isl-capture-v1" and
                receipt.get("captureId") == capture, "native capture save was not confirmed")
        require(type(receipt.get("bytes")) is int and receipt["bytes"] > 0 and
                all(type(receipt.get(k)) is int and receipt[k] >= 0 for k in ("rawTotal", "islTotal")),
                "invalid native file receipt; capture remains owned")
        self.save(label + "-native-file.json", receipt)
        source = Path(receipt["path"])
        require(source.is_absolute() and source.suffix.lower() == ".csxcap", "native recording path is not an absolute .csxcap file")
        destination = self.root / (label + ".csxcap")
        partial = destination.with_suffix(".csxcap.partial")
        require(source.stat().st_size == receipt["bytes"], "native capture file size changed")
        with source.open("rb") as incoming, partial.open("xb") as outgoing:
            shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)
            outgoing.flush()
            os.fsync(outgoing.fileno())
        require(partial.stat().st_size == receipt["bytes"], "capture copy incomplete; originals preserved")
        require(not destination.exists(), "capture destination exists; preserving it")
        partial.rename(destination)
        copied = receipt | {"file": destination.name, "copiedBytes": destination.stat().st_size}
        self.save(label + "-collection.json", copied)
        # Both totals are retained before either ACK so lost replies can be retried safely.
        self.raw_unacked, self.side_unacked = receipt["rawTotal"], receipt["islTotal"]
        self.call("acknowledge_export", captureId=capture, total=self.raw_unacked)
        self.raw_unacked = None
        released = self.isl("acknowledge_export", captureId=capture, total=self.side_unacked)
        self.side_unacked = None
        require(released["released"], "joint capture not released after both acknowledgements")
        return copied

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
        duration = phase.get("durationMs", self.config["durationMs"])
        nr_comparison = phase.get("group") == "nr"
        self.save(name + "-requested.json", phase)
        if phase.get("group") == "engine":
            reset = self.call("set_noon")
            self.save(name + "-time-reset.json", reset)
            require(evidence.finite(reset.get("gameHour")) and 12 <= reset["gameHour"] <= 12.05,
                    "noon reset not confirmed; no capture started")
        if not nr_comparison:
            self.isl("configure", gates=phase["gates"], islEnabled=phase["islEnabled"],
                     shadowCulling=phase["shadowCulling"] or self.original["shadowCulling"]["settings"])
        before = self.settle_isl()
        if phase.get("group") == "engine":
            at_noon = evidence.finite(before.get("gameHour")) and 12 <= before["gameHour"] <= 12.05
            self.save(name + "-time-verified.json", {"gameHour": before.get("gameHour"), "settleSeconds": self.config["settleSeconds"], "withinNoonRange": at_noon})
            if not at_noon:
                print("Noon settling exceeded the comparison range; retaining this recording for offline diagnosis.", flush=True)
        if nr_comparison:
            require(all(before[key] == self.original[key] for key in ("pacing", "isl", "effective", "shadowBatch")) and
                    before["shadowCulling"]["settings"] == self.original["shadowCulling"]["settings"],
                    "ISL, shadows or pacing changed during NR comparison; preserving user settings")
        nr_before = self.nr_condition(name + "-nr-before", before, phase.get("nrEnabled", False))
        if phase["pruneFrames"]:
            self.isl("arm_prune_deferral", frames=phase["pruneFrames"])
        print(f"Capturing {name} ({duration / 1000:g}s); keep the same viewpoint.", flush=True)
        self.pending_start = True
        started = self.call("start", mode="B", durationMs=duration, capacity=self.config["capacity"],
                            admissionDelayMs=self.config["admissionDelayMs"], readback=False)
        transport_start = self.last_transport
        self.capture = started["captureId"]
        self.last_started = started
        self.last_capture_id = self.capture
        self.pending_start = False
        self.save(name + "-start.json", started)
        client_ready = base.qpc_now()
        # Local bounded sleeps only: no controller, file I/O, screenshot or polling inside admission.
        deadline = time.monotonic() + (started["admissionDelayMs"] + duration + 2250) / 1000
        while time.monotonic() < deadline:
            time.sleep(min(30, max(0, deadline - time.monotonic())))
        stopped = self.call("stop", captureId=self.capture, reason="completed")
        transport_stop = self.last_transport
        require(stopped["frozen"], "capture still draining; recovery manifest retains ownership")
        after = self.isl("condition")
        nr_after = self.nr_condition(name + "-nr-after", after, phase.get("nrEnabled", False))
        raw_after = self.call("condition")
        # Save analysis inputs before copying so interruption cannot erase the context.
        self.save(name + "-guard-before.json", transport_start)
        self.save(name + "-guard-after.json", transport_stop)
        inputs = {"name": name, "condition": phase, "before": before, "after": after,
                  "started": started, "rawAfter": raw_after, "nrBefore": nr_before, "nrAfter": nr_after,
                  "clientReadyQpc": client_ready, "original": self.original,
                  "pacingPolicy": self.config["pacing"], "sceneTimePolicy": "noon reset then settled" if phase.get("group") == "engine" else "preserved"}
        self.save(name + "-analysis-input.json", inputs)
        print(f"Saving {name}; analysis will run offline after you send the files.", flush=True)
        receipt = self.export_pair(self.capture, name)
        self.capture = None
        result = {"name": name, "condition": phase, "collected": True,
                  "file": receipt["file"], "bytes": receipt["bytes"], "analysis": "pending_offline"}
        self.completed.append(result)
        # Natural weather/time changes are evidence, not a reason to abort collection.
        stable = all(before[k] == after[k] for k in ("pacing", "nrRequested", "effective", "shadowBatch"))
        stable &= before.get("isl") == after.get("isl")
        stable &= before["shadowCulling"]["settings"] == after["shadowCulling"]["settings"]
        expected_pacing = self.original["pacing"] if self.config["pacing"] == "preserve" else before["pacing"]
        require(stable and before["pacing"] == expected_pacing,
                f"{name}: settings changed outside the test; recording saved, preserving user changes")

    def restore(self):
        try:
            stop = self.call("recover_and_stop", reason="isl_runner_finally")
            self.session = stop["sessionId"]
            self.cleanup["stop"] = stop
            capture = stop.get("captureId")
            if stop.get("released"):
                self.raw_unacked = self.side_unacked = None
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
                  "recovery": "same owner: recover_and_stop; wait for frozen; save_capture and copy the native file; acknowledge both; restore; inspect restoration and restart-required flags"})
        self.save("restoration.json", self.cleanup)

    def run(self):
        phases = plan(self.config, self.args.plan)
        compare_nr = getattr(self.args, "compare_neural_rendering", False)
        extended = late_start_phases() if compare_nr and self.args.plan == "core" else []
        total = len(phases) + len(extended) + (7 if compare_nr else 0)
        self.save("scenario.json", {"config": self.config, "plan": self.args.plan, "phases": phases, "clientId": self.client,
                                   "nrComparison": ["as-found", "off", "on", "off"] if compare_nr else None,
                                   "interaction": "NR on; ISL off/on/off" if compare_nr else None, "lateStarts": extended,
                                   "engineTimePolicy": "reset to noon before each engine-plan window; verify and settle at least 5s" if self.args.plan == "engine" else None})
        try:
            require(not compare_nr or (self.args.plan in ("core", "engine") and self.config["pacing"] == "preserve" and
                                      self.config["observationLevel"] == "counters" and not self.args.disable_neural_rendering),
                    "NR comparison requires core/engine, counters, preserved pacing, and no separate DisableNeuralRendering option")
            print("Checking DevBench connection and diagnostic tools...", flush=True)
            catalog = self.controller("list", "catalog")
            tools = catalog["data"]["tools"]
            require({base.TOOL, ISL_TOOL} <= {tool["name"] for tool in tools}, "combined diagnostic tools not registered")
            raw_tool = next(tool for tool in tools if tool["name"] == base.TOOL)
            require("save_capture" in raw_tool.get("inputSchema", {}).get("properties", {}).get("action", {}).get("enum", []),
                    "this toolkit requires the new file-capture diagnostic AIO; no settings changed")
            side_tool = next(tool for tool in tools if tool["name"] == ISL_TOOL)
            observer_schema = side_tool.get("inputSchema", {}).get("properties", {}).get("gates", {}).get("properties", {}).get("observationLevel", {})
            require(not (compare_nr or self.args.plan == "engine") or "engine" in observer_schema.get("enum", []),
                    "this diagnostic AIO lacks broader engine recording; no settings changed")
            nr_tool = next((tool for tool in tools if tool["name"] == NR_TOOL), {})
            properties = nr_tool.get("inputSchema", {}).get("properties", {})
            require({"nr_status", "nr_configure"} <= set(properties.get("action", {}).get("enum", [])) and
                    {"enabled", "expectedBuildId", "expectedConfigurationFingerprint"} <= properties.keys(),
                    "required NR status and guarded configuration API unavailable; no capture started")
            if compare_nr:
                nr_original = self.nr("nr_status", "nr-original")
            else:
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
            if compare_nr:
                self.compare_nr(nr_original)
                self.compare_interaction()
            if self.config["pacing"] == "off_unlimited":
                self.call("configure", reflexMode="off", capFps=0)
            for phase in phases + extended:
                print(f"Preparing {phase['name']}; checking and settling the test condition.", flush=True)
                self.phase(phase)
        except (Exception, KeyboardInterrupt) as error:
            self.failures.append({"type": type(error).__name__, "message": str(error)})
            print(f"TEST FAILED: {type(error).__name__}: {error}", file=sys.stderr, flush=True)
        finally:
            if self.prepare_dispatched:
                print("Finalizing captures and restoring settings; keep Skyrim running...", flush=True)
                self.restore()
            if compare_nr:
                self.finish_nr_comparison()
            self.save("nr-setup-final.json", self.nr_setup)
            self.save("campaign.json", {"completed": self.completed, "failures": self.failures, "cleanup": self.cleanup,
                      "buildId": self.args.build_id, "plan": self.args.plan, "sceneTime": "per-phase policy in analysis inputs",
                      "neuralRenderingSetup": self.nr_setup})
            self.save("report.json", {"status": "COLLECTION_ONLY", "collected": len(self.completed),
                      "expected": total, "analysis": "pending_offline", "failures": self.failures, "cleanup": self.cleanup})
            with (self.root / 'report.txt').open('x', encoding='utf-8') as output:
                output.write(f"COLLECTION ONLY: {len(self.completed)}/{total} windows saved.\n")
                output.write("No diagnosis or offline analysis was run on this computer. Send the entire folder.\n")
                output.write("Failures: " + json.dumps(self.failures) + "\n")
                output.write("Restoration: " + json.dumps(self.cleanup) + "\n")
                output.write("NR final state: " + json.dumps(self.nr_setup) + "\n")
        failed = bool(self.failures or any(key in self.cleanup for key in ("restorationFailure", "stopOrExportFailure", "nrFinalizationFailure")))
        if failed:
            print(f"TEST FAILED / INCOMPLETE: {len(self.completed)}/{total} windows processed. See report.txt.",
                  file=sys.stderr, flush=True)
        else:
            print(f"TEST FINISHED: {len(self.completed)}/{total} windows completed; capture settings restored.", flush=True)
            print("You can close Skyrim now. Export and analysis will run on our computer; send the whole folder.", flush=True)
        if self.nr_setup["state"] == "off_verified":
            print("NR comparison finished with NR OFF. See nr-setup-final.json." if compare_nr else
                  "NR was verified OFF before testing; this toolkit never re-enables it. See nr-setup-final.json.", flush=True)
        elif self.nr_setup["state"] in ("disable_dispatched_outcome_unconfirmed", "disable_acknowledged"):
            print("NR setup did not finish verification; inspect NR receipts before retrying. No capture started.", file=sys.stderr, flush=True)
        print(f"Send the ENTIRE output folder: {self.root}", flush=True)
        return int(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", default=str(HERE / "core.json"))
    parser.add_argument("--plan", choices=PLANS, default="core")
    parser.add_argument("--observation", choices=("off", "counters", "timed", "engine"))
    parser.add_argument("--off-unlimited", action="store_true", help="Explicitly change pacing once; restore accepted settings afterward")
    nr_options = parser.add_mutually_exclusive_group()
    nr_options.add_argument("--compare-neural-rendering", action="store_true",
                           help="Capture initial state and NR/ISL reversals, then the selected core or focused engine plan; leave NR off")
    nr_options.add_argument("--disable-neural-rendering", action="store_true",
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
