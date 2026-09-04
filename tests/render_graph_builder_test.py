#!/usr/bin/env python3
from __future__ import annotations

import json
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path


def envelope(sequence: int, event_type: str, payload: dict) -> dict:
    return {
        "schema": {"name": "csx.render-event", "major": 1, "minor": 17, "producerVersion": "test"},
        "captureId": "capture-resource-flow-test",
        "sequence": sequence,
        "timestampQpc": 1000 + sequence,
        "processId": 1,
        "threadId": 2,
        "frame": {"cpuFrame": 1, "sceneEpoch": 1, "submissionEpoch": None, "eye": "unknown", "eyeMask": None},
        "execution": {"observationDomain": "cpu-call", "commandStreamSequence": sequence, "gpuTimestampTicks": None, "gpuTimestampFrequencyHz": None},
        "deviceContextObservationId": "obs-device-context-1-g1",
        "commandRecordingObservationId": None,
        "submissionObservationId": None,
        "type": event_type,
        "scopes": {"renderPass": None, "technique": None, "geometry": None, "commandList": None},
        "causes": [], "manifestRefs": [], "engineRefs": [], "observationRefs": [],
        "payload": payload, "extensions": {},
    }


def build_graph(
    tool: Path,
    manifest: dict,
    events: list[dict],
    shader_manifest: dict | None = None,
    engine_map: dict | None = None,
) -> dict:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        manifest_path = root / "capture-manifest.json"
        events_path = root / "events.jsonl"
        output_path = root / "render-graph.json"
        shader_manifest_path = root / "shader-manifest.json"
        engine_map_path = root / "engine-map.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        events_path.write_text("\n".join(json.dumps(event) for event in events) + "\n", encoding="utf-8")
        command = [
            sys.executable, str(tool), "--capture-manifest", str(manifest_path),
            "--events", str(events_path), "--output", str(output_path),
        ]
        if shader_manifest is not None:
            shader_manifest_path.write_text(json.dumps(shader_manifest), encoding="utf-8")
            command.extend(["--shader-manifest", str(shader_manifest_path)])
        if engine_map is not None:
            engine_map_path.write_text(json.dumps(engine_map), encoding="utf-8")
            command.extend(["--engine-map", str(engine_map_path)])
        subprocess.run(
            command,
            check=True,
        )
        return json.loads(output_path.read_text(encoding="utf-8"))


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    tool = repo / "tools" / "build-render-graph.py"
    manifest = {
        "schema": {"name": "csx.render-capture-manifest", "major": 1, "minor": 4, "producerVersion": "test"},
        "captureId": "capture-resource-flow-test", "status": "complete", "createdAtUtc": "2026-08-26T00:00:00Z",
        "completion": {"truncated": False},
    }
    resource_base = {
        "schema": "resource-observation-v1", "d3dObjectPointer": "0x1", "pointerGeneration": 1,
        "dimension": "texture-2d", "widthOrBytes": 64, "height": 64, "depthOrArraySize": 1,
        "mipLevels": 1, "format": 28, "sampleCount": 1, "sampleQuality": 0, "usage": 0,
        "bindFlags": 0, "cpuAccessFlags": 0, "miscFlags": 0, "structureByteStride": 0,
    }
    hazard_events = [
        envelope(0, "resource-observed", {**resource_base, "resourceObservationId": "obs-resource-1-g1"}),
        envelope(1, "resource-observed", {**resource_base, "resourceObservationId": "obs-resource-2-g1", "d3dObjectPointer": "0x2"}),
        envelope(2, "target-view-observed", {"schema": "target-view-observation-v1", "targetViewObservationId": "obs-shader-resource-view-3-g1", "kind": "shader-resource-view", "d3dObjectPointer": "0x3", "pointerGeneration": 1, "resourceObservationId": "obs-resource-1-g1", "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0}),
        envelope(3, "target-view-observed", {"schema": "target-view-observation-v1", "targetViewObservationId": "obs-render-target-4-g1", "kind": "render-target", "d3dObjectPointer": "0x4", "pointerGeneration": 1, "resourceObservationId": "obs-resource-2-g1", "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0}),
        envelope(4, "target-view-observed", {"schema": "target-view-observation-v1", "targetViewObservationId": "obs-shader-resource-view-6-g1", "kind": "shader-resource-view", "d3dObjectPointer": "0x6", "pointerGeneration": 1, "resourceObservationId": "obs-resource-2-g1", "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0}),
        envelope(5, "resource-view-bind", {"schema": "resource-view-binding-v1", "viewObservationId": "obs-shader-resource-view-3-g1", "bindingKind": "shader-resource", "stage": "pixel", "slot": 0}),
        envelope(6, "render-target-bind", {"schema": "render-target-binding-v1", "targetBindingObservationId": "obs-target-binding-5-g1", "renderTargetObservationIds": ["obs-render-target-4-g1"], "depthTargetObservationId": None, "identityDetailsAvailable": True}),
        envelope(7, "draw", {"schema": "draw-call-v2", "operation": "draw", "immediateContextPointer": "0x9", "vertexShaderObservationId": None, "pixelShaderObservationId": None, "targetBindingObservationId": "obs-target-binding-5-g1", "arguments": {}}),
        envelope(8, "resource-flow", {"schema": "resource-flow-v1", "operation": "copy-resource", "sourceResourceObservationId": "obs-resource-2-g1", "destinationResourceObservationId": "obs-resource-1-g1", "sourceSubresource": 0, "destinationSubresource": 0}),
        envelope(9, "resource-view-bind", {"schema": "resource-view-binding-v1", "viewObservationId": "obs-shader-resource-view-6-g1", "bindingKind": "shader-resource", "stage": "pixel", "slot": 0}),
        envelope(10, "draw", {"schema": "draw-call-v2", "operation": "draw", "immediateContextPointer": "0x9", "vertexShaderObservationId": None, "pixelShaderObservationId": None, "targetBindingObservationId": "obs-target-binding-5-g1", "arguments": {}}),
        envelope(11, "resource-flow", {"schema": "resource-flow-v1", "operation": "update-subresource", "sourceResourceObservationId": None, "destinationResourceObservationId": "obs-resource-1-g1", "sourceSubresource": 0, "destinationSubresource": 0}),
    ]
    graph = build_graph(tool, manifest, hazard_events)
    assert graph["schema"]["producerVersion"] == "static-semantic-resource-graph-10"
    assert len(graph["nodes"]) == 12, graph["nodes"]
    assert [edge["type"] for edge in graph["edges"]].count("reads") == 2
    assert [edge["type"] for edge in graph["edges"]].count("writes") == 4
    assert [edge["type"] for edge in graph["edges"]].count("owns") == 6
    hazards = [edge["attributes"]["hazard"] for edge in graph["edges"] if edge["type"] == "precedes"]
    assert sorted(hazards) == ["RAW", "WAR", "WAR", "WAW", "WAW"], hazards
    assert all(edge["evidenceClass"] == "correlated" for edge in graph["edges"] if edge["type"] == "precedes")
    assert graph["extensions"]["csx.effectiveStateAdjustments"] == 1
    assert graph["extensions"]["csx.hazardModel"] == "exact-view-subresource-v1"
    assert graph["extensions"]["csx.hazardOverlapFallbacks"] == 0
    assert graph["extensions"]["csx.graphAcyclic"] is True
    assert len(graph["gaps"]) == 1 and graph["gaps"][0]["kind"] == "unsupported-route"
    versions = [node for node in graph["nodes"] if node["attributes"].get("resourceRole") == "content-version"]
    assert len(versions) == 6
    assert any(node["kind"] == "resource-operation" and node["attributes"]["operation"] == "update-subresource" for node in graph["nodes"])

    cpu_manifest = {
        **manifest,
        "clock": {"source": "QueryPerformanceCounter", "frequencyHz": 10_000_000},
    }
    cpu_events = [
        envelope(0, "resource-observed", {
            **resource_base, "resourceObservationId": "obs-resource-40-g1",
            "usage": 3, "cpuAccessFlags": 0x30000,
        }),
        envelope(1, "resource-cpu-access", {
            "schema": "resource-cpu-access-v1", "phase": "map",
            "mapObservationId": "obs-cpu-map-41-g1", "resourceObservationId": "obs-resource-40-g1",
            "subresource": 0, "mapType": "read", "mapTypeValue": 1, "mapFlags": 0,
            "doNotWait": False, "resultHresult": "0x00000000", "succeeded": True,
            "matchedMap": None, "readable": True, "writable": False,
            "durationQpcTicks": 2500, "rowPitch": 256, "depthPitch": 16384,
            "visibilityBoundary": "cpu-readable-after-map-return", "publicationBoundary": None,
        }),
        envelope(2, "resource-cpu-access", {
            "schema": "resource-cpu-access-v1", "phase": "unmap",
            "mapObservationId": "obs-cpu-map-41-g1", "resourceObservationId": "obs-resource-40-g1",
            "subresource": 0, "mapType": "read", "mapTypeValue": 1, "mapFlags": 0,
            "doNotWait": False, "resultHresult": None, "succeeded": True,
            "matchedMap": True, "readable": True, "writable": False,
            "durationQpcTicks": 1000, "rowPitch": 256, "depthPitch": 16384,
            "visibilityBoundary": None, "publicationBoundary": None,
        }),
        envelope(3, "resource-cpu-access", {
            "schema": "resource-cpu-access-v1", "phase": "map",
            "mapObservationId": "obs-cpu-map-42-g1", "resourceObservationId": "obs-resource-40-g1",
            "subresource": 0, "mapType": "write-discard", "mapTypeValue": 4, "mapFlags": 0,
            "doNotWait": False, "resultHresult": "0x00000000", "succeeded": True,
            "matchedMap": None, "readable": False, "writable": True,
            "durationQpcTicks": 500, "rowPitch": 256, "depthPitch": 16384,
            "visibilityBoundary": None, "publicationBoundary": None,
        }),
        envelope(4, "resource-cpu-access", {
            "schema": "resource-cpu-access-v1", "phase": "unmap",
            "mapObservationId": "obs-cpu-map-42-g1", "resourceObservationId": "obs-resource-40-g1",
            "subresource": 0, "mapType": "write-discard", "mapTypeValue": 4, "mapFlags": 0,
            "doNotWait": False, "resultHresult": None, "succeeded": True,
            "matchedMap": True, "readable": False, "writable": True,
            "durationQpcTicks": 3000, "rowPitch": 256, "depthPitch": 16384,
            "visibilityBoundary": None, "publicationBoundary": "gpu-visible-after-unmap-return",
        }),
        envelope(5, "resource-cpu-access", {
            "schema": "resource-cpu-access-v1", "phase": "map",
            "mapObservationId": "obs-cpu-map-43-g1", "resourceObservationId": "obs-resource-40-g1",
            "subresource": 0, "mapType": "read", "mapTypeValue": 1, "mapFlags": 0x100000,
            "doNotWait": True, "resultHresult": "0x887A000A", "succeeded": False,
            "matchedMap": None, "readable": True, "writable": False,
            "durationQpcTicks": 100, "rowPitch": 0, "depthPitch": 0,
            "visibilityBoundary": None, "publicationBoundary": None,
        }),
        envelope(6, "resource-cpu-access", {
            "schema": "resource-cpu-access-v1", "phase": "unmap",
            "mapObservationId": None, "resourceObservationId": "obs-resource-40-g1",
            "subresource": 0, "mapType": "unknown", "mapTypeValue": 0, "mapFlags": 0,
            "doNotWait": False, "resultHresult": None, "succeeded": True,
            "matchedMap": False, "readable": False, "writable": False,
            "durationQpcTicks": 0, "rowPitch": 0, "depthPitch": 0,
            "visibilityBoundary": None, "publicationBoundary": None,
        }),
    ]
    cpu_graph = build_graph(tool, cpu_manifest, cpu_events)
    assert cpu_graph["extensions"]["csx.cpuAccessCount"] == 6
    assert cpu_graph["extensions"]["csx.cpuReadVisibilityCount"] == 1
    assert cpu_graph["extensions"]["csx.cpuWritePublicationCount"] == 1
    assert cpu_graph["extensions"]["csx.cpuMapStallTicks"] == 3000
    assert cpu_graph["extensions"]["csx.cpuMapMaxStallTicks"] == 2500
    assert cpu_graph["extensions"]["csx.cpuMapStallMicroseconds"] == 300.0
    assert sum(edge["type"] == "reads" for edge in cpu_graph["edges"]) == 1
    assert sum(edge["type"] == "writes" for edge in cpu_graph["edges"]) == 1
    assert sum(edge["attributes"].get("hazard") == "CPU-map-lifetime" for edge in cpu_graph["edges"]) == 2
    assert any(gap["kind"] == "incomplete-capture" for gap in cpu_graph["gaps"])

    observed_hazard_events = [
        envelope(0, "resource-observed", {**resource_base, "resourceObservationId": "obs-resource-30-g1"}),
        envelope(1, "target-view-observed", {
            "schema": "target-view-observation-v1",
            "targetViewObservationId": "obs-shader-resource-view-31-g1",
            "kind": "shader-resource-view", "d3dObjectPointer": "0x31",
            "pointerGeneration": 1, "resourceObservationId": "obs-resource-30-g1",
            "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0,
        }),
        envelope(2, "target-view-observed", {
            "schema": "target-view-observation-v1",
            "targetViewObservationId": "obs-render-target-32-g1",
            "kind": "render-target", "d3dObjectPointer": "0x32",
            "pointerGeneration": 1, "resourceObservationId": "obs-resource-30-g1",
            "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0,
        }),
        envelope(3, "resource-view-bind", {
            "schema": "resource-view-binding-v2", "source": "requested-call",
            "viewObservationId": "obs-shader-resource-view-31-g1",
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
        }),
        envelope(4, "resource-view-bind", {
            "schema": "resource-view-binding-v2", "source": "post-call-query",
            "viewObservationId": "obs-shader-resource-view-31-g1",
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
        }),
        envelope(5, "resource-view-state-observed", {
            "schema": "resource-view-state-observed-v1", "source": "post-call-query",
            "bindingKind": "shader-resource", "stage": "pixel", "startSlot": 0,
            "count": 1, "changedSlotCount": 1,
        }),
        envelope(6, "render-target-bind", {
            "schema": "render-target-binding-v2", "source": "observed-call",
            "targetBindingObservationId": "obs-target-binding-33-g1",
            "renderTargetObservationIds": ["obs-render-target-32-g1"],
            "depthTargetObservationId": None, "identityDetailsAvailable": True,
        }),
        envelope(7, "render-target-bind", {
            "schema": "render-target-binding-v2", "source": "post-call-query",
            "targetBindingObservationId": "obs-target-binding-33-g1",
            "renderTargetObservationIds": ["obs-render-target-32-g1"],
            "depthTargetObservationId": None, "identityDetailsAvailable": True,
        }),
        envelope(8, "resource-view-bind", {
            "schema": "resource-view-binding-v2", "source": "post-call-query",
            "viewObservationId": None, "bindingKind": "shader-resource",
            "stage": "pixel", "slot": 0,
        }),
        envelope(9, "resource-view-state-observed", {
            "schema": "resource-view-state-observed-v1", "source": "post-call-query",
            "bindingKind": "shader-resource", "stage": "pixel", "startSlot": 0,
            "count": 1, "changedSlotCount": 1,
        }),
        envelope(10, "draw", {
            "schema": "draw-call-v2", "operation": "draw", "immediateContextPointer": "0x9",
            "vertexShaderObservationId": None, "pixelShaderObservationId": None,
            "targetBindingObservationId": "obs-target-binding-33-g1", "arguments": {},
        }),
    ]
    observed_graph = build_graph(tool, manifest, observed_hazard_events)
    assert observed_graph["extensions"]["csx.hazardModel"] == "observed-post-call-effective-state-v1"
    assert observed_graph["extensions"]["csx.effectiveStateAdjustments"] == 1
    assert observed_graph["extensions"]["csx.effectiveStateQueryCount"] == 2
    assert observed_graph["extensions"]["csx.effectiveStateVerifiedSlots"] == 2
    assert observed_graph["extensions"]["csx.effectiveStateMismatchSlots"] == 0
    assert [edge["type"] for edge in observed_graph["edges"]].count("reads") == 0
    assert observed_graph["gaps"] == []

    subresource_resource = {
        **resource_base, "resourceObservationId": "obs-resource-20-g1",
        "mipLevels": 2,
    }
    disjoint_subresource_events = [
        envelope(0, "resource-observed", subresource_resource),
        envelope(1, "target-view-observed", {
            "schema": "target-view-observation-v1",
            "targetViewObservationId": "obs-shader-resource-view-21-g1",
            "kind": "shader-resource-view", "d3dObjectPointer": "0x21",
            "pointerGeneration": 1, "resourceObservationId": "obs-resource-20-g1",
            "format": 28, "viewDimension": 4,
            "subresources": {"mipSliceOrFirstMip": 0, "firstArraySlice": 0,
                             "arraySizeOrMipCount": 1, "firstElement": 0,
                             "elementCountOrArraySize": 0}, "flags": 0,
        }),
        envelope(2, "target-view-observed", {
            "schema": "target-view-observation-v1",
            "targetViewObservationId": "obs-render-target-22-g1",
            "kind": "render-target", "d3dObjectPointer": "0x22",
            "pointerGeneration": 1, "resourceObservationId": "obs-resource-20-g1",
            "format": 28, "viewDimension": 4,
            "subresources": {"mipSliceOrFirstMip": 1, "firstArraySlice": 0,
                             "arraySizeOrMipCount": 0, "firstElement": 0,
                             "elementCountOrArraySize": 0}, "flags": 0,
        }),
        envelope(3, "resource-view-bind", {
            "schema": "resource-view-binding-v1",
            "viewObservationId": "obs-shader-resource-view-21-g1",
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
        }),
        envelope(4, "render-target-bind", {
            "schema": "render-target-binding-v1",
            "targetBindingObservationId": "obs-target-binding-23-g1",
            "renderTargetObservationIds": ["obs-render-target-22-g1"],
            "depthTargetObservationId": None, "identityDetailsAvailable": True,
        }),
        envelope(5, "draw", {
            "schema": "draw-call-v2", "operation": "draw",
            "immediateContextPointer": "0x9", "vertexShaderObservationId": None,
            "pixelShaderObservationId": None,
            "targetBindingObservationId": "obs-target-binding-23-g1", "arguments": {},
        }),
    ]
    graph = build_graph(tool, manifest, disjoint_subresource_events)
    assert graph["extensions"]["csx.effectiveStateAdjustments"] == 0
    assert graph["extensions"]["csx.hazardOverlapFallbacks"] == 0
    assert [edge["type"] for edge in graph["edges"]].count("reads") == 1

    decision_events = [
        envelope(0, "resource-observed", {**resource_base, "resourceObservationId": "obs-resource-1-g1"}),
        envelope(1, "resource-observed", {**resource_base, "resourceObservationId": "obs-resource-2-g1", "d3dObjectPointer": "0x2"}),
        envelope(2, "target-view-observed", {"schema": "target-view-observation-v1", "targetViewObservationId": "obs-shader-resource-view-3-g1", "kind": "shader-resource-view", "d3dObjectPointer": "0x3", "pointerGeneration": 1, "resourceObservationId": "obs-resource-1-g1", "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0}),
        envelope(3, "target-view-observed", {"schema": "target-view-observation-v1", "targetViewObservationId": "obs-render-target-4-g1", "kind": "render-target", "d3dObjectPointer": "0x4", "pointerGeneration": 1, "resourceObservationId": "obs-resource-2-g1", "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0}),
        envelope(4, "resource-view-bind", {"schema": "resource-view-binding-v1", "viewObservationId": "obs-shader-resource-view-3-g1", "bindingKind": "shader-resource", "stage": "pixel", "slot": 0}),
        envelope(5, "render-target-bind", {"schema": "render-target-binding-v1", "targetBindingObservationId": "obs-target-binding-5-g1", "renderTargetObservationIds": ["obs-render-target-4-g1"], "depthTargetObservationId": None, "identityDetailsAvailable": True}),
        envelope(6, "visibility-candidate", {"schema": "visibility-candidate-v1", "objectIndex": 3, "objectPointer": "0x6", "producerFrame": 1}),
        envelope(7, "resource-version-observed", {"schema": "resource-version-observation-v1", "resourceVersionObservationId": "obs-resource-version-6-g1", "resourceObservationId": "obs-resource-1-g1", "subresources": {"first": 0, "count": 1}, "writeEpoch": 4, "producerFrame": 1, "readinessDomain": "same-immediate-context-order", "eye": "unknown", "eyeMask": None}),
        envelope(8, "visibility-result-ready", {"schema": "visibility-result-ready-v1", "resourceVersionObservationId": "obs-resource-version-6-g1", "viewObservationId": "obs-shader-resource-view-3-g1", "objectCount": 1, "producerFrame": 1}),
        envelope(9, "visibility-consumed", {"schema": "visibility-submission-v1", "submissionObservationId": "obs-submission-7-g1", "renderPassPointer": "0x7", "geometryPointer": "0x8", "objectIndex": 3, "resourceVersionObservationId": "obs-resource-version-6-g1", "requestedViewObservationId": "obs-shader-resource-view-3-g1", "effectiveViewObservationId": "obs-shader-resource-view-3-g1", "category": 1, "slot": 127, "bindingMatches": True, "forcedVisible": False}),
        {**envelope(10, "draw", {"schema": "draw-call-v2", "operation": "draw", "immediateContextPointer": "0x9", "vertexShaderObservationId": None, "pixelShaderObservationId": None, "targetBindingObservationId": "obs-target-binding-5-g1", "submissionObservationId": "obs-submission-7-g1", "arguments": {}}), "submissionObservationId": "obs-submission-7-g1"},
        envelope(11, "resource-flow", {"schema": "resource-flow-v1", "operation": "copy-resource", "sourceResourceObservationId": "obs-resource-2-g1", "destinationResourceObservationId": "obs-resource-1-g1", "sourceSubresource": 0, "destinationSubresource": 0}),
        envelope(12, "cull-decision", {"schema": "cull-decision-v1", "resourceVersionObservationId": "obs-resource-version-6-g1", "objectIndex": 3, "producerVisible": False, "drawCounts": {"total": 1, "lighting": 1, "distantTree": 0, "grass": 0}, "producerFrame": 1, "readinessDomain": "cpu-readback-complete"}),
        envelope(13, "eye-submitted", {"schema": "eye-submission-v1", "resourceObservationId": "obs-resource-1-g1", "eye": "left", "eyeMask": 1, "bounds": {"uMin": 0.0, "vMin": 0.0, "uMax": 0.5, "vMax": 1.0}, "submitFlags": 0, "compositorCycle": 2}),
        envelope(14, "eye-submitted", {"schema": "eye-submission-v1", "resourceObservationId": "obs-resource-1-g1", "eye": "right", "eyeMask": 2, "bounds": {"uMin": 0.5, "vMin": 0.0, "uMax": 1.0, "vMax": 1.0}, "submitFlags": 0, "compositorCycle": 2}),
    ]
    graph = build_graph(tool, manifest, decision_events)
    assert [edge["type"] for edge in graph["edges"]].count("versions") == 1
    assert [edge["type"] for edge in graph["edges"]].count("consumes") == 1
    assert [edge["type"] for edge in graph["edges"]].count("submits") == 1
    assert [edge["type"] for edge in graph["edges"]].count("presents") == 2
    assert [edge["type"] for edge in graph["edges"]].count("contributes-to") == 2
    assert [edge["type"] for edge in graph["edges"]].count("result-for") == 1
    assert [edge["type"] for edge in graph["edges"]].count("tests") == 1
    assert len(graph["decisionWindows"]) == 1
    assert graph["decisionWindows"][0]["result"] == "viable"
    assert graph["decisionWindows"][0]["suppressionStage"] == "vertex-shader"
    assert graph["decisionWindows"][0]["visibilityAvailable"]["readinessDomain"] == "gpu-resource-consumable"
    eye_coverage = graph["decisionWindows"][0]["eyeCoverage"]
    assert eye_coverage["result"] == "observed", eye_coverage
    assert eye_coverage["eyes"] == ["left", "right"]
    assert eye_coverage["physicalSubmissionCount"] == 2
    assert eye_coverage["stereoMechanism"] == "shared-resource-distinct-bounds"
    assert eye_coverage["searchTruncated"] is False
    assert len(eye_coverage["routes"]) == 2
    assert all(route["mechanism"] == "resource-flow" for route in eye_coverage["routes"])
    assert all(route["resourceObservationIds"] == ["obs-resource-2-g1", "obs-resource-1-g1"] for route in eye_coverage["routes"])
    assert graph["ambiguities"] == []
    assert graph["extensions"]["csx.vrEyeAttribution"] is True
    assert len(graph["gaps"]) == 1 and graph["gaps"][0]["kind"] == "unsupported-route"

    overwritten_events = json.loads(json.dumps(decision_events))
    overwrite = next(event for event in overwritten_events if event["sequence"] == 11)
    overwrite["payload"] = {
        "schema": "resource-flow-v1", "operation": "update-subresource",
        "sourceResourceObservationId": None, "destinationResourceObservationId": "obs-resource-2-g1",
        "sourceSubresource": 0, "destinationSubresource": 0,
    }
    for event in overwritten_events:
        if event["type"] == "eye-submitted":
            event["payload"]["resourceObservationId"] = "obs-resource-2-g1"
    overwritten_graph = build_graph(tool, manifest, overwritten_events)
    assert overwritten_graph["decisionWindows"][0]["eyeCoverage"]["result"] == "not-proven"
    assert [edge["type"] for edge in overwritten_graph["edges"]].count("contributes-to") == 0

    unmatched_events = [
        event for event in decision_events
        if event["type"] not in {"visibility-consumed", "draw"}
    ]
    unmatched_graph = build_graph(tool, manifest, unmatched_events)
    assert unmatched_graph["decisionWindows"][0]["result"] == "not-proven"
    assert unmatched_graph["decisionWindows"][0]["eyeCoverage"]["result"] == "not-proven"

    semantic_events = [
        envelope(0, "shader-observed", {"schema": "shader-observation-v2", "shaderObservationId": "obs-shader-1-g1", "shaderPointer": "0x10", "pointerGeneration": 1, "shaderType": 6, "fxpFilename": "Lighting", "imageSpaceName": None, "compileSourceName": "Lighting", "definesSuffix": "ABC", "identityDetailsAvailable": True}),
        envelope(1, "render-pass-enter", {"schema": "render-pass-boundary-v1", "renderPassPointer": "0x11", "geometryPointer": "0x12", "passEnum": 33, "technique": 33, "renderFlags": 1, "alphaTest": False}),
        envelope(2, "technique-begin", {"schema": "technique-boundary-v2", "shaderObservationId": "obs-shader-1-g1", "shaderPointer": "0x10", "shaderType": 6, "callerRva": "0x1337D7B", "vertexDescriptor": 1, "pixelDescriptor": 2, "skipPixelShader": False}),
        envelope(3, "stage-shader-observed", {"schema": "stage-shader-observation-v1", "stageShaderObservationId": "obs-vertex-shader-2-g1", "stage": "vertex", "d3dObjectPointer": "0x13", "wrapperPointer": "0x14", "pointerGeneration": 1, "wrapperDescriptor": 1, "bytecodeSize": 32, "bytecodeSha256": "A" * 64, "cachePath": "ShaderCache/Lighting/1.vso", "identityDetailsAvailable": True}),
        envelope(4, "stage-shader-observed", {"schema": "stage-shader-observation-v1", "stageShaderObservationId": "obs-pixel-shader-3-g1", "stage": "pixel", "d3dObjectPointer": "0x15", "wrapperPointer": "0x16", "pointerGeneration": 1, "wrapperDescriptor": 2, "bytecodeSize": 32, "bytecodeSha256": "B" * 64, "cachePath": "ShaderCache/Lighting/2.pso", "identityDetailsAvailable": True}),
        envelope(5, "technique-resolved", {"schema": "technique-resolution-v1", "inputVertexDescriptor": 1, "inputPixelDescriptor": 2, "resolvedVertexDescriptor": 1, "resolvedPixelDescriptor": 2, "vertexShaderObservationId": "obs-vertex-shader-2-g1", "pixelShaderObservationId": "obs-pixel-shader-3-g1", "vertexRoute": "csx-cache", "pixelRoute": "csx-cache", "shaderFound": True, "skipPixelShader": False}),
        envelope(6, "geometry-setup-begin", {"schema": "geometry-boundary-v1", "geometryPointer": "0x12", "renderPassPointer": "0x11", "shaderPointer": "0x10", "shaderType": 6, "passEnum": 33, "renderFlags": 1}),
        envelope(7, "draw", {"schema": "draw-call-v2", "operation": "draw-indexed", "immediateContextPointer": "0x17", "vertexShaderObservationId": "obs-vertex-shader-2-g1", "pixelShaderObservationId": "obs-pixel-shader-3-g1", "targetBindingObservationId": None, "arguments": {"indexCount": 36, "startIndexLocation": 0, "baseVertexLocation": 0}}),
    ]
    for event in semantic_events:
        if event["sequence"] >= 1:
            event["scopes"]["renderPass"] = "obs-render-pass-4-g1"
        if event["type"] in {"technique-begin", "technique-resolved", "draw"}:
            event["scopes"]["technique"] = "obs-technique-5-g1"
        if event["type"] in {"geometry-setup-begin", "draw"}:
            event["scopes"]["geometry"] = "obs-geometry-6-g1"
    semantic_graph = build_graph(tool, manifest, semantic_events)
    kinds = [node["kind"] for node in semantic_graph["nodes"]]
    assert kinds.count("engine-shader") == 1
    assert kinds.count("render-pass") == 1
    assert kinds.count("technique") == 1
    assert kinds.count("pipeline-state") == 2
    assert kinds.count("geometry") == 0
    assert kinds.count("geometry-setup") == 1
    assert kinds.count("draw") == 1
    edge_types = [edge["type"] for edge in semantic_graph["edges"]]
    assert edge_types.count("selects") == 4
    assert edge_types.count("uses") == 1
    assert edge_types.count("draws") == 3
    assert edge_types.count("binds") == 2
    assert semantic_graph["extensions"]["csx.graphAcyclic"] is True

    object_material_events = [
        envelope(0, "object-observed", {
            "schema": "scene-object-observation-v1",
            "sceneObjectObservationId": "obs-scene-object-1-g1", "referencePointer": "0x30",
            "pointerGeneration": 1, "referenceFormId": "0x0000003C", "baseFormId": "0x00000007",
            "referenceName": "Whiterun bench", "baseFormName": "Bench", "referenceFormDynamic": False,
            "baseFormDynamic": False, "truncated": {"referenceName": False, "baseFormName": False},
            "identityDetailsAvailable": True,
        }),
        envelope(1, "geometry-observed", {
            "schema": "geometry-observation-v1",
            "geometryObservationId": "obs-geometry-object-2-g1", "geometryPointer": "0x31",
            "pointerGeneration": 1, "runtimeTypeName": "BSTriShape", "name": "Bench01:0",
            "geometryType": 0, "vertexDescriptor": 4660,
            "sceneObjectObservationId": "obs-scene-object-1-g1",
            "worldTransform": [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 10.0, 20.0, 30.0, 1.0],
            "worldBound": [10.0, 20.0, 30.0, 4.0],
            "truncated": {"runtimeTypeName": False, "name": False}, "identityDetailsAvailable": True,
        }),
        envelope(2, "material-observed", {
            "schema": "material-state-observation-v1",
            "materialStateObservationId": "obs-material-state-3-g1", "stateRevision": 2,
            "fingerprint": "0x0123456789ABCDEF", "shaderPropertyPointer": "0x32",
            "shaderPropertyRuntimeTypeName": "BSLightingShaderProperty", "shaderPropertyFlags": 17,
            "alpha": 1.0, "engineMaterialType": 1, "materialPointer": "0x33", "materialType": 2,
            "feature": 3, "hashKey": 4, "shaderPropertyAvailable": True, "materialAvailable": True,
            "truncated": {"shaderPropertyRuntimeTypeName": False}, "identityDetailsAvailable": True,
        }),
        envelope(3, "render-pass-enter", {
            "schema": "render-pass-boundary-v1", "renderPassPointer": "0x34", "geometryPointer": "0x31",
            "technique": 9, "passEnum": 9, "renderFlags": 0, "alphaTest": False,
        }),
        envelope(4, "geometry-setup-begin", {
            "schema": "geometry-boundary-v2", "geometryPointer": "0x31", "renderPassPointer": "0x34",
            "shaderPointer": "0x35", "shaderType": 6, "passEnum": 9, "renderFlags": 0,
            "geometryObservationId": "obs-geometry-object-2-g1",
            "materialStateObservationId": "obs-material-state-3-g1",
        }),
        envelope(5, "draw", {
            "schema": "draw-call-v2", "operation": "draw-indexed", "immediateContextPointer": "0x36",
            "vertexShaderObservationId": None, "pixelShaderObservationId": None,
            "targetBindingObservationId": None, "arguments": {"indexCount": 6},
        }),
    ]
    for event in object_material_events:
        if event["sequence"] >= 3:
            event["scopes"]["renderPass"] = "obs-render-pass-4-g1"
        if event["sequence"] >= 4:
            event["scopes"]["geometry"] = "obs-geometry-scope-5-g1"
    object_material_graph = build_graph(tool, manifest, object_material_events)
    object_material_kinds = [node["kind"] for node in object_material_graph["nodes"]]
    assert object_material_kinds.count("scene-object") == 1
    assert object_material_kinds.count("material") == 1
    assert object_material_kinds.count("geometry") == 1
    assert object_material_kinds.count("geometry-setup") == 1
    object_material_edges = [edge["type"] for edge in object_material_graph["edges"]]
    assert object_material_edges.count("represented-by") == 1
    assert object_material_edges.count("uses-material-state") == 1
    assert object_material_edges.count("same-observed-object") == 4
    projected_edges = [
        edge for edge in object_material_graph["edges"]
        if edge["type"] == "same-observed-object" and edge["to"].startswith("node-draw-")
    ]
    assert sorted(edge["attributes"]["projection"] for edge in projected_edges) == [
        "geometry", "material-state", "scene-object",
    ]
    assert all(edge["evidence"][0]["eventSequences"][-1] == 5 for edge in projected_edges)
    assert object_material_graph["extensions"]["csx.graphAcyclic"] is True

    material_texture_events = [
        envelope(0, "resource-observed", {
            **resource_base, "resourceObservationId": "obs-resource-20-g1",
            "d3dObjectPointer": "0x200",
        }),
        envelope(1, "material-observed", {
            **object_material_events[2]["payload"],
            "materialStateObservationId": "obs-material-state-21-g1",
            "textureBindings": [{
                "role": "runtime-material-list", "bindingIndex": 0,
                "niSourceTexturePointer": "0x201", "path": "textures\\architecture\\whiterun\\wrwood.dds",
                "resourceObservationId": "obs-resource-20-g1", "pathTruncated": False,
            }],
            "textureBindingsTruncated": False,
        }),
    ]
    material_texture_graph = build_graph(tool, manifest, material_texture_events)
    material_texture_kinds = [node["kind"] for node in material_texture_graph["nodes"]]
    assert material_texture_kinds.count("material") == 1
    assert material_texture_kinds.count("material-texture-binding") == 1
    assert material_texture_kinds.count("resource") == 1
    material_texture_edges = [edge["type"] for edge in material_texture_graph["edges"]]
    assert material_texture_edges.count("binds-texture") == 1
    assert material_texture_edges.count("resolves-to-resource") == 1
    texture_binding_node = next(
        node for node in material_texture_graph["nodes"]
        if node["kind"] == "material-texture-binding"
    )
    assert texture_binding_node["attributes"]["bindingIndexSemantics"] == \
        "runtime-material-list-position-not-shader-register"

    material_execution_events = [
        envelope(0, "resource-observed", {
            **resource_base, "resourceObservationId": "obs-resource-30-g1",
            "d3dObjectPointer": "0x300",
        }),
        envelope(1, "target-view-observed", {
            "schema": "target-view-observation-v1",
            "targetViewObservationId": "obs-shader-resource-view-31-g1",
            "kind": "shader-resource-view", "d3dObjectPointer": "0x301", "pointerGeneration": 1,
            "resourceObservationId": "obs-resource-30-g1", "format": 28, "viewDimension": 4,
            "subresources": {}, "flags": 0,
        }),
        envelope(2, "scene-object-observed", {
            **object_material_events[0]["payload"],
            "sceneObjectObservationId": "obs-scene-object-32-g1",
        }),
        envelope(3, "geometry-observed", {
            **object_material_events[1]["payload"],
            "geometryObservationId": "obs-geometry-object-33-g1",
            "sceneObjectObservationId": "obs-scene-object-32-g1",
        }),
        envelope(4, "material-observed", {
            **object_material_events[2]["payload"],
            "materialStateObservationId": "obs-material-state-34-g1",
            "textureBindings": [{
                "role": "runtime-material-list", "bindingIndex": 0,
                "niSourceTexturePointer": "0x302", "path": "textures\\architecture\\whiterun\\wrwood.dds",
                "resourceObservationId": "obs-resource-30-g1", "pathTruncated": False,
            }],
            "textureBindingsTruncated": False,
        }),
        envelope(5, "render-pass-enter", {
            **object_material_events[3]["payload"],
            "geometryPointer": "0x31",
        }),
        envelope(6, "geometry-setup-begin", {
            **object_material_events[4]["payload"],
            "geometryObservationId": "obs-geometry-object-33-g1",
            "materialStateObservationId": "obs-material-state-34-g1",
        }),
        envelope(7, "resource-view-bind", {
            "schema": "geometry-boundary-v2", "geometryPointer": "0x31", "renderPassPointer": "0x34",
            "shaderPointer": "0x35", "shaderType": 6, "passEnum": 9, "renderFlags": 0,
            "geometryObservationId": "obs-geometry-object-33-g1",
            "materialStateObservationId": "obs-material-state-34-g1",
        }),
        envelope(8, "resource-view-bind", {
            "schema": "resource-view-binding-v1",
            "viewObservationId": "obs-shader-resource-view-31-g1",
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 3,
        }),
        envelope(9, "draw", {
            **object_material_events[5]["payload"],
            "immediateContextPointer": "0x36",
            "schema": "draw-call-v3",
            "preparedGeometrySetupObservationId": "obs-geometry-scope-36-g1",
        }),
    ]
    material_execution_events[7]["type"] = "geometry-setup-end"
    for event in material_execution_events:
        if event["sequence"] >= 5:
            event["scopes"]["renderPass"] = "obs-render-pass-35-g1"
        if event["sequence"] in {6, 7}:
            event["scopes"]["geometry"] = "obs-geometry-scope-36-g1"
    material_execution_graph = build_graph(tool, manifest, material_execution_events)
    material_execution_reads = [
        edge for edge in material_execution_graph["edges"]
        if edge["type"] == "reads" and edge["to"].startswith("node-draw-")
    ]
    assert len(material_execution_reads) == 1
    material_matches = material_execution_reads[0]["attributes"]["materialInputMatches"]
    assert len(material_matches) == 1
    assert material_matches[0]["role"] == "runtime-material-list"
    assert material_matches[0]["bindingIndex"] == 0
    assert material_matches[0]["stage"] == "pixel"
    assert material_matches[0]["slot"] == 3
    assert material_matches[0]["viewObservationId"] == "obs-shader-resource-view-31-g1"
    assert material_matches[0]["resourceObservationId"] == "obs-resource-30-g1"
    assert material_matches[0]["materialTextureBindingNodeId"].startswith(
        "node-material-texture-binding-"
    )
    assert material_execution_graph["extensions"]["csx.materialInputExecutionCorrelation"] is True
    assert material_execution_graph["extensions"]["csx.materialInputMatchCount"] == 1
    assert material_execution_graph["extensions"]["csx.preparedGeometryDrawCount"] == 1
    prepare_edges = [
        edge for edge in material_execution_graph["edges"]
        if edge["type"] == "prepares" and edge["to"].startswith("node-draw-")
    ]
    assert len(prepare_edges) == 1
    assert prepare_edges[0]["attributes"]["associationBasis"] == \
        "same-thread-next-immediate-context-draw"
    prepared_projection_edges = [
        edge for edge in material_execution_graph["edges"]
        if edge["type"] == "same-observed-object" and edge["to"].startswith("node-draw-")
    ]
    assert all(
        edge["attributes"]["associationBasis"] == "same-thread-next-immediate-context-draw"
        for edge in prepared_projection_edges
    )
    assert material_execution_graph["extensions"]["csx.samplerStateCoverage"] is False

    missing_texture_resource_graph = build_graph(tool, manifest, material_texture_events[1:])
    assert any(
        gap["blocking"] and "refers to undeclared resource" in gap["description"]
        for gap in missing_texture_resource_graph["gaps"]
    )

    missing_material_graph = build_graph(
        tool, manifest, [event for event in object_material_events if event["type"] != "material-observed"]
    )
    assert any(
        gap["blocking"] and "undeclared material-state observation" in gap["description"]
        for gap in missing_material_graph["gaps"]
    )
    missing_object_graph = build_graph(
        tool, manifest, [event for event in object_material_events if event["type"] != "object-observed"]
    )
    assert any(
        gap["blocking"] and "undeclared scene object" in gap["description"]
        for gap in missing_object_graph["gaps"]
    )

    revision_events = json.loads(json.dumps(object_material_events))
    revision_events.extend([
        envelope(6, "material-observed", {
            **object_material_events[2]["payload"],
            "materialStateObservationId": "obs-material-state-6-g1", "stateRevision": 3,
            "fingerprint": "0xFEDCBA9876543210", "hashKey": 5,
        }),
        envelope(7, "geometry-setup-begin", {
            **object_material_events[4]["payload"],
            "materialStateObservationId": "obs-material-state-6-g1",
        }),
        envelope(8, "draw", {**object_material_events[5]["payload"]}),
    ])
    revision_events[7]["scopes"]["renderPass"] = "obs-render-pass-4-g1"
    revision_events[7]["scopes"]["geometry"] = "obs-geometry-scope-7-g1"
    revision_events[8]["scopes"]["renderPass"] = "obs-render-pass-4-g1"
    revision_events[8]["scopes"]["geometry"] = "obs-geometry-scope-7-g1"
    revision_graph = build_graph(tool, manifest, revision_events)
    revision_materials = {node["id"] for node in revision_graph["nodes"] if node["kind"] == "material"}
    revision_draw_materials = {
        edge["from"] for edge in revision_graph["edges"]
        if edge["type"] == "same-observed-object" and edge["to"].startswith("node-draw-")
        and edge["attributes"].get("projection") == "material-state"
    }
    assert len(revision_materials) == 2 and revision_draw_materials == revision_materials

    static_shader_manifest = {
        "schemaVersion": 2,
        "compileUnits": [
            {
                "id": "compile-0074", "kind": "engine-shader-cache-family",
                "sourceVirtualPath": "Effect.hlsl", "sourcePath": "package/Shaders/Effect.hlsl",
                "owner": "core", "entryPoint": "main",
                "defineMode": "engine-descriptor-plus-resident-feature-set",
                "dependencyClosure": ["Common/VR.hlsli", "Effect.hlsl"],
            },
            {
                "id": "compile-0088", "kind": "engine-shader-cache-family",
                "sourceVirtualPath": "ISHDR.hlsl", "sourcePath": "package/Shaders/ISHDR.hlsl",
                "owner": "core", "entryPoint": "main",
                "defineMode": "engine-descriptor-plus-resident-feature-set",
                "dependencyClosure": ["Common/VR.hlsli", "ISHDR.hlsl"],
            },
        ],
    }
    static_engine_map = {
        "schema": {"major": 1},
        "entities": [{
            "id": "engine-technique-0004", "kind": "technique",
            "name": "Effect descriptor pair 0x42/0x42",
            "attributes": {
                "shaderType": "Effect", "techniqueId": "vertex=0x42,pixel=0x42",
                "vertexFlags": ["TexCoord", "Texture"],
                "pixelFlags": ["TexCoord", "Texture"],
            },
        }],
    }
    static_events = [
        envelope(0, "stage-shader-observed", {
            "schema": "stage-shader-observation-v3",
            "stageShaderObservationId": "obs-vertex-shader-1-g1", "stage": "vertex",
            "d3dObjectPointer": "0x20", "wrapperPointer": "0x21", "pointerGeneration": 1,
            "wrapperDescriptor": 66, "bytecodeSize": 32, "bytecodeSha256": "C" * 64,
            "cachePath": "Data/ShaderCache/Effect/42.vso", "identityDetailsAvailable": True,
            "engineAliases": [{"loaderType": "Effect", "compileSourceName": "Effect", "descriptor": 66, "truncated": {"loaderType": False, "compileSourceName": False}}],
        }),
        envelope(1, "stage-shader-observed", {
            "schema": "stage-shader-observation-v3",
            "stageShaderObservationId": "obs-pixel-shader-2-g1", "stage": "pixel",
            "d3dObjectPointer": "0x22", "wrapperPointer": "0x23", "pointerGeneration": 1,
            "wrapperDescriptor": 66, "bytecodeSize": 32, "bytecodeSha256": "D" * 64,
            "cachePath": "Data/ShaderCache/Effect/42.pso", "identityDetailsAvailable": True,
            "engineAliases": [{"loaderType": "Effect", "compileSourceName": "Effect", "descriptor": 66, "truncated": {"loaderType": False, "compileSourceName": False}}],
        }),
        envelope(2, "draw", {
            "schema": "draw-call-v2", "operation": "draw-indexed-instanced",
            "immediateContextPointer": "0x24",
            "vertexShaderObservationId": "obs-vertex-shader-1-g1",
            "pixelShaderObservationId": "obs-pixel-shader-2-g1",
            "targetBindingObservationId": None, "arguments": {},
        }),
        envelope(3, "stage-shader-observed", {
            "schema": "stage-shader-observation-v3",
            "stageShaderObservationId": "obs-pixel-shader-3-g1", "stage": "pixel",
            "d3dObjectPointer": "0x25", "wrapperPointer": "0x26", "pointerGeneration": 1,
            "wrapperDescriptor": 58, "bytecodeSize": 32, "bytecodeSha256": "E" * 64,
            "cachePath": "Data/ShaderCache/ISHDRDownSample4/3A.pso", "identityDetailsAvailable": True,
            "engineAliases": [{"loaderType": "ISHDRDownSample4", "compileSourceName": "ISHDR", "descriptor": 58, "truncated": {"loaderType": False, "compileSourceName": False}}],
        }),
    ]
    static_capture_manifest = json.loads(json.dumps(manifest))
    static_capture_manifest["schema"]["minor"] = 6
    static_capture_manifest["extensions"] = {
        "csx.shaderCompilation": {
            "availability": "observed", "evidenceClass": "runtime-observed",
            "capturedAt": "capture-start", "shaderCacheAbiId": "abi-test",
            "shaderCompilerIdentity": "compiler-test", "compileMode": "optimized",
            "virtualReality": True,
            "compilerFlags": {"partialPrecision": False, "avoidFlowControl": False},
            "globalDefines": {"canonicalText": "", "cachePathSuffix": ""},
            "globalCompileState": {
                "algorithm": "xxh3-128", "digest": "1" * 32,
                "identityBasis": ["shader-cache-abi", "global-defines"],
            },
            "compatibilityRegistry": {
                "phase": "frozen", "revision": 2, "registrationCount": 2,
                "compatibilitySetDigest": "2" * 64, "complete": True,
                "registrations": [
                    {
                        "handle": 1, "identity": "global-provider", "owner": "test",
                        "displayVersion": "1", "contractMajor": 1, "currentMinor": 0,
                        "minimumCompatibleMinor": 0, "maximumCompatibleMinor": 0,
                        "resourceFingerprint": "global", "canonical": "global-canonical",
                        "canonicalDigest": "3" * 64,
                        "scopes": [{"kind": "global", "value": ""}],
                    },
                    {
                        "handle": 2, "identity": "water-provider", "owner": "test",
                        "displayVersion": "1", "contractMajor": 1, "currentMinor": 0,
                        "minimumCompatibleMinor": 0, "maximumCompatibleMinor": 0,
                        "resourceFingerprint": "water", "canonical": "water-canonical",
                        "canonicalDigest": "4" * 64,
                        "scopes": [{"kind": "shader-family", "value": "water"}],
                    },
                ],
            },
            "qualification": "test",
        }
    }
    static_graph = build_graph(
        tool, static_capture_manifest, static_events, static_shader_manifest, static_engine_map
    )
    compile_nodes = [node for node in static_graph["nodes"] if node["kind"] == "shader-compile-unit"]
    technique_nodes = [node for node in static_graph["nodes"] if node["kind"] == "technique"]
    assert {node["attributes"]["compileUnitId"] for node in compile_nodes} == {
        "compile-0074", "compile-0088"
    }
    effect_compile = next(
        node for node in compile_nodes if node["attributes"]["compileUnitId"] == "compile-0074"
    )
    assert effect_compile["sourceRefs"] == [{"kind": "shader-manifest", "value": "compile-0074"}]
    assert len(technique_nodes) == 1
    assert technique_nodes[0]["attributes"]["engineEntityId"] == "engine-technique-0004"
    assert technique_nodes[0]["sourceRefs"] == [{"kind": "engine-map", "value": "engine-technique-0004"}]
    static_edges = [edge["type"] for edge in static_graph["edges"]]
    assert static_edges.count("implemented-by") == 3
    assert static_edges.count("compiled-under") == 3
    assert static_edges.count("selects") == 2
    assert static_edges.count("draws") == 1
    assert static_graph["ambiguities"] == []
    compile_context_nodes = [
        node for node in static_graph["nodes"] if node["kind"] == "shader-compilation-context"
    ]
    assert len(compile_context_nodes) == 1
    assert compile_context_nodes[0]["sourceRefs"] == [
        {"kind": "capture-manifest", "value": "extensions.csx.shaderCompilation"}
    ]
    compiled_under = [edge for edge in static_graph["edges"] if edge["type"] == "compiled-under"]
    assert all(edge["attributes"]["compatibilityRegistrationIdentities"] == ["global-provider"] for edge in compiled_under)
    expected_requirement = hashlib.sha256(b"16:global-canonical\n").hexdigest()
    assert all(edge["attributes"]["compatibilityRequirementSha256"] == expected_requirement for edge in compiled_under)
    image_compile_node = next(
        node for node in compile_nodes if node["attributes"]["compileUnitId"] == "compile-0088"
    )
    image_join = next(
        edge for edge in static_graph["edges"]
        if edge["type"] == "implemented-by" and edge["to"] == image_compile_node["id"]
    )
    assert image_join["attributes"]["loaderType"] == "ISHDRDownSample4"
    assert image_join["attributes"]["compileSourceName"] == "ISHDR"
    assert image_join["attributes"]["joinBasis"] == "compile-source"
    static_inputs = {item["kind"]: item for item in static_graph["inputs"]}
    assert static_inputs["shader-manifest"]["schemaMajor"] == 2
    assert static_inputs["engine-map"]["schemaMajor"] == 1

    command_events = [
        envelope(0, "device-context-observed", {
            "schema": "device-context-observation-v2",
            "deviceContextObservationId": "obs-device-context-1-g1",
            "contextPointer": "0x100", "pointerGeneration": 1,
            "kind": "deferred", "creationEvidence": "create-deferred-context-hook",
            "contextFlags": 0,
        }),
        {**envelope(1, "command-recording-observed", {
            "schema": "command-recording-observation-v1",
            "commandRecordingObservationId": "obs-command-recording-2-g1",
            "deviceContextObservationId": "obs-device-context-1-g1",
            "epoch": 1, "partialAtCaptureStart": False,
        }), "commandRecordingObservationId": "obs-command-recording-2-g1"},
        {**envelope(2, "draw", {
            "schema": "draw-call-v4", "operation": "draw",
            "deviceContextPointer": "0x100", "vertexShaderObservationId": None,
            "pixelShaderObservationId": None, "targetBindingObservationId": None,
            "submissionObservationId": None, "preparedGeometrySetupObservationId": None,
            "arguments": {"vertexCount": 3, "startVertexLocation": 0},
        }), "commandRecordingObservationId": "obs-command-recording-2-g1",
            "execution": {"observationDomain": "command-recording", "commandStreamSequence": 1,
                          "gpuTimestampTicks": None, "gpuTimestampFrequencyHz": None}},
        {**envelope(3, "command-list-observed", {
            "schema": "command-list-observation-v2",
            "commandListObservationId": "obs-command-list-3-g1",
            "commandListPointer": "0x200", "pointerGeneration": 1,
            "sourceDeviceContextObservationId": "obs-device-context-1-g1",
            "sourceCommandRecordingObservationId": "obs-command-recording-2-g1",
            "sourceRecordingComplete": True,
            "sourceRecordingIncompleteReasons": [],
        }), "commandRecordingObservationId": "obs-command-recording-2-g1"},
        {**envelope(4, "finish-command-list", {
            "schema": "finish-command-list-v2",
            "commandRecordingObservationId": "obs-command-recording-2-g1",
            "commandListObservationId": "obs-command-list-3-g1",
            "commandListPointer": "0x200", "restoreDeferredContextState": False,
            "hresult": 0, "succeeded": True,
            "sourceRecordingComplete": True,
            "sourceRecordingIncompleteReasons": [],
        }), "commandRecordingObservationId": "obs-command-recording-2-g1"},
        {**envelope(5, "device-context-observed", {
            "schema": "device-context-observation-v2",
            "deviceContextObservationId": "obs-device-context-4-g1",
            "contextPointer": "0x101", "pointerGeneration": 1,
            "kind": "immediate", "creationEvidence": "initial-immediate-context",
            "contextFlags": 0,
        }), "deviceContextObservationId": "obs-device-context-4-g1"},
        {**envelope(6, "execute-command-list", {
            "schema": "execute-command-list-v1",
            "commandListObservationId": "obs-command-list-3-g1",
            "commandListPointer": "0x200",
            "sourceCommandRecordingObservationId": "obs-command-recording-2-g1",
            "restoreContextState": False,
        }), "deviceContextObservationId": "obs-device-context-4-g1"},
    ]
    command_graph = build_graph(tool, manifest, command_events)
    command_kinds = [node["kind"] for node in command_graph["nodes"]]
    command_edges = [edge["type"] for edge in command_graph["edges"]]
    assert command_kinds.count("device-context") == 2
    assert command_kinds.count("command-recording") == 1
    assert command_kinds.count("command-list") == 1
    assert command_kinds.count("command-list-finish") == 1
    assert command_kinds.count("command-execution") == 1
    assert command_edges.count("records") == 2
    assert command_edges.count("finishes") == 1
    assert command_edges.count("materializes") == 1
    assert command_edges.count("executes") == 1
    assert any(
        "immediate-context state was deliberately not applied" in gap["description"]
        for gap in command_graph["gaps"]
    )

    def deferred_execute_events(start: int, restore_context_state: bool) -> list[dict]:
        context_id = "obs-device-context-70-g1"
        immediate_context_id = "obs-device-context-73-g1"
        recording_id = "obs-command-recording-71-g1"
        list_id = "obs-command-list-72-g1"
        context = envelope(start, "device-context-observed", {
            "schema": "device-context-observation-v2",
            "deviceContextObservationId": context_id,
            "contextPointer": "0x700", "pointerGeneration": 1,
            "kind": "deferred", "creationEvidence": "create-deferred-context-hook",
            "contextFlags": 0,
        })
        context["deviceContextObservationId"] = context_id
        recording = envelope(start + 1, "command-recording-observed", {
            "schema": "command-recording-observation-v1",
            "commandRecordingObservationId": recording_id,
            "deviceContextObservationId": context_id,
            "epoch": 1, "partialAtCaptureStart": False,
        })
        recording["deviceContextObservationId"] = context_id
        recording["commandRecordingObservationId"] = recording_id
        command_list = envelope(start + 2, "command-list-observed", {
            "schema": "command-list-observation-v2",
            "commandListObservationId": list_id,
            "commandListPointer": "0x720", "pointerGeneration": 1,
            "sourceDeviceContextObservationId": context_id,
            "sourceCommandRecordingObservationId": recording_id,
            "sourceRecordingComplete": True,
            "sourceRecordingIncompleteReasons": [],
        })
        command_list["deviceContextObservationId"] = context_id
        command_list["commandRecordingObservationId"] = recording_id
        finish = envelope(start + 3, "finish-command-list", {
            "schema": "finish-command-list-v2",
            "commandRecordingObservationId": recording_id,
            "commandListObservationId": list_id,
            "commandListPointer": "0x720", "restoreDeferredContextState": False,
            "hresult": 0, "succeeded": True,
            "sourceRecordingComplete": True,
            "sourceRecordingIncompleteReasons": [],
        })
        finish["deviceContextObservationId"] = context_id
        finish["commandRecordingObservationId"] = recording_id
        immediate_context = envelope(start + 4, "device-context-observed", {
            "schema": "device-context-observation-v2",
            "deviceContextObservationId": immediate_context_id,
            "contextPointer": "0x701", "pointerGeneration": 1,
            "kind": "immediate", "creationEvidence": "initial-immediate-context",
            "contextFlags": 0,
        })
        immediate_context["deviceContextObservationId"] = immediate_context_id
        execute = envelope(start + 5, "execute-command-list", {
            "schema": "execute-command-list-v1",
            "commandListObservationId": list_id,
            "commandListPointer": "0x720",
            "sourceCommandRecordingObservationId": recording_id,
            "restoreContextState": restore_context_state,
        })
        execute["deviceContextObservationId"] = immediate_context_id
        return [context, recording, command_list, finish, immediate_context, execute]

    def event_resource_edges(graph_under_test: dict, sequence: int) -> list[dict]:
        event_node = next(
            node for node in graph_under_test["nodes"]
            if node["attributes"].get("eventSequence") == sequence
            and node["kind"] in {"draw", "dispatch"}
        )
        return [
            edge for edge in graph_under_test["edges"]
            if edge["type"] in {"reads", "writes", "precedes"}
            and event_node["id"] in {edge["from"], edge["to"]}
        ]

    stale_draw = envelope(13, "draw", {
        "schema": "draw-call-v2", "operation": "draw", "immediateContextPointer": "0x9",
        "vertexShaderObservationId": None, "pixelShaderObservationId": None,
        "targetBindingObservationId": None, "arguments": {},
    })
    restore_false_prefix = json.loads(json.dumps(hazard_events[:7])) + deferred_execute_events(7, False)
    restore_false_draw_graph = build_graph(tool, manifest, restore_false_prefix + [stale_draw])
    assert event_resource_edges(restore_false_draw_graph, 13) == []
    assert any(
        "reset to D3D11 defaults" in gap["description"]
        for gap in restore_false_draw_graph["gaps"]
    )

    stale_dispatch_prefix = json.loads(json.dumps(hazard_events[:6]))
    stale_dispatch_prefix.append(envelope(6, "resource-view-bind", {
        "schema": "resource-view-binding-v1",
        "viewObservationId": "obs-shader-resource-view-3-g1",
        "bindingKind": "shader-resource", "stage": "compute", "slot": 0,
    }))
    stale_dispatch = envelope(13, "dispatch", {
        "schema": "dispatch-call-v2", "operation": "dispatch",
        "deviceContextPointer": "0x9", "computeShaderObservationId": None,
        "arguments": {"threadGroupCountX": 1, "threadGroupCountY": 1, "threadGroupCountZ": 1},
    })
    restore_false_dispatch_graph = build_graph(
        tool, manifest, stale_dispatch_prefix + deferred_execute_events(7, False) + [stale_dispatch]
    )
    assert event_resource_edges(restore_false_dispatch_graph, 13) == []

    partial_rebind = envelope(13, "resource-view-bind", {
        "schema": "resource-view-binding-v1",
        "viewObservationId": "obs-shader-resource-view-3-g1",
        "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
    })
    partial_draw = json.loads(json.dumps(stale_draw))
    partial_draw["sequence"] = 14
    partial_draw["timestampQpc"] = 1014
    partial_graph = build_graph(
        tool, manifest, restore_false_prefix + [partial_rebind, partial_draw]
    )
    partial_edges = event_resource_edges(partial_graph, 14)
    assert sum(edge["type"] == "reads" for edge in partial_edges) == 1
    assert sum(edge["type"] == "writes" for edge in partial_edges) == 0

    reseed_target = envelope(14, "render-target-bind", {
        "schema": "render-target-binding-v1",
        "targetBindingObservationId": "obs-target-binding-5-g1",
        "renderTargetObservationIds": ["obs-render-target-4-g1"],
        "depthTargetObservationId": None, "identityDetailsAvailable": True,
    })
    reseeded_draw = json.loads(json.dumps(stale_draw))
    reseeded_draw["sequence"] = 15
    reseeded_draw["timestampQpc"] = 1015
    reseeded_draw["payload"]["targetBindingObservationId"] = "obs-target-binding-5-g1"
    reseeded_graph = build_graph(
        tool, manifest, restore_false_prefix + [partial_rebind, reseed_target, reseeded_draw]
    )
    reseeded_edges = event_resource_edges(reseeded_graph, 15)
    assert sum(edge["type"] == "reads" for edge in reseeded_edges) == 1
    assert sum(edge["type"] == "writes" for edge in reseeded_edges) == 1

    restore_true_draw = json.loads(json.dumps(stale_draw))
    restore_true_draw["payload"]["targetBindingObservationId"] = "obs-target-binding-5-g1"
    restore_true_draw["sequence"] = 13
    restore_true_draw["timestampQpc"] = 1013
    restore_true_graph = build_graph(
        tool, manifest,
        json.loads(json.dumps(hazard_events[:7])) + deferred_execute_events(7, True) + [restore_true_draw],
    )
    restore_true_edges = event_resource_edges(restore_true_graph, 13)
    assert sum(edge["type"] == "reads" for edge in restore_true_edges) == 1
    assert sum(edge["type"] == "writes" for edge in restore_true_edges) == 1

    predicted_reset_events = [
        envelope(0, "resource-observed", {
            **resource_base, "resourceObservationId": "obs-resource-73-g1",
        }),
        envelope(1, "target-view-observed", {
            "schema": "target-view-observation-v1",
            "targetViewObservationId": "obs-shader-resource-view-74-g1",
            "kind": "shader-resource-view", "d3dObjectPointer": "0x740",
            "pointerGeneration": 1, "resourceObservationId": "obs-resource-73-g1",
            "format": 28, "viewDimension": 4, "subresources": {}, "flags": 0,
        }),
        envelope(2, "resource-view-bind", {
            "schema": "resource-view-binding-v2", "source": "requested-call",
            "viewObservationId": "obs-shader-resource-view-74-g1",
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
        }),
        envelope(3, "resource-view-bind", {
            "schema": "resource-view-binding-v2", "source": "post-call-query",
            "viewObservationId": "obs-shader-resource-view-74-g1",
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
        }),
        envelope(4, "resource-view-state-observed", {
            "schema": "resource-view-state-observed-v1", "source": "post-call-query",
            "bindingKind": "shader-resource", "stage": "pixel", "startSlot": 0,
            "count": 1, "changedSlotCount": 1,
        }),
        *deferred_execute_events(5, False),
        envelope(11, "resource-view-bind", {
            "schema": "resource-view-binding-v2", "source": "post-call-query",
            "viewObservationId": None,
            "bindingKind": "shader-resource", "stage": "pixel", "slot": 0,
        }),
        envelope(12, "resource-view-state-observed", {
            "schema": "resource-view-state-observed-v1", "source": "post-call-query",
            "bindingKind": "shader-resource", "stage": "pixel", "startSlot": 0,
            "count": 1, "changedSlotCount": 1,
        }),
    ]
    predicted_reset_graph = build_graph(tool, manifest, predicted_reset_events)
    assert predicted_reset_graph["extensions"]["csx.effectiveStateVerifiedSlots"] == 1
    assert predicted_reset_graph["extensions"]["csx.effectiveStateMismatchSlots"] == 0

    identity_fixture = json.loads(
        (repo / "tests" / "fixtures" / "render-map" / "deferred-command-identity-cases.json")
        .read_text(encoding="utf-8")
    )

    def identity_declarations() -> list[dict]:
        result: list[dict] = []
        sequence = 0
        for context in identity_fixture["contexts"].values():
            event = envelope(sequence, "device-context-observed", {
                "schema": "device-context-observation-v2",
                "deviceContextObservationId": context["id"],
                "contextPointer": context["pointer"], "pointerGeneration": 1,
                "kind": "deferred", "creationEvidence": "create-deferred-context-hook",
                "contextFlags": 0,
            })
            event["deviceContextObservationId"] = context["id"]
            result.append(event)
            sequence += 1
        for recording in identity_fixture["recordings"].values():
            context = identity_fixture["contexts"][recording["context"]]
            event = envelope(sequence, "command-recording-observed", {
                "schema": "command-recording-observation-v1",
                "commandRecordingObservationId": recording["id"],
                "deviceContextObservationId": context["id"],
                "epoch": recording["epoch"], "partialAtCaptureStart": False,
            })
            event["deviceContextObservationId"] = context["id"]
            event["commandRecordingObservationId"] = recording["id"]
            result.append(event)
            sequence += 1
        for command_list in identity_fixture["commandLists"].values():
            recording = identity_fixture["recordings"][command_list["recording"]]
            context = identity_fixture["contexts"][recording["context"]]
            event = envelope(sequence, "command-list-observed", {
                "schema": "command-list-observation-v2",
                "commandListObservationId": command_list["id"],
                "commandListPointer": command_list["pointer"], "pointerGeneration": 1,
                "sourceDeviceContextObservationId": context["id"],
                "sourceCommandRecordingObservationId": recording["id"],
                "sourceRecordingComplete": True, "sourceRecordingIncompleteReasons": [],
            })
            event["deviceContextObservationId"] = context["id"]
            event["commandRecordingObservationId"] = recording["id"]
            result.append(event)
            sequence += 1
        return result

    context_a = identity_fixture["contexts"]["a"]
    context_b = identity_fixture["contexts"]["b"]
    recording_a = identity_fixture["recordings"]["a"]
    recording_b = identity_fixture["recordings"]["b"]
    list_a = identity_fixture["commandLists"]["a"]
    list_b = identity_fixture["commandLists"]["b"]

    def recorded_draw(sequence: int, context_id: str, recording_id: str) -> dict:
        event = envelope(sequence, "draw", {
            "schema": "draw-call-v4", "operation": "draw",
            "deviceContextPointer": "0x0", "vertexShaderObservationId": None,
            "pixelShaderObservationId": None, "targetBindingObservationId": None,
            "submissionObservationId": None, "preparedGeometrySetupObservationId": None,
            "arguments": {"vertexCount": 3, "startVertexLocation": 0},
        })
        event["deviceContextObservationId"] = context_id
        event["commandRecordingObservationId"] = recording_id
        event["execution"]["observationDomain"] = "command-recording"
        return event

    def finish_event(
        sequence: int, envelope_recording: str, payload_recording: str,
        context_id: str, command_list: dict,
    ) -> dict:
        event = envelope(sequence, "finish-command-list", {
            "schema": "finish-command-list-v2",
            "commandRecordingObservationId": payload_recording,
            "commandListObservationId": command_list["id"],
            "commandListPointer": command_list["pointer"],
            "restoreDeferredContextState": False, "hresult": 0, "succeeded": True,
            "sourceRecordingComplete": True, "sourceRecordingIncompleteReasons": [],
        })
        event["deviceContextObservationId"] = context_id
        event["commandRecordingObservationId"] = envelope_recording
        event["execution"]["observationDomain"] = "command-recording"
        return event

    def immediate_context_event(
        sequence: int, context_id: str = "obs-device-context-90-g1",
    ) -> dict:
        event = envelope(sequence, "device-context-observed", {
            "schema": "device-context-observation-v2",
            "deviceContextObservationId": context_id,
            "contextPointer": "0x900", "pointerGeneration": 1,
            "kind": "immediate", "creationEvidence": "initial-immediate-context",
            "contextFlags": 0,
        })
        event["deviceContextObservationId"] = context_id
        return event

    def execute_event(
        sequence: int, command_list: dict, source_recording_id: str,
        context_id: str | None = "obs-device-context-90-g1",
    ) -> dict:
        event = envelope(sequence, "execute-command-list", {
            "schema": "execute-command-list-v1",
            "commandListObservationId": command_list["id"],
            "commandListPointer": command_list["pointer"],
            "sourceCommandRecordingObservationId": source_recording_id,
            "restoreContextState": True,
        })
        event["deviceContextObservationId"] = context_id
        return event

    valid_identity_events = identity_declarations() + [
        immediate_context_event(6),
        recorded_draw(7, context_a["id"], recording_a["id"]),
        finish_event(8, recording_a["id"], recording_a["id"], context_a["id"], list_a),
        execute_event(9, list_a, recording_a["id"]),
    ]
    valid_identity_graph = build_graph(tool, manifest, valid_identity_events)
    valid_edge_types = [edge["type"] for edge in valid_identity_graph["edges"]]
    assert valid_edge_types.count("records") == 3
    assert valid_edge_types.count("materializes") == 2
    assert valid_edge_types.count("finishes") == 1
    assert valid_edge_types.count("executes") == 1

    contradiction_results: set[str] = set()

    cross_draw_graph = build_graph(
        tool, manifest,
        identity_declarations() + [recorded_draw(6, context_b["id"], recording_a["id"])],
    )
    cross_draw_node = next(
        node for node in cross_draw_graph["nodes"] if node["attributes"].get("eventSequence") == 6
    )
    assert not [
        edge for edge in cross_draw_graph["edges"]
        if edge["type"] == "records" and edge["to"] == cross_draw_node["id"]
    ]
    assert any(gap["blocking"] and "recording owner" in gap["description"] for gap in cross_draw_graph["gaps"])
    contradiction_results.add("draw-context-versus-recording-owner")

    cross_list = envelope(6, "command-list-observed", {
        "schema": "command-list-observation-v2",
        "commandListObservationId": "obs-command-list-86-g1",
        "commandListPointer": "0x860", "pointerGeneration": 1,
        "sourceDeviceContextObservationId": context_b["id"],
        "sourceCommandRecordingObservationId": recording_a["id"],
        "sourceRecordingComplete": True, "sourceRecordingIncompleteReasons": [],
    })
    cross_list["deviceContextObservationId"] = context_b["id"]
    cross_list["commandRecordingObservationId"] = recording_a["id"]
    cross_list_graph = build_graph(tool, manifest, identity_declarations() + [cross_list])
    assert sum(edge["type"] == "materializes" for edge in cross_list_graph["edges"]) == 2
    assert any(gap["blocking"] and "is owned by" in gap["description"] for gap in cross_list_graph["gaps"])
    contradiction_results.add("command-list-context-versus-recording-owner")

    finish_envelope_graph = build_graph(
        tool, manifest, identity_declarations() + [
            finish_event(6, recording_b["id"], recording_a["id"], context_a["id"], list_a)
        ],
    )
    assert sum(edge["type"] == "finishes" for edge in finish_envelope_graph["edges"]) == 0
    assert any(gap["blocking"] and "envelope recording" in gap["description"] for gap in finish_envelope_graph["gaps"])
    contradiction_results.add("finish-envelope-versus-payload-recording")

    finish_list_graph = build_graph(
        tool, manifest, identity_declarations() + [
            finish_event(6, recording_a["id"], recording_a["id"], context_a["id"], list_b)
        ],
    )
    assert sum(edge["type"] == "finishes" for edge in finish_list_graph["edges"]) == 0
    assert any(gap["blocking"] and "does not match command list" in gap["description"] for gap in finish_list_graph["gaps"])
    contradiction_results.add("finish-list-versus-recording")

    execute_source_graph = build_graph(
        tool, manifest, identity_declarations() + [
            immediate_context_event(6), execute_event(7, list_a, recording_b["id"]),
        ],
    )
    assert sum(edge["type"] == "executes" for edge in execute_source_graph["edges"]) == 0
    assert any(gap["blocking"] and "does not match command list" in gap["description"] for gap in execute_source_graph["gaps"])
    contradiction_results.add("execute-source-recording-versus-list")

    def assert_execute_rejected(candidate: dict, expected_gap: str, declarations: list[dict]) -> None:
        graph_under_test = build_graph(tool, manifest, declarations + [candidate])
        assert sum(edge["type"] == "executes" for edge in graph_under_test["edges"]) == 0
        assert any(
            gap["blocking"] and expected_gap in gap["description"]
            for gap in graph_under_test["gaps"]
        ), graph_under_test["gaps"]

    assert_execute_rejected(
        execute_event(6, list_a, recording_a["id"], None),
        "no valid declared execution context", identity_declarations(),
    )
    assert_execute_rejected(
        execute_event(6, list_a, recording_a["id"], "obs-device-context-404-g1"),
        "no valid declared execution context", identity_declarations(),
    )
    assert_execute_rejected(
        execute_event(6, list_a, recording_a["id"], context_a["id"]),
        "names non-immediate context", identity_declarations(),
    )
    wrong_domain = execute_event(7, list_a, recording_a["id"])
    wrong_domain["execution"]["observationDomain"] = "command-recording"
    assert_execute_rejected(
        wrong_domain, "is not a sequenced CPU-call observation",
        identity_declarations() + [immediate_context_event(6)],
    )
    recording_envelope = execute_event(7, list_a, recording_a["id"])
    recording_envelope["commandRecordingObservationId"] = recording_a["id"]
    assert_execute_rejected(
        recording_envelope, "unexpectedly carries recording envelope",
        identity_declarations() + [immediate_context_event(6)],
    )
    conflicted_immediate = immediate_context_event(7)
    conflicted_immediate["payload"]["contextPointer"] = "0x901"
    assert_execute_rejected(
        execute_event(8, list_a, recording_a["id"]),
        "no valid declared execution context",
        identity_declarations() + [immediate_context_event(6), conflicted_immediate],
    )
    mixed_execute_graph = build_graph(
        tool, manifest,
        identity_declarations() + [
            immediate_context_event(6),
            execute_event(7, list_a, recording_a["id"], context_a["id"]),
            execute_event(8, list_b, recording_b["id"]),
        ],
    )
    assert sum(edge["type"] == "executes" for edge in mixed_execute_graph["edges"]) == 1

    duplicate_specs = [
        ("device-context", "duplicate-device-context-identity", 1, "contextPointer", "0x811"),
        ("command-recording", "duplicate-command-recording-identity", 3, "epoch", 2),
        ("command-list", "duplicate-command-list-identity", 5, "commandListPointer", "0x851"),
    ]
    for identity_kind, case_name, declaration_index, field, replacement in duplicate_specs:
        declarations = identity_declarations()
        duplicate = json.loads(json.dumps(declarations[declaration_index]))
        duplicate["sequence"] = 6
        duplicate["timestampQpc"] = 1006
        duplicate["payload"][field] = replacement
        graph_under_test = build_graph(
            tool, manifest, declarations + [duplicate] + [
                recorded_draw(7, context_b["id"], recording_b["id"]),
                recorded_draw(8, context_a["id"], recording_a["id"]),
            ],
        )
        valid_draw_node = next(
            node for node in graph_under_test["nodes"] if node["attributes"].get("eventSequence") == 8
        )
        assert any(
            edge["type"] == "records" and edge["to"] == valid_draw_node["id"]
            for edge in graph_under_test["edges"]
        )
        invalid_draw_node = next(
            node for node in graph_under_test["nodes"] if node["attributes"].get("eventSequence") == 7
        )
        if identity_kind in {"device-context", "command-recording"}:
            assert not [
                edge for edge in graph_under_test["edges"]
                if edge["type"] == "records" and edge["to"] == invalid_draw_node["id"]
            ]
        else:
            duplicated_list_node = next(
                node for node in graph_under_test["nodes"]
                if any(
                    ref.get("kind") == "observation" and ref.get("value") == list_b["id"]
                    for ref in node["sourceRefs"]
                )
            )
            assert not [
                edge for edge in graph_under_test["edges"]
                if edge["type"] == "materializes" and edge["to"] == duplicated_list_node["id"]
            ]
        assert any(
            gap["blocking"] and f"Typed {identity_kind} identity" in gap["description"]
            for gap in graph_under_test["gaps"]
        )
        contradiction_results.add(case_name)

    envelope_duplicate_specs = [
        ("device-context", "duplicate-device-context-envelope-identity", 1,
         "deviceContextObservationId", context_a["id"]),
        ("command-recording", "duplicate-command-recording-envelope-identity", 3,
         "commandRecordingObservationId", recording_a["id"]),
        ("command-recording", "duplicate-command-recording-envelope-owner", 3,
         "deviceContextObservationId", context_a["id"]),
        ("command-list", "duplicate-command-list-envelope-owner", 5,
         "deviceContextObservationId", context_a["id"]),
        ("command-list", "duplicate-command-list-envelope-recording", 5,
         "commandRecordingObservationId", recording_a["id"]),
    ]
    identity_field_by_kind = {
        "device-context": "deviceContextObservationId",
        "command-recording": "commandRecordingObservationId",
        "command-list": "commandListObservationId",
    }
    for identity_kind, case_name, declaration_index, envelope_field, replacement in envelope_duplicate_specs:
        for contradictory_first in (False, True):
            declarations = identity_declarations()
            duplicate = json.loads(json.dumps(declarations[declaration_index]))
            duplicate[envelope_field] = replacement
            if contradictory_first:
                declarations.insert(declaration_index, duplicate)
            else:
                declarations.append(duplicate)
            for sequence, declaration in enumerate(declarations):
                declaration["sequence"] = sequence
                declaration["timestampQpc"] = 1000 + sequence
            invalid_draw = recorded_draw(len(declarations), context_b["id"], recording_b["id"])
            valid_draw = recorded_draw(len(declarations) + 1, context_a["id"], recording_a["id"])
            graph_under_test = build_graph(
                tool, manifest, declarations + [invalid_draw, valid_draw],
            )
            valid_draw_node = next(
                node for node in graph_under_test["nodes"]
                if node["attributes"].get("eventSequence") == len(declarations) + 1
            )
            assert any(
                edge["type"] == "records" and edge["to"] == valid_draw_node["id"]
                for edge in graph_under_test["edges"]
            )
            target_id = duplicate["payload"][identity_field_by_kind[identity_kind]]
            target_node = next(
                node for node in graph_under_test["nodes"]
                if any(
                    ref.get("kind") == "observation" and ref.get("value") == target_id
                    for ref in node["sourceRefs"]
                )
            )
            assert not [
                edge for edge in graph_under_test["edges"]
                if edge["type"] in {"records", "materializes", "finishes", "executes"}
                and target_node["id"] in {edge["from"], edge["to"]}
            ]
            assert any(
                gap["blocking"] and f"Typed {identity_kind} identity" in gap["description"]
                for gap in graph_under_test["gaps"]
            )
        contradiction_results.add(case_name)

    for identity_kind, _, declaration_index, _, _ in envelope_duplicate_specs:
        declarations = identity_declarations()
        declarations.append(json.loads(json.dumps(declarations[declaration_index])))
        for sequence, declaration in enumerate(declarations):
            declaration["sequence"] = sequence
            declaration["timestampQpc"] = 1000 + sequence
        exact_duplicate_graph = build_graph(tool, manifest, declarations)
        assert not any(
            f"Typed {identity_kind} identity" in gap["description"]
            for gap in exact_duplicate_graph["gaps"]
        )
        assert sum(edge["type"] == "records" for edge in exact_duplicate_graph["edges"]) == 2
        assert sum(edge["type"] == "materializes" for edge in exact_duplicate_graph["edges"]) == 2

    assert contradiction_results == set(identity_fixture["contradictions"])

    def assert_recording_event_failed_closed(candidate: dict, expected_gap: str) -> None:
        active_immediate_state = json.loads(json.dumps(hazard_events[:7]))
        candidate["sequence"] = 7
        candidate["timestampQpc"] = 1007
        graph_under_test = build_graph(tool, manifest, active_immediate_state + [candidate])
        event_node = next(
            node for node in graph_under_test["nodes"]
            if node["attributes"].get("eventSequence") == 7
            and node["kind"] in {"draw", "dispatch"}
        )
        forbidden = {"reads", "writes", "submits", "precedes"}
        assert not [
            edge for edge in graph_under_test["edges"]
            if edge["type"] in forbidden
            and event_node["id"] in {edge["from"], edge["to"]}
        ], graph_under_test["edges"]
        assert any(
            expected_gap in gap["description"] and event_node["id"] in gap["relatedNodeIds"]
            for gap in graph_under_test["gaps"]
        ), graph_under_test["gaps"]

    ambiguous_draw = envelope(7, "draw", {
        "schema": "draw-call-v4", "operation": "draw",
        "deviceContextPointer": "0x100", "vertexShaderObservationId": None,
        "pixelShaderObservationId": None, "targetBindingObservationId": None,
        "submissionObservationId": None, "preparedGeometrySetupObservationId": None,
        "arguments": {"vertexCount": 3, "startVertexLocation": 0},
    })
    ambiguous_draw["execution"]["observationDomain"] = "command-recording"
    ambiguous_draw["deviceContextObservationId"] = None
    assert_recording_event_failed_closed(ambiguous_draw, "has no recording identity")

    undeclared_dispatch = envelope(7, "dispatch", {
        "schema": "dispatch-call-v2", "operation": "dispatch",
        "deviceContextPointer": "0x101", "computeShaderObservationId": None,
        "arguments": {"threadGroupCountX": 1, "threadGroupCountY": 1, "threadGroupCountZ": 1},
    })
    undeclared_dispatch["execution"]["observationDomain"] = "command-recording"
    undeclared_dispatch["deviceContextObservationId"] = "obs-device-context-404-g1"
    undeclared_dispatch["commandRecordingObservationId"] = "obs-command-recording-404-g1"
    assert_recording_event_failed_closed(undeclared_dispatch, "undeclared recording")

    conflicting_draw = json.loads(json.dumps(ambiguous_draw))
    conflicting_draw["deviceContextObservationId"] = "obs-device-context-1-g1"
    conflicting_draw["commandRecordingObservationId"] = "obs-command-recording-405-g1"
    immediate_context = envelope(6, "device-context-observed", {
        "schema": "device-context-observation-v2",
        "deviceContextObservationId": "obs-device-context-1-g1",
        "contextPointer": "0x9", "pointerGeneration": 1,
        "kind": "immediate", "creationEvidence": "initial-immediate-context",
        "contextFlags": 0,
    })
    conflicting_prefix = json.loads(json.dumps(hazard_events[:6])) + [immediate_context]
    conflicting_graph = build_graph(tool, manifest, conflicting_prefix + [conflicting_draw])
    conflicting_node = next(
        node for node in conflicting_graph["nodes"]
        if node["attributes"].get("eventSequence") == 7 and node["kind"] == "draw"
    )
    assert not [
        edge for edge in conflicting_graph["edges"]
        if edge["type"] in {"reads", "writes", "submits", "precedes"}
        and conflicting_node["id"] in {edge["from"], edge["to"]}
    ]
    assert any(
        "conflicts with immediate device context" in gap["description"]
        for gap in conflicting_graph["gaps"]
    )

    incomplete_events = [
        envelope(0, "device-context-observed", {
            "schema": "device-context-observation-v2",
            "deviceContextObservationId": "obs-device-context-1-g1",
            "contextPointer": "0x100", "pointerGeneration": 1,
            "kind": "immediate", "creationEvidence": "initial-immediate-context",
            "contextFlags": 0,
        }),
        envelope(1, "command-list-observed", {
            "schema": "command-list-observation-v2",
            "commandListObservationId": "obs-command-list-11-g1",
            "commandListPointer": "0x400", "pointerGeneration": 1,
            "sourceDeviceContextObservationId": None,
            "sourceCommandRecordingObservationId": None,
            "sourceRecordingComplete": False,
            "sourceRecordingIncompleteReasons": ["declaration-unavailable"],
        }),
        envelope(2, "execute-command-list", {
            "schema": "execute-command-list-v1",
            "commandListObservationId": "obs-command-list-11-g1",
            "commandListPointer": "0x400",
            "sourceCommandRecordingObservationId": None,
            "restoreContextState": False,
        }),
        envelope(3, "execute-command-list", {
            "schema": "execute-command-list-v1",
            "commandListObservationId": "obs-command-list-11-g1",
            "commandListPointer": "0x400",
            "sourceCommandRecordingObservationId": None,
            "restoreContextState": True,
        }),
        envelope(4, "finish-command-list", {
            "schema": "finish-command-list-v2",
            "commandRecordingObservationId": None,
            "commandListObservationId": None,
            "commandListPointer": None,
            "restoreDeferredContextState": True,
            "hresult": -2147467259,
            "succeeded": False,
            "sourceRecordingComplete": False,
            "sourceRecordingIncompleteReasons": ["declaration-unavailable"],
        }),
    ]
    incomplete_graph = build_graph(tool, manifest, incomplete_events)
    incomplete_kinds = [node["kind"] for node in incomplete_graph["nodes"]]
    incomplete_edges = [edge["type"] for edge in incomplete_graph["edges"]]
    incomplete_gaps = [gap["description"] for gap in incomplete_graph["gaps"]]
    assert incomplete_kinds.count("command-list-finish") == 1
    assert incomplete_kinds.count("command-execution") == 2
    assert incomplete_edges.count("executes") == 2
    assert any("has no declared source recording" in gap for gap in incomplete_gaps)
    assert any("ended an incomplete source recording" in gap for gap in incomplete_gaps)
    print("Render graph builder test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
