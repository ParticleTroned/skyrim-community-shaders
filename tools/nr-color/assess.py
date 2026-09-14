#!/usr/bin/env python3
"""Bounded NR colour candidate assessment through skyrim-vr-automation/dev.

No direct HTTP, tokens, profile edits, scene mutations, or game launches. Default
is plan-only. Live runs require an already prepared static scene/test workspace.
Numerical ranking measures source drift, NOT a proven NVIDIA colour contract.
"""
from __future__ import annotations
import argparse
import copy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import time
from typing import Any
from verify_assets import verify

TOOL = "communityshaders.nr_color"
SETTING_KEYS = ("schemaVersion", "enabled", "mode", "detailStrength", "appearanceMix", "maximumDetailStops")
PROFILE_KEYS = ("domain", "transform", "exposureMultiplier", "exposureSource")
EXPERIMENT_KEYS = ("transportBypass", "diagnostics", "captureEngineExposure", "applyModelEdit")
PROFILE_NAMES = ("upscaled_center", "final_ldr_pre_ui")


class AssessmentError(RuntimeError):
    pass


class MutationUncertain(AssessmentError):
    pass


def editable(status: dict) -> dict:
    """Never send read-only provenance fields from status back to configure."""
    result = {"settings": {key: status["settings"][key] for key in SETTING_KEYS},
              "experiments": {key: status["experiments"][key] for key in EXPERIMENT_KEYS}}
    for name in PROFILE_NAMES:
        result["experiments"][name] = {key: status["experiments"][name][key] for key in PROFILE_KEYS}
    return result


def unwrap(receipt: dict) -> dict:
    semantic = receipt.get("semantic") or {}
    if receipt.get("transportOk") is not True or receipt.get("ok") is not True:
        raise AssessmentError("Controller rejected call or transport failed; inspect its receipt")
    if semantic.get("known") is not True or semantic.get("ok") is not True:
        raise AssessmentError("Unverified/failed DevBench semantic result")
    content = receipt.get("data", {}).get("content")
    if not isinstance(content, list) or len(content) != 1:
        raise AssessmentError("Unexpected tools/call response shape")
    payload = content[0]
    if isinstance(payload, dict) and payload.get("type") == "text":
        payload = json.loads(payload["text"])
    if not isinstance(payload, dict) or payload.get("ok") is not True:
        raise AssessmentError("NR colour API did not explicitly succeed")
    return payload


