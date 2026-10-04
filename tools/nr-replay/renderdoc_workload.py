"""Audit one offline capture through qrenderdoc's embedded Python runtime.

Set NR_REPLAY_CAPTURE and NR_REPLAY_TRACE_OUTPUT, then run
qrenderdoc.exe --python tools/nr-replay/renderdoc_workload.py. The JSON receipt,
not qrenderdoc's process exit code, determines whether analysis succeeded.
Set NR_REPLAY_TRACE_SCRIPT to this exact file to preserve its source hash when
the embedded interpreter omits __file__.
API references: renderdoc/api/replay/{renderdoc_replay,data_types,replay_enums}.h
in the official baldurk/renderdoc v1.46 source release.
"""

from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import traceback


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def save(path, report):
    temporary = path.with_name(path.name + ".writing")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    os.replace(str(temporary), str(path))


def walk_actions(actions):
    pending = [(action, None, []) for action in reversed(actions)]
    while pending:
        action, parent, markers = pending.pop()
        yield action, parent, markers
        child_markers = markers + ([str(action.customName)] if action.customName else [])
        pending.extend((child, int(action.eventId), child_markers)
                       for child in reversed(action.children))


def inventory(controller, rd, report):
    structured = controller.GetStructuredFile()
    chunks = structured.chunks
    api_events = {}
    actions = []
    flags_to_count = ("Dispatch", "Copy", "Resolve", "Clear", "PushMarker", "SetMarker", "Indirect")
    counts = Counter()
    for action, parent, markers in walk_actions(controller.GetRootActions()):
        flags = [name for name in flags_to_count if action.flags & getattr(rd.ActionFlags, name)]
        counts.update(flags)
        row = {
            "eventId": int(action.eventId), "actionId": int(action.actionId),
            "parentEventId": parent, "name": action.GetName(structured),
            "customName": str(action.customName), "markers": markers,
            "flags": int(action.flags), "classifiedFlags": flags,
            "dispatchGroups": list(action.dispatchDimension),
            "dispatchThreadOverride": list(action.dispatchThreadsDimension),
            "dispatchBase": list(action.dispatchBase),
            "copySource": str(action.copySource), "copyDestination": str(action.copyDestination),
            "children": [int(child.eventId) for child in action.children],
            "durationSeconds": None, "durationState": "not_collected", "errors": [],
        }
        actions.append(row)
        for event in action.events:
            chunk_index = int(event.chunkIndex)
            api_events[int(event.eventId)] = {
                "eventId": int(event.eventId), "chunkIndex": chunk_index,
                "name": str(chunks[chunk_index].name) if chunk_index < len(chunks) else None,
            }
    report["actions"] = actions
    report["apiEvents"] = [api_events[key] for key in sorted(api_events)]
    api_counts = Counter(event["name"] for event in api_events.values() if event["name"])
    report["apiCallCounts"] = dict(sorted(api_counts.items()))
    report["summary"] = {
        "actionCount": len(actions), "actionFlagCounts": dict(counts),
        "apiEventCount": len(api_events),
        "barrierApiCallCount": sum(count for name, count in api_counts.items()
                                   if "ResourceBarrier" in name or name.endswith("::Barrier")),
        "barrierCountDefinition": "Captured API calls, not the number of barriers within each call",
        "dispatchGroupHistogram": dict(Counter(
            "x".join(str(value) for value in action["dispatchGroups"])
            for action in actions if "Dispatch" in action["classifiedFlags"])),
    }
    report["structuredChunkCountsIncludingInitialization"] = dict(sorted(Counter(
        str(chunk.name) for chunk in chunks).items()))
    report["resources"] = [{"id": str(value.resourceId), "name": str(value.name),
                            "type": str(value.type)} for value in controller.GetResources()]
    report["textures"] = [{"id": str(value.resourceId), "width": int(value.width),
                           "height": int(value.height), "depth": int(value.depth),
                           "arraySize": int(value.arraysize), "mips": int(value.mips),
                           "format": value.format.Name(), "byteSize": int(value.byteSize)}
                          for value in controller.GetTextures()]
    report["buffers"] = [{"id": str(value.resourceId), "length": int(value.length)}
                         for value in controller.GetBuffers()]
    report["inventoryComplete"] = True


