#!/usr/bin/env python3
"""Derive an evidence-bearing resource-flow graph from a CSX render-map capture."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any


OBSERVATION_NUMBER = re.compile(r"-(\d+)-g\d+$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def observation_sort_key(value: str) -> tuple[int, str]:
    match = OBSERVATION_NUMBER.search(value)
    return (int(match.group(1)) if match else 2**63 - 1, value)


def load_events(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
    previous = -1
    for event in events:
        sequence = event.get("sequence")
        if not isinstance(sequence, int) or sequence <= previous:
            raise ValueError("event sequences must be strictly increasing integers")
        previous = sequence
    return events


def git_commit(repo: Path) -> str | None:
    try:
        value = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True, stderr=subprocess.DEVNULL
        ).strip()
        return value if re.fullmatch(r"[0-9a-fA-F]{40}", value) else None
    except (OSError, subprocess.CalledProcessError):
        return None


class Graph:
    def __init__(self, capture_id: str) -> None:
        self.capture_id = capture_id
        self.nodes: list[dict[str, Any]] = []
        self.edges: list[dict[str, Any]] = []
        self.gaps: list[dict[str, Any]] = []
        self._node_ids: dict[tuple[str, str], str] = {}
        self._kind_counts: dict[str, int] = {}
        self.eye_attribution_observed = False

    def node(self, key: str, kind: str, label: str, sequence: int | None, attributes: dict[str, Any]) -> str:
        identity = (kind, key)
        if identity in self._node_ids:
            return self._node_ids[identity]
        self._kind_counts[kind] = self._kind_counts.get(kind, 0) + 1
        node_id = f"node-{kind}-{self._kind_counts[kind]:04d}"
        source_refs: list[dict[str, Any]] = []
        if key.startswith("obs-"):
            source_refs.append({"kind": "observation", "value": key})
        if sequence is not None:
            source_refs.append({"kind": "capture-event", "value": sequence})
        self.nodes.append({
            "id": node_id,
            "kind": kind,
            "label": label,
            "sourceRefs": source_refs,
            "attributes": attributes,
            "extensions": {},
        })
        self._node_ids[identity] = node_id
        return node_id

    def edge(self, edge_type: str, source: str, target: str, sequences: list[int], note: str,
             attributes: dict[str, Any] | None = None) -> None:
        self.edges.append({
            "id": f"edge-{len(self.edges) + 1:04d}",
            "type": edge_type,
            "from": source,
            "to": target,
            "evidenceClass": "runtime-observed",
            "confidence": "confirmed",
            "evidence": [{
                "captureId": self.capture_id,
                "eventSequences": sorted(set(sequences)),
                "engineEvidenceRefs": [],
                "note": note,
            }],
            "ambiguityGroup": None,
            "attributes": attributes or {},
            "extensions": {},
        })

    def gap(self, description: str, related: list[str] | None = None, blocking: bool = False) -> None:
        self.gaps.append({
            "id": f"gap-{len(self.gaps) + 1:04d}",
            "kind": "uncorrelated",
            "description": description,
            "relatedNodeIds": sorted(set(related or [])),
            "blocking": blocking,
            "extensions": {},
        })


def derive(manifest: dict[str, Any], events: list[dict[str, Any]]) -> Graph:
    capture_id = manifest["captureId"]
    graph = Graph(capture_id)
    resources: dict[str, dict[str, Any]] = {}
    views: dict[str, dict[str, Any]] = {}
    target_bindings: dict[str, dict[str, Any]] = {}
    resource_versions: dict[str, dict[str, Any]] = {}
    submissions: dict[str, dict[str, Any]] = {}
    srv_state: dict[tuple[str, int], str | None] = {}
    uav_state: dict[tuple[str, int], str | None] = {}

    for event in events:
        if event.get("captureId") != capture_id:
            raise ValueError(f"event {event.get('sequence')} belongs to a different capture")
        sequence = event["sequence"]
        payload = event.get("payload", {})
        event_type = event.get("type")
        if event_type == "resource-observed":
            resource_id = payload.get("resourceObservationId")
            if resource_id:
                resources[resource_id] = {"sequence": sequence, "payload": payload}
        elif event_type == "target-view-observed":
            view_id = payload.get("targetViewObservationId")
            if view_id:
                views[view_id] = {"sequence": sequence, "payload": payload}
        elif event_type == "render-target-bind":
            binding_id = payload.get("targetBindingObservationId")
            if binding_id:
                target_bindings[binding_id] = {"sequence": sequence, "payload": payload}
        elif event_type == "resource-view-bind":
            key = (str(payload.get("stage")), int(payload.get("slot", 0)))
            state = srv_state if payload.get("bindingKind") == "shader-resource" else uav_state
            state[key] = payload.get("viewObservationId")
        elif event_type == "resource-version-observed":
            version_id = payload.get("resourceVersionObservationId")
            resource_id = payload.get("resourceObservationId")
            if version_id:
                resource_versions[version_id] = {"sequence": sequence, "payload": payload}
                version_node = graph.node(
                    version_id, "resource-version", version_id, sequence, payload
                )
                resource = resources.get(resource_id)
                if resource:
                    resource_node = graph.node(
                        resource_id, "resource", resource_id, resource["sequence"], resource["payload"]
                    )
                    graph.edge(
                        "versions", resource_node, version_node,
                        [resource["sequence"], sequence],
                        "A write epoch versions this observed resource and subresource range.",
                    )
                else:
                    graph.gap(
                        f"Resource version {version_id} refers to undeclared resource {resource_id}.",
                        [version_node], True,
                    )
        elif event_type == "visibility-consumed":
            submission_id = payload.get("submissionObservationId")
            version_id = payload.get("resourceVersionObservationId")
            if submission_id:
                submissions[submission_id] = {"sequence": sequence, "payload": payload}
                submission_node = graph.node(
                    submission_id, "submission", submission_id, sequence, payload
                )
                version = resource_versions.get(version_id)
                if version:
                    version_node = graph.node(
                        version_id, "resource-version", version_id, version["sequence"], version["payload"]
                    )
                    graph.edge(
                        "consumes", version_node, submission_node,
                        [version["sequence"], sequence],
                        "The explicit visibility consumer names this resource version.",
                        {"bindingMatches": payload.get("bindingMatches"), "slot": payload.get("slot")},
                    )
                elif version_id:
                    graph.gap(
                        f"Visibility submission {submission_id} refers to undeclared version {version_id}.",
                        [submission_node], True,
                    )
        elif event_type == "cull-decision" and payload.get("schema") == "cull-decision-v1":
            version_id = payload.get("resourceVersionObservationId")
            decision_node = graph.node(
                f"event-{sequence}", "visibility-test",
                f"visibility decision for object {payload.get('objectIndex')} at event {sequence}",
                sequence, payload,
            )
            version = resource_versions.get(version_id)
            if version:
                version_node = graph.node(
                    version_id, "resource-version", version_id, version["sequence"], version["payload"]
                )
                graph.edge(
                    "result-for", version_node, decision_node,
                    [version["sequence"], sequence],
                    "Completed CPU readback classified a covered object from this exact resource version.",
                )
            elif version_id:
                graph.gap(
                    f"Cull decision event {sequence} refers to undeclared version {version_id}.",
                    [decision_node], True,
                )
        elif event_type == "eye-submitted":
            resource_id = payload.get("resourceObservationId")
            eye_node = graph.node(
                f"event-{sequence}", "eye-submit",
                f"{payload.get('eye', 'unknown')} eye submit at event {sequence}",
                sequence, payload,
            )
            resource = resources.get(resource_id)
            if resource:
                resource_node = graph.node(
                    resource_id, "resource", resource_id, resource["sequence"], resource["payload"]
                )
                graph.edge(
                    "presents", resource_node, eye_node,
                    [resource["sequence"], sequence],
                    "Accepted OpenVR Submit identifies this texture resource, eye, and source bounds.",
                )
                graph.eye_attribution_observed = True
            else:
                graph.gap(
                    f"Eye submission event {sequence} refers to undeclared resource {resource_id}.",
                    [eye_node], True,
                )
        elif event_type in {"draw", "dispatch", "resource-flow"}:
            execution_kind = "draw" if event_type == "draw" else ("dispatch" if event_type == "dispatch" else "copy")
            operation = payload.get("operation", event_type)
            execution = graph.node(
                f"event-{sequence}", execution_kind, f"{operation} at event {sequence}", sequence,
                {
                    "eventSequence": sequence,
                    "commandStreamSequence": event.get("execution", {}).get("commandStreamSequence"),
                    "cpuFrame": event.get("frame", {}).get("cpuFrame"),
                    "eye": event.get("frame", {}).get("eye", "unknown"),
                    "operation": operation,
                },
            )

            read_views: list[tuple[str, str, int]] = []
            write_views: list[tuple[str, str, int]] = []
            direct_reads: list[tuple[str, str]] = []
            direct_writes: list[tuple[str, str]] = []
            if event_type == "draw":
                submission_id = event.get("submissionObservationId") or payload.get("submissionObservationId")
                submission = submissions.get(submission_id)
                if submission:
                    submission_node = graph.node(
                        submission_id, "submission", submission_id,
                        submission["sequence"], submission["payload"]
                    )
                    graph.edge(
                        "submits", submission_node, execution,
                        [submission["sequence"], sequence],
                        "The draw consumed this explicit pending submission identity.",
                    )
                elif submission_id:
                    graph.gap(
                        f"Draw event {sequence} refers to undeclared submission {submission_id}.",
                        [execution], True,
                    )
                for (stage, slot), view_id in sorted(srv_state.items()):
                    if stage != "compute" and view_id:
                        read_views.append((view_id, stage, slot))
                for (stage, slot), view_id in sorted(uav_state.items()):
                    if stage == "output-merger" and view_id:
                        write_views.append((view_id, stage, slot))
                binding_id = payload.get("targetBindingObservationId")
                binding = target_bindings.get(binding_id)
                if binding:
                    binding_payload = binding["payload"]
                    for slot, view_id in enumerate(binding_payload.get("renderTargetObservationIds", [])):
                        if view_id:
                            write_views.append((view_id, "output-merger", slot))
                    depth_view = binding_payload.get("depthTargetObservationId")
                    if depth_view:
                        read_views.append((depth_view, "depth-stencil", 0))
                        write_views.append((depth_view, "depth-stencil", 0))
                else:
                    graph.gap(
                        f"Draw event {sequence} has no catalogued output-merger binding; pre-capture state is unknown.",
                        [execution], False,
                    )
            elif event_type == "dispatch":
                for (stage, slot), view_id in sorted(srv_state.items()):
                    if stage == "compute" and view_id:
                        read_views.append((view_id, stage, slot))
                for (stage, slot), view_id in sorted(uav_state.items()):
                    if stage == "compute" and view_id:
                        write_views.append((view_id, stage, slot))
            else:
                source = payload.get("sourceResourceObservationId")
                destination = payload.get("destinationResourceObservationId")
                if source:
                    direct_reads.append((source, "copy-source"))
                if destination:
                    direct_writes.append((destination, "copy-destination"))

            for view_id, stage, slot in read_views:
                view = views.get(view_id)
                resource_id = view and view["payload"].get("resourceObservationId")
                if resource_id:
                    direct_reads.append((resource_id, f"{stage}-slot-{slot}"))
                else:
                    graph.gap(f"Read view {view_id} at event {sequence} has no resource declaration.", [execution], True)
            for view_id, stage, slot in write_views:
                view = views.get(view_id)
                resource_id = view and view["payload"].get("resourceObservationId")
                if resource_id:
                    direct_writes.append((resource_id, f"{stage}-slot-{slot}"))
                else:
                    graph.gap(f"Write view {view_id} at event {sequence} has no resource declaration.", [execution], True)

            for resource_id, role in sorted(set(direct_reads), key=lambda item: observation_sort_key(item[0])):
                resource = resources.get(resource_id)
                if not resource:
                    graph.gap(f"Execution event {sequence} reads undeclared resource {resource_id}.", [execution], True)
                    continue
                resource_node = graph.node(
                    resource_id, "resource", resource_id, resource["sequence"], resource["payload"]
                )
                graph.edge("reads", resource_node, execution, [resource["sequence"], sequence],
                           "Ordered immediate-context binding identifies this resource as an execution input.",
                           {"role": role})
            for resource_id, role in sorted(set(direct_writes), key=lambda item: observation_sort_key(item[0])):
                resource = resources.get(resource_id)
                if not resource:
                    graph.gap(f"Execution event {sequence} writes undeclared resource {resource_id}.", [execution], True)
                    continue
                resource_node = graph.node(
                    resource_id, "resource", resource_id, resource["sequence"], resource["payload"]
                )
                graph.edge("writes", execution, resource_node, [resource["sequence"], sequence],
                           "Ordered immediate-context output binding identifies this resource as an execution output.",
                           {"role": role})

    completion = manifest.get("completion", {})
    if manifest.get("status") != "complete" or completion.get("truncated"):
        graph.gap("The source capture is incomplete or truncated; absence of an edge is not evidence of absence.", blocking=True)
    if not resources:
        graph.gap("The capture contains no typed resource declarations.", blocking=True)
    return graph


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-manifest", type=Path, required=True)
    parser.add_argument("--events", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shader-manifest", type=Path)
    parser.add_argument("--engine-map", type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.capture_manifest.read_text(encoding="utf-8-sig"))
    events = load_events(args.events)
    graph = derive(manifest, events)
    repo = Path(__file__).resolve().parent.parent
    inputs = [
        {"kind": "capture-manifest", "path": str(args.capture_manifest), "sha256": sha256(args.capture_manifest), "schemaMajor": int(manifest.get("schema", {}).get("major", 1))},
        {"kind": "events", "path": str(args.events), "sha256": sha256(args.events), "schemaMajor": int(events[0].get("schema", {}).get("major", 1)) if events else 1},
    ]
    for kind, path in (("shader-manifest", args.shader_manifest), ("engine-map", args.engine_map)):
        if path:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
            inputs.append({"kind": kind, "path": str(path), "sha256": sha256(path), "schemaMajor": int(data.get("schema", {}).get("major", 1))})
    output = {
        "schema": {"name": "csx.derived-render-graph", "major": 1, "minor": 1, "producerVersion": "resource-flow-1"},
        "reportId": f"render-graph-{manifest['captureId'].removeprefix('capture-')}",
        "generatedAtUtc": manifest.get("createdAtUtc", "1970-01-01T00:00:00Z"),
        "generatedBy": {"name": "csx-render-map-join", "version": "0.2.0", "gitCommit": git_commit(repo)},
        "inputs": inputs,
        "nodes": graph.nodes,
        "edges": graph.edges,
        "ambiguities": [],
        "gaps": graph.gaps,
        "decisionWindows": [],
        "extensions": {
            "csx.executionGranularity": "individual-immediate-context-call",
            "csx.deferredContextCoverage": False,
            "csx.vrEyeAttribution": graph.eye_attribution_observed,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "nodes": len(graph.nodes), "edges": len(graph.edges), "gaps": len(graph.gaps), "sha256": sha256(args.output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