def fresh_groups(status: dict, revision: int, insertion: int, after_frame: int) -> list[list[dict]]:
    """Require a coherent current pair, including any matching secondary regions."""
    groups: dict[tuple, dict[int, dict]] = {}
    for item in status.get("measurements", []):
        source, values = item.get("source", {}), item.get("values", [])
        slot = source.get("physicalSlot", -1)
        if not isinstance(slot, int) or not 0 <= slot < 8:
            continue
        if (source.get("revision") != revision or source.get("insertionPoint") != insertion
                or not source.get("processed") or source.get("failure")
                or source.get("sourceWorldFrame", -1) <= after_frame):
            continue
        if len(values) != 24 or not all(isinstance(x, (float, int)) and math.isfinite(x) for x in values):
            continue
        key = (source["frame"], source["sourceWorldFrame"], source["generation"], slot % 4 // 2)
        groups.setdefault(key, {})[slot] = item
    output = []
    for key, items in groups.items():
        primary = key[3] * 2
        if primary not in items or primary + 1 not in items:
            continue
        # If a secondary physical region is known for this exact transaction,
        # don't accept only its primary while its measurement is still pending.
        expected = {o["physicalSlot"] for o in status.get("slots", [])
                    if o.get("revision") == revision and o.get("frame") == key[0]
                    and o.get("sourceWorldFrame") == key[1] and o.get("generation") == key[2]
                    and o.get("insertionPoint") == insertion and o.get("physicalSlot", -1) % 4 // 2 == key[3]}
        if not expected.issubset(items):
            continue
        output.append([items[slot] for slot in sorted(items)])
    return output


def assess_samples(groups: list[list[dict]], transport: bool) -> dict:
    failures: list[str] = []
    scores = []
    for group in groups:
        capture_sequences = set()
        for item in group:
            s, v = item["source"], item["values"]
            if v[3] <= 0 or v[15] <= 0 or any(v[i] > 0 for i in (12, 13, 14, 16, 17)):
                failures.append("nonfinite pixels, invalid codec or no valid samples")
            if v[19] != 1:
                failures.append("effective exposure invalid")
            if s["profile"]["exposureSource"] == "captured_hdr":
                stamp = s.get("exposure", {})
                if (s.get("exposureBinding") != "gpu_snapshot_queued" or v[23] != 1
                        or stamp.get("frame") != s["sourceWorldFrame"] or stamp.get("ambiguous")):
                    failures.append("missing/stale/ambiguous engine exposure")
                capture_sequences.add((stamp.get("epoch"), stamp.get("sequence")))
            if s.get("modelEditShown") is not True or bool(s.get("transportBypass")) != transport:
                failures.append("wrong A/B or transport state")
            # At identity transport the shader preserves baseline values. Allow a
            # small source-relative float tolerance, not an arbitrary green fix.
            tolerance = 1e-5 * max(1.0, *(abs(x) for x in v[:3]))
            if transport and v[8] > tolerance:
                failures.append("transport round-trip error")
            scale = max(1e-4, sum(abs(x) for x in v[:3]) / 3)
            scores.append(v[7] / scale)
        if len(capture_sequences) > 1:
            failures.append("different captured exposure identities within a stereo transaction")
    if not groups:
        failures.append("no coherent fresh stereo samples")
    return {"valid": not failures, "reasons": sorted(set(failures)),
            "sourceDriftScore": sum(scores) / len(scores) if scores else None,
            "domainVerified": False,
            "meaning": "Source-relative RGB change, not physical lighting accuracy or NR colour-domain proof"}


def candidates(include_captured: bool) -> list[dict]:
    choices = [dict(name="identity_native", domain="unknown", transform="identity", exposureSource="manual"),
               dict(name="linear_srgb", domain="linear", transform="linear_to_srgb", exposureSource="manual"),
               dict(name="linear_proxy", domain="linear", transform="reversible_proxy", exposureSource="manual")]
    if include_captured:
        choices += [dict(name="captured_" + transform, domain="linear", transform=transform, exposureSource="captured_hdr")
                    for transform in ("linear_to_srgb", "reversible_proxy")]
    return choices


def atomic_json(path: Path, data: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False), encoding="utf-8")
    temporary.replace(path)


class Controller:
    def __init__(self, args: argparse.Namespace, directory: Path):
        self.args, self.directory, self.index = args, directory, 0
        self.expected_identity: dict | None = None
        self.script = args.automation_root / "tools/devbench-control/Invoke-DevBenchControl.ps1"
        self.capture_script = args.automation_root / "tools/capture-interaction-control/Invoke-CaptureInteraction.ps1"
        if not self.script.is_file():
            raise AssessmentError("Missing automation/dev DevBench controller")

    def invoke(self, command: list[str], label: str, timeout: float) -> dict:
        self.index += 1
        stem = self.directory / f"{self.index:03d}-{label}"
        atomic_json(stem.with_suffix(".request.json"), {"command": command, "utc": datetime.now(timezone.utc).isoformat()})
        # No shell and no unbounded RPC retry. A timeout may have mutated state.
        try:
            result = subprocess.run(command, text=True, encoding="utf-8-sig", capture_output=True, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            atomic_json(stem.with_suffix(".timeout.json"), {"error": str(error), "indeterminate": True})
            raise AssessmentError("Timed-out controller; inspect its mutation journal") from error
        stem.with_suffix(".stdout.txt").write_text(result.stdout, encoding="utf-8")
        stem.with_suffix(".stderr.txt").write_text(result.stderr, encoding="utf-8")
        try:
            value = json.loads(result.stdout)
        except (ValueError, TypeError) as error:
            raise AssessmentError("Controller returned non-JSON output") from error
        atomic_json(stem.with_suffix(".json"), value)
        if result.returncode != 0:
            raise AssessmentError(f"Controller process returned {result.returncode}")
        return value

    def call(self, payload: dict, label: str) -> dict:
        command = [self.args.pwsh, "-NoProfile", "-File", str(self.script), "call", "-Tool", TOOL,
                   "-ArgumentsJson", json.dumps(payload, separators=(",", ":")), "-RuntimePath", str(self.args.runtime),
                   "-EvidenceDirectory", str(self.directory / "controller"), "-EvidenceLabel", label,
                   "-RequireSuccess", "-RequirePerformanceNeutral", "-MaxTransientRetries", "0", "-NoExit", "-Compact"]
        for option, field in (("-ArtifactPath", "artifact_path"), ("-WorkspaceManifestPath", "workspace_manifest"),
                              ("-ExpectedBuildId", "expected_build_id"), ("-ExpectedArtifactSha256", "expected_artifact_sha256")):
            value = getattr(self.args, field, None)
            if value is not None:
                command += [option, str(value)]
        if self.expected_identity is not None:
            command += ["-ExpectedRuntimeIdentityJson", json.dumps(self.expected_identity, separators=(",", ":"))]
        receipt = self.invoke(command, label, 45)
        payload = unwrap(receipt)
        if self.expected_identity is None:
            if not isinstance(receipt.get("runtimeIdentity"), dict):
                raise AssessmentError("Controller supplied no runtime identity for a qualified live run")
            self.expected_identity = receipt["runtimeIdentity"]
        return payload

    def wait_scene(self) -> None:
        command = [self.args.pwsh, "-NoProfile", "-File", str(self.script), "wait", "-Condition", "upscalingStable",
                   "-ExpectedCell", self.args.expected_cell, "-RuntimePath", str(self.args.runtime),
                   "-TimeoutSeconds", "30", "-RequireSuccess", "-NoExit", "-Compact",
                   "-EvidenceDirectory", str(self.directory / "controller")]
        result = self.invoke(command, "scene-barrier", 40)
        if result.get("ok") is not True or result.get("semantic", {}).get("ok") is not True:
            raise AssessmentError("Prepared scene/upscaling barrier failed; no scene mutation attempted")

    def episode(self, label: str) -> dict:
        if not self.capture_script.is_file():
            raise AssessmentError("Missing automation/dev capture-interaction controller")
        session = self.directory / (label + "-capture")
        result: dict[str, Any] = {"sessionDirectory": str(session), "sameFrameAsNumericSample": False}
        started = False
        try:
            for action in ("start", "observe"):
                command = [self.args.pwsh, "-NoProfile", "-File", str(self.capture_script), action,
                           "-SessionDirectory", str(session), "-RuntimePath", str(self.args.runtime), "-NoExit", "-Compact"]
                if action == "start":
                    command += ["-VisualMode", "sequence", "-PreferredView", "side_by_side", "-MaximumFrames", "8"]
                    started = True  # Also attempt cleanup after an indeterminate start.
                receipt = self.invoke(command, label + "-" + action, 60)
                result[action] = receipt
                if receipt.get("ok") is not True:
                    raise AssessmentError("Capture episode failed; retain its state/receipts")
        finally:
            if started:
                command = [self.args.pwsh, "-NoProfile", "-File", str(self.capture_script), "stop",
                           "-SessionDirectory", str(session), "-RuntimePath", str(self.args.runtime), "-NoExit", "-Compact"]
                result["stop"] = self.invoke(command, label + "-stop", 60)
                atomic_json(session.parent / (label + "-capture-result.json"), result)
                if result["stop"].get("ok") is not True:
                    raise AssessmentError("Owned capture cleanup failed; inspect session before another run")
        return result


def collect(controller: Controller, revision: int, insertion: int, after_frame: int, args: argparse.Namespace) -> list[list[dict]]:
    deadline = time.monotonic() + args.timeout
    samples: dict[tuple, list[dict]] = {}
    while time.monotonic() < deadline:
        status = controller.call({"action": "status"}, "sample")
        if status.get("revision") != revision:
            raise MutationUncertain("UI/another agent changed the configuration; automatic restore would overwrite it")
        for group in fresh_groups(status, revision, insertion, after_frame + args.warmup_frames):
            s = group[0]["source"]
            samples[(s["sourceWorldFrame"], s["generation"], s["physicalSlot"] % 4 // 2)] = group
        if len(samples) >= args.sample_frames:
            return list(samples.values())[-args.sample_frames:]
        time.sleep(0.25)
    raise AssessmentError("No fresh complete stereo samples within the deadline; check route, NR state and diagnostics")


def run_live(args: argparse.Namespace, directory: Path) -> dict:
    controller = Controller(args, directory)
    report: dict[str, Any] = {"ok": False, "candidates": [], "domainVerified": False, "restored": False,
                              "automationReferenceReviewed": "a4ab2cf6ea6c853926918e5626b8d17f15cf5d79", "insertion": args.insertion}
    original = None
    owned_revision = None
    current = None
    try:
        controller.wait_scene()
        assets = controller.call({"action": "assets"}, "assets")
        if not assets.get("allPresent"):
            raise AssessmentError("Runtime colour shader inventory is incomplete")
        current = controller.call({"action": "status"}, "initial")
        if current.get("apiVersion", 0) < 2:
            raise AssessmentError("The running plugin does not expose NR colour API v2")
        original = editable(current)
        atomic_json(directory / "original-configuration.json", {"revision": current["revision"], **original})
        owned_revision = current["revision"]
        insertion = PROFILE_NAMES.index(args.insertion)

        def apply(desired: dict, label: str) -> dict:
            nonlocal owned_revision, current
            expected = owned_revision + (desired != editable(current))
            try:
                updated = controller.call({"action": "configure", "expectedRevision": owned_revision, **desired}, label)
            except Exception as error:
                owned_revision = None
                raise MutationUncertain("Mutation outcome/ownership unproven; no retry or blind restore") from error
            if updated.get("revision") != expected or editable(updated) != desired:
                owned_revision = None
                raise MutationUncertain("Configuration changed during acknowledgement")
            owned_revision, current = expected, updated
            return updated

        for candidate in candidates(args.include_captured):
            controller.wait_scene()
            entry: dict[str, Any] = {"candidate": candidate, "domainVerified": False}
            report["candidates"].append(entry)
            desired = copy.deepcopy(original)
            desired["settings"].update(enabled=True, mode="managed")
            desired["experiments"].update(diagnostics=True, applyModelEdit=True, captureEngineExposure=True)
            desired["experiments"][args.insertion] = {k: candidate[k] for k in ("domain", "transform", "exposureSource")}
            desired["experiments"][args.insertion]["exposureMultiplier"] = 1.0
            for phase in ("transport", "neural"):
                desired["experiments"]["transportBypass"] = phase == "transport"
                floor = max((s.get("sourceWorldFrame", 0) for s in current.get("slots", [])), default=0)
                apply(desired, candidate["name"] + "-" + phase)
                groups = collect(controller, owned_revision, insertion, floor, args)
                verdict = assess_samples(groups, phase == "transport")
                entry[phase] = {"revision": owned_revision, "verdict": verdict, "samples": groups}
                atomic_json(directory / "report.json", report)
                if not verdict["valid"]:
                    entry["classification"] = "rejected_or_unavailable"
                    break
            else:
                entry["classification"] = "candidate_for_visual_review"
                if args.capture_episodes:
                    # Paired visual A/B with unchanged model input. Camera is not
                    # frozen by this script; preserve both frame manifests.
                    entry["editShownCapture"] = controller.episode(candidate["name"] + "-shown")
                    desired["experiments"]["applyModelEdit"] = False
                    apply(desired, candidate["name"] + "-hide-edit")
                    # Wait for actual application of the A/B state, not just an API ack.
                    floor = max((s.get("sourceWorldFrame", 0) for s in current.get("slots", [])), default=0)
                    collect(controller, owned_revision, insertion, floor, args)
                    entry["baselineCapture"] = controller.episode(candidate["name"] + "-baseline")
            atomic_json(directory / "report.json", report)
        valid = [e for e in report["candidates"] if e.get("classification") == "candidate_for_visual_review"]
        report["sourceFidelityRanking"] = [e["candidate"]["name"] for e in sorted(valid, key=lambda e: e["neural"]["verdict"]["sourceDriftScore"])]
        report["ok"] = bool(valid)
        report["conclusion"] = "Candidates ranked for source fidelity; image/producer evidence must adjudicate colour domain. No automatic production profile was selected."
    except Exception as error:
        if isinstance(error, MutationUncertain):
            owned_revision = None
        report["error"] = str(error)
    finally:
        if original is not None and owned_revision is not None:
            try:
                latest = controller.call({"action": "status"}, "before-restore")
                if latest.get("revision") != owned_revision:
                    raise AssessmentError("Concurrent change detected: preserve the user's newer configuration")
                restored = controller.call({"action": "configure", "expectedRevision": owned_revision, **original}, "restore")
                report["restored"] = editable(restored) == original
                if not report["restored"]:
                    report["restoreError"] = "Restore did not acknowledge the original configuration"
            except Exception as error:
                report["restoreError"] = str(error)
        elif original is not None:
            report["restoreError"] = "Ownership unproven after a mutation failure; original-configuration.json is retained for explicit recovery"
        if not report["restored"] and original is not None:
            report["ok"] = False
        atomic_json(directory / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--automation-root", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--expected-cell")
    parser.add_argument("--insertion", choices=PROFILE_NAMES, default="upscaled_center")
    parser.add_argument("--deployed-data", type=Path)
    parser.add_argument("--pwsh", default="pwsh")
    parser.add_argument("--artifact-path", type=Path)
    parser.add_argument("--workspace-manifest", type=Path)
    parser.add_argument("--expected-build-id")
    parser.add_argument("--expected-artifact-sha256")
    parser.add_argument("--sample-frames", type=int, default=3)
    parser.add_argument("--warmup-frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--include-captured", action="store_true")
    parser.add_argument("--capture-episodes", action="store_true")
    parser.add_argument("--confirm-static-scene", action="store_true")
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.sample_frames <= 30 or not 0 <= args.warmup_frames <= 600 or not 1 <= args.timeout <= 300:
        parser.error("sample/warmup/timeout outside bounded test limits")
    if not args.live:
        print(json.dumps({"mode": "plan_only", "candidates": candidates(args.include_captured),
                          "insertion": args.insertion, "worldMutations": False, "domainVerified": False,
                          "requires": "Prepared static scene, identity-bound automation/dev runtime, new evidence directory and explicit --live"}, indent=2))
        return 0
    if not all((args.automation_root, args.runtime, args.evidence_dir, args.expected_cell, args.confirm_static_scene)):
        parser.error("--live requires automation-root, runtime, new evidence-dir, expected-cell and confirm-static-scene")
    args.automation_root, args.runtime = args.automation_root.resolve(), args.runtime.resolve()
    args.evidence_dir = args.evidence_dir.resolve()
    # Never reuse a prior run's evidence or overwrite a shared profile directory.
    args.evidence_dir.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    assets = verify(root, args.deployed_data.resolve() if args.deployed_data else None)
    atomic_json(args.evidence_dir / "asset-check.json", assets)
    if not assets["ok"]:
        print(json.dumps(assets, indent=2)); return 2
    try:
        result = run_live(args, args.evidence_dir)
    except Exception as error:
        result = {"ok": False, "error": str(error), "domainVerified": False}
        atomic_json(args.evidence_dir / "report.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