def shader_inventory(controller, rd, report):
    for action in report["actions"]:
        if "Dispatch" not in action["classifiedFlags"] or action["children"]:
            continue
        try:
            controller.SetFrameEvent(action["eventId"], True)
            fatal = controller.GetFatalErrorStatus()
            if fatal != rd.ResultCode.Succeeded:
                raise RuntimeError("capture replay failed: " + str(fatal))
            state = controller.GetPipelineState()
            reflection = state.GetShaderReflection(rd.ShaderStage.Compute)
            action["computeShader"] = {"resourceId": str(state.GetShader(rd.ShaderStage.Compute)),
                                       "sha256": None, "threadsPerGroup": None,
                                       "reflectionAvailable": reflection is not None}
            if reflection is not None:
                action["computeShader"]["threadsPerGroup"] = list(reflection.dispatchThreadsDimension)
                raw = bytes(reflection.rawBytes)
                if raw:
                    action["computeShader"]["sha256"] = hashlib.sha256(raw).hexdigest()
        except Exception as error:
            action["errors"].append({"stage": "shader_inventory", "message": str(error)})
            if controller.GetFatalErrorStatus() != rd.ResultCode.Succeeded:
                raise


def durations(controller, rd, report):
    duration_report = report["durationCounter"] = {"state": "unavailable", "reason": None, "results": []}
    try:
        counters = controller.EnumerateCounters()
        report["availableCounterIds"] = [int(counter) for counter in counters]
        counter = rd.GPUCounter.EventGPUDuration
        if counter not in counters:
            duration_report["reason"] = "EventGPUDuration was not advertised by the replay implementation"
            return
        desc = controller.DescribeCounter(counter)
        duration_report["description"] = {
            "name": str(desc.name), "description": str(desc.description),
            "type": str(desc.resultType), "byteWidth": int(desc.resultByteWidth), "unit": str(desc.unit)}
        if desc.resultType != rd.CompType.Float or desc.resultByteWidth not in (4, 8) or desc.unit != rd.CounterUnit.Seconds:
            raise RuntimeError("EventGPUDuration does not expose a floating point seconds contract")
        by_event = {}
        for result in controller.FetchCounters([counter]):
            value = float(result.value.d if desc.resultByteWidth == 8 else result.value.f)
            valid = math.isfinite(value) and value >= 0
            row = {"eventId": int(result.eventId), "counter": int(result.counter),
                   "seconds": value if valid else None,
                   "state": "available" if valid else "invalid",
                   "reason": None if valid else "negative or non-finite timestamp delta"}
            duration_report["results"].append(row)
            if row["eventId"] in by_event:
                raise RuntimeError("duplicate EventGPUDuration results for one event")
            by_event[row["eventId"]] = row
        for action in report["actions"]:
            result = by_event.get(action["eventId"])
            action["durationState"] = result["state"] if result else "not_reported"
            action["durationSeconds"] = result["seconds"] if result else None
        duration_report["state"] = "collected" if by_event else "unavailable"
        duration_report["missingDispatchEventIds"] = [
            action["eventId"] for action in report["actions"]
            if "Dispatch" in action["classifiedFlags"] and not action["children"]
            and action["durationState"] != "available"]
        if by_event and (duration_report["missingDispatchEventIds"] or any(
                result["state"] != "available" for result in duration_report["results"])):
            duration_report["state"] = "partial"
        if not by_event:
            duration_report["reason"] = "advertised counter returned no event results"
    except Exception as error:
        duration_report["state"] = "failed"
        duration_report["reason"] = str(error)


