#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def envelope(sequence: int, event_type: str, payload: dict) -> dict:
    return {
        "schema": {"name": "csx.render-event", "major": 1, "minor": 8, "producerVersion": "test"},
        "captureId": "capture-resource-flow-test",
        "sequence": sequence,
        "timestampQpc": 1000 + sequence,
        "processId": 1,
        "threadId": 2,
        "frame": {"cpuFrame": 1, "sceneEpoch": 1, "submissionEpoch": None, "eye": "unknown", "eyeMask": None},
        "execution": {"observationDomain": "cpu-call", "commandStreamSequence": sequence, "gpuTimestampTicks": None, "gpuTimestampFrequencyHz": None},
        "deviceContextObservationId": "obs-device-context-1-g1",
        "submissionObservationId": None,
        "type": event_type,
        "scopes": {"renderPass": None, "technique": None, "geometry": None, "commandList": None},
        "causes": [], "manifestRefs": [], "engineRefs": [], "observationRefs": [],
        "payload": payload, "extensions": {},
    }


def build_graph(tool: Path, manifest: dict, events: list[dict]) -> dict:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        manifest_path = root / "capture-manifest.json"
        events_path = root / "events.jsonl"
        output_path = root / "render-graph.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        events_path.write_text("\n".join(json.dumps(event) for event in events) + "\n", encoding="utf-8")
        subprocess.run(
            [sys.executable, str(tool), "--capture-manifest", str(manifest_path),
             "--events", str(events_path), "--output", str(output_path)],
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
    assert graph["schema"]["producerVersion"] == "semantic-resource-graph-1"
    assert len(graph["nodes"]) == 12, graph["nodes"]
    assert [edge["type"] for edge in graph["edges"]].count("reads") == 2
    assert [edge["type"] for edge in graph["edges"]].count("writes") == 4
    assert [edge["type"] for edge in graph["edges"]].count("owns") == 6
    hazards = [edge["attributes"]["hazard"] for edge in graph["edges"] if edge["type"] == "precedes"]
    assert sorted(hazards) == ["RAW", "WAR", "WAR", "WAW", "WAW"], hazards
    assert all(edge["evidenceClass"] == "correlated" for edge in graph["edges"] if edge["type"] == "precedes")
    assert graph["extensions"]["csx.effectiveStateAdjustments"] == 1
    assert graph["extensions"]["csx.graphAcyclic"] is True
    assert len(graph["gaps"]) == 1 and graph["gaps"][0]["kind"] == "unsupported-route"
    versions = [node for node in graph["nodes"] if node["attributes"].get("resourceRole") == "content-version"]
    assert len(versions) == 6
    assert any(node["kind"] == "resource-operation" and node["attributes"]["operation"] == "update-subresource" for node in graph["nodes"])

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
        envelope(0, "shader-observed", {"schema": "shader-observation-v1", "shaderObservationId": "obs-shader-1-g1", "shaderPointer": "0x10", "pointerGeneration": 1, "shaderType": 6, "fxpFilename": "Lighting", "imageSpaceName": None, "definesSuffix": "ABC", "identityDetailsAvailable": True}),
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
    assert kinds.count("geometry") == 1
    assert kinds.count("draw") == 1
    edge_types = [edge["type"] for edge in semantic_graph["edges"]]
    assert edge_types.count("selects") == 4
    assert edge_types.count("uses") == 1
    assert edge_types.count("draws") == 3
    assert edge_types.count("binds") == 2
    assert semantic_graph["extensions"]["csx.graphAcyclic"] is True
    print("Render graph builder test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
