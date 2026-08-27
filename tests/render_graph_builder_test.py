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
        "type": event_type,
        "scopes": {"renderPass": None, "technique": None, "geometry": None, "commandList": None},
        "causes": [], "manifestRefs": [], "engineRefs": [], "observationRefs": [],
        "payload": payload, "extensions": {},
    }


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
    events = [
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
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        manifest_path = root / "capture-manifest.json"
        events_path = root / "events.jsonl"
        output_path = root / "render-graph.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        events_path.write_text("\n".join(json.dumps(event) for event in events) + "\n", encoding="utf-8")
        subprocess.run([sys.executable, str(tool), "--capture-manifest", str(manifest_path), "--events", str(events_path), "--output", str(output_path)], check=True)
        graph = json.loads(output_path.read_text(encoding="utf-8"))
        assert graph["schema"]["producerVersion"] == "resource-versions-1"
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
    print("Render graph builder test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