def analyse(capture_path, report, checkpoint):
    import renderdoc as rd

    handle = None
    controller = None
    try:
        report["renderdocVersion"] = rd.GetVersionString()
        handle = rd.OpenCaptureFile()
        status = handle.OpenFile(str(capture_path), "", None)
        report["openFileStatus"] = str(status)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError("cannot open capture: " + str(status))
        status, controller = handle.OpenCapture(rd.ReplayOptions(), None)
        report["openReplayStatus"] = str(status)
        if status != rd.ResultCode.Succeeded or controller is None:
            raise RuntimeError("cannot replay capture: " + str(status))
        properties = controller.GetAPIProperties()
        report["api"] = {"pipeline": str(properties.pipelineType),
                         "renderer": str(properties.localRenderer), "vendor": str(properties.vendor)}
        inventory(controller, rd, report)
        checkpoint()
        shader_inventory(controller, rd, report)
        checkpoint()
        durations(controller, rd, report)
        fatal = controller.GetFatalErrorStatus()
        report["fatalReplayStatus"] = str(fatal)
        report["debugMessages"] = [{"eventId": int(value.eventId), "severity": str(value.severity),
                                    "category": str(value.category), "description": str(value.description)}
                                   for value in controller.GetDebugMessages()]
        if fatal != rd.ResultCode.Succeeded:
            raise RuntimeError("capture replay failed: " + str(fatal))
        report["complete"] = True
        report["state"] = "complete" if report["durationCounter"]["state"] == "collected" and not any(
            row["errors"] for row in report["actions"]) else "complete_with_gaps"
    finally:
        for name, resource in (("controller", controller), ("capture", handle)):
            if resource is not None:
                try:
                    resource.Shutdown()
                except Exception as error:
                    report["cleanupErrors"].append({"resource": name, "message": str(error)})
                    report["complete"] = False
                    report["state"] = "failed"


def main():
    report = {
        "schema": "csx-nr-renderdoc-workload-v1", "complete": False, "state": "running",
        "inventoryComplete": False, "cleanupErrors": [], "actions": [],
        "startedUtc": datetime.now(timezone.utc).isoformat(),
        "limitations": [
            "Capture replay timings are instrumented diagnostics, not uninstrumented production timings.",
            "Dispatch grids do not reveal private kernel arithmetic, active tiles, or model semantics.",
            "No fixed-cost or area-scaling classification is inferred from a single capture.",
            "Barrier counts describe API calls; each call can contain multiple barriers.",
            "Resource byte sizes do not establish native residency or total physical GPU allocation.",
        ],
    }
    output = None
    try:
        capture_value = os.environ.get("NR_REPLAY_CAPTURE")
        output_value = os.environ.get("NR_REPLAY_TRACE_OUTPUT")
        if not output_value:
            raise RuntimeError("NR_REPLAY_TRACE_OUTPUT is required")
        output = Path(output_value).resolve()
        if output.exists() or output.with_name(output.name + ".writing").exists():
            output = None
            raise RuntimeError("preserving existing analysis output or pending write; choose a new output path")
        output.parent.mkdir(parents=True, exist_ok=True)
        if not capture_value:
            raise RuntimeError("NR_REPLAY_CAPTURE is required")
        capture = Path(capture_value).resolve(strict=True)
        if not capture.is_file() or capture.suffix.lower() != ".rdc":
            raise RuntimeError("NR_REPLAY_CAPTURE must identify an existing .rdc file")
        report["capture"] = {"path": str(capture), "bytes": capture.stat().st_size, "sha256": file_hash(capture)}
        script_path = globals().get("__file__") or os.environ.get("NR_REPLAY_TRACE_SCRIPT")
        report["scriptSha256"] = file_hash(Path(script_path).resolve()) if script_path else None
        report["scriptIdentityReason"] = None if script_path else "embedded Python does not expose __file__; caller may supply NR_REPLAY_TRACE_SCRIPT"
        save(output, report)
        analyse(capture, report, lambda: save(output, report))
    except Exception as error:
        report["state"] = "failed"
        report["complete"] = False
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
    finally:
        report["finishedUtc"] = datetime.now(timezone.utc).isoformat()
        if output is not None:
            save(output, report)
        print(json.dumps({"state": report["state"], "output": str(output) if output else None,
                          "error": report.get("error")}, allow_nan=False))
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    exit_code = 1
    try:
        exit_code = main()
    except BaseException:
        traceback.print_exc()
    finally:
        sys.exit(exit_code)
