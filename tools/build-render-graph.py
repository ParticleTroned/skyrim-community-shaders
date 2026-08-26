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
        self._edge_keys: set[tuple[str, str, str, str]] = set()

    def node(self, key: str, kind: str, label: str, sequence: int | None, attributes: dict[str, Any],
             source_refs: list[dict[str, Any]] | None = None) -> str:
        identity = (kind, key)
        if identity in self._node_ids:
            return self._node_ids[identity]
        self._kind_counts[kind] = self._kind_counts.get(kind, 0) + 1
        node_id = f"node-{kind}-{self._kind_counts[kind]:04d}"
        if source_refs is None:
            source_refs = []
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
             attributes: dict[str, Any] | None = None, evidence_class: str = "runtime-observed",
             confidence: str = "confirmed") -> None:
        attributes = attributes or {}
        edge_key = (edge_type, source, target, json.dumps(attributes, sort_keys=True))
        if edge_key in self._edge_keys:
            return
        self._edge_keys.add(edge_key)
        self.edges.append({
            "id": f"edge-{len(self.edges) + 1:04d}",
            "type": edge_type,
            "from": source,
            "to": target,
            "evidenceClass": evidence_class,
            "confidence": confidence,
            "evidence": [{
                "captureId": self.capture_id,
                "eventSequences": sorted(set(sequences)),
                "engineEvidenceRefs": [],
                "note": note,
            }],
            "ambiguityGroup": None,
            "attributes": attributes,
            "extensions": {},
        })

    def gap(self, description: str, related: list[str] | None = None, blocking: bool = False,
            kind: str = "uncorrelated") -> None:
        self.gaps.append({
            "id": f"gap-{len(self.gaps) + 1:04d}",
            "kind": kind,
            "description": description,
            "relatedNodeIds": sorted(set(related or [])),
            "blocking": blocking,
            "extensions": {},
        })

    def is_acyclic(self) -> bool:
        node_ids = {node["id"] for node in self.nodes}
        indegree = {node_id: 0 for node_id in node_ids}
        outgoing: dict[str, list[str]] = {node_id: [] for node_id in node_ids}
        for edge in self.edges:
            source = edge["from"]
            target = edge["to"]
            if source not in indegree or target not in indegree:
                return False
            outgoing[source].append(target)
            indegree[target] += 1
        ready = [node_id for node_id, degree in indegree.items() if degree == 0]
        visited = 0
        while ready:
            source = ready.pop()
            visited += 1
            for target in outgoing[source]:
                indegree[target] -= 1
                if indegree[target] == 0:
                    ready.append(target)
        return visited == len(node_ids)


def derive(manifest: dict[str, Any], events: list[dict[str, Any]]) -> Graph:
    capture_id = manifest["captureId"]
    graph = Graph(capture_id)
    resources: dict[str, dict[str, Any]] = {}
    views: dict[str, dict[str, Any]] = {}
    target_bindings: dict[str, dict[str, Any]] = {}
    srv_state: dict[tuple[str, int], str | None] = {}
    uav_state: dict[tuple[str, int], str | None] = {}
    active_target_binding: str | None = None
    version_number: dict[str, int] = {}
    current_version: dict[str, str] = {}
    last_writer: dict[str, tuple[str, int]] = {}
    readers_since_write: dict[str, dict[str, int]] = {}
    hazard_adjustment_count = 0

    def resource_node(resource_id: str) -> str | None:
        resource = resources.get(resource_id)
        if not resource:
            return None
        return graph.node(resource_id, "resource", resource_id, resource["sequence"], {
            **resource["payload"], "resourceRole": "allocation",
        })

    def ensure_version(resource_id: str) -> str | None:
        existing = current_version.get(resource_id)
        if existing:
            return existing
        allocation = resource_node(resource_id)
        resource = resources.get(resource_id)
        if not allocation or not resource:
            return None
        version_number[resource_id] = 0
        version = graph.node(
            f"{resource_id}#version-0", "resource", f"{resource_id} content version 0",
            resource["sequence"], {
                "resourceRole": "content-version", "allocationObservationId": resource_id,
                "version": 0, "captureEntryContents": True,
                "versionScope": "whole-resource-conservative",
            }, [
                {"kind": "observation", "value": resource_id},
                {"kind": "capture-event", "value": resource["sequence"]},
            ],
        )
        graph.edge(
            "owns", allocation, version, [resource["sequence"]],
            "The observed D3D11 allocation owns an initial capture-entry content version.",
            {"version": 0, "versionScope": "whole-resource-conservative"},
        )
        current_version[resource_id] = version
        readers_since_write[resource_id] = {}
        return version

    def write_version(resource_id: str, execution: str, sequence: int, roles: list[str]) -> str | None:
        allocation = resource_node(resource_id)
        resource = resources.get(resource_id)
        if not allocation or not resource:
            return None
        next_version = version_number.get(resource_id, 0) + 1
        version_number[resource_id] = next_version
        version = graph.node(
            f"{resource_id}#version-{next_version}", "resource",
            f"{resource_id} content version {next_version}", sequence, {
                "resourceRole": "content-version", "allocationObservationId": resource_id,
                "version": next_version, "producerEventSequence": sequence,
                "versionScope": "whole-resource-conservative",
            }, [
                {"kind": "observation", "value": resource_id},
                {"kind": "capture-event", "value": sequence},
            ],
        )
        graph.edge(
            "owns", allocation, version, [resource["sequence"], sequence],
            "The observed allocation owns the content version created by this write.",
            {"version": next_version, "versionScope": "whole-resource-conservative"},
        )
        graph.edge(
            "writes", execution, version, [sequence],
            "Ordered immediate-context output state identifies a new whole-resource content epoch.",
            {"roles": sorted(set(roles)), "version": next_version,
             "versionScope": "whole-resource-conservative"},
        )
        current_version[resource_id] = version
        return version

    def view_resource(view_id: str | None) -> str | None:
        view = views.get(view_id or "")
        return view and view["payload"].get("resourceObservationId")

    def active_output_resources() -> set[str]:
        result: set[str] = set()
        binding = target_bindings.get(active_target_binding or "")
        if binding:
            target_payload = binding["payload"]
            for view_id in target_payload.get("renderTargetObservationIds", []):
                resource_id = view_resource(view_id)
                if resource_id:
                    result.add(resource_id)
            resource_id = view_resource(target_payload.get("depthTargetObservationId"))
            if resource_id:
                result.add(resource_id)
        for view_id in uav_state.values():
            resource_id = view_resource(view_id)
            if resource_id:
                result.add(resource_id)
        return result

    def clear_conflicting_srvs(output_resources: set[str]) -> int:
        cleared = 0
        for key, view_id in list(srv_state.items()):
            if view_id and view_resource(view_id) in output_resources:
                srv_state[key] = None
                cleared += 1
        return cleared

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
                active_target_binding = binding_id
                hazard_adjustment_count += clear_conflicting_srvs(active_output_resources())
        elif event_type == "resource-view-bind":
            key = (str(payload.get("stage")), int(payload.get("slot", 0)))
            view_id = payload.get("viewObservationId")
            if payload.get("bindingKind") == "shader-resource":
                resource_id = view_resource(view_id)
                if view_id and resource_id and resource_id in active_output_resources():
                    srv_state[key] = None
                    hazard_adjustment_count += 1
                else:
                    srv_state[key] = view_id
            else:
                uav_state[key] = view_id
                resource_id = view_resource(view_id)
                if resource_id:
                    hazard_adjustment_count += clear_conflicting_srvs({resource_id})
        elif event_type in {"draw", "dispatch", "resource-flow"}:
            operation = payload.get("operation", event_type)
            execution_kind = "draw" if event_type == "draw" else ("dispatch" if event_type == "dispatch" else
                ("copy" if operation in {"copy-resource", "copy-subresource-region", "resolve-subresource", "copy-structure-count"}
                 else "resource-operation"))
            execution_attributes = {
                "eventSequence": sequence,
                "commandStreamSequence": event.get("execution", {}).get("commandStreamSequence"),
                "cpuFrame": event.get("frame", {}).get("cpuFrame"),
                "eye": event.get("frame", {}).get("eye", "unknown"),
                "operation": operation,
            }
            if event_type == "resource-flow":
                execution_attributes["sourceSubresource"] = payload.get("sourceSubresource")
                execution_attributes["destinationSubresource"] = payload.get("destinationSubresource")
            execution = graph.node(
                f"event-{sequence}", execution_kind, f"{operation} at event {sequence}", sequence,
                execution_attributes,
            )

            read_views: list[tuple[str, str, int]] = []
            write_views: list[tuple[str, str, int]] = []
            direct_reads: list[tuple[str, str]] = []
            direct_writes: list[tuple[str, str]] = []
            if event_type == "draw":
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
                role_names = {
                    "generate-mips": ("mip-source", "mip-destination"),
                    "copy-structure-count": ("structure-count-source", "structure-count-destination"),
                    "update-subresource": ("cpu-update-source", "cpu-update-destination"),
                    "clear-render-target": ("clear-source", "render-target-clear-destination"),
                    "clear-unordered-access": ("clear-source", "unordered-access-clear-destination"),
                    "clear-depth-stencil": ("clear-source", "depth-stencil-clear-destination"),
                }
                source_role, destination_role = role_names.get(operation, ("copy-source", "copy-destination"))
                if source:
                    direct_reads.append((source, source_role))
                if destination:
                    direct_writes.append((destination, destination_role))

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

            read_roles: dict[str, list[str]] = {}
            write_roles: dict[str, list[str]] = {}
            for resource_id, role in direct_reads:
                read_roles.setdefault(resource_id, []).append(role)
            for resource_id, role in direct_writes:
                write_roles.setdefault(resource_id, []).append(role)

            for resource_id, roles in sorted(read_roles.items(), key=lambda item: observation_sort_key(item[0])):
                resource = resources.get(resource_id)
                if not resource:
                    graph.gap(f"Execution event {sequence} reads undeclared resource {resource_id}.", [execution], True)
                    continue
                version = ensure_version(resource_id)
                if not version:
                    continue
                graph.edge(
                    "reads", version, execution, [resource["sequence"], sequence],
                    "Ordered immediate-context state identifies the current content version as an execution input.",
                    {"roles": sorted(set(roles)), "versionScope": "whole-resource-conservative"},
                )
                writer = last_writer.get(resource_id)
                if writer and writer[0] != execution:
                    graph.edge(
                        "precedes", writer[0], execution, [writer[1], sequence],
                        "A read-after-write dependency is derived from ordered access to the same allocation.",
                        {"hazard": "RAW", "resourceObservationId": resource_id,
                         "versionScope": "whole-resource-conservative"}, "correlated", "high",
                    )
                readers_since_write.setdefault(resource_id, {})[execution] = sequence

            for resource_id, roles in sorted(write_roles.items(), key=lambda item: observation_sort_key(item[0])):
                resource = resources.get(resource_id)
                if not resource:
                    graph.gap(f"Execution event {sequence} writes undeclared resource {resource_id}.", [execution], True)
                    continue
                ensure_version(resource_id)
                writer = last_writer.get(resource_id)
                if writer and writer[0] != execution:
                    graph.edge(
                        "precedes", writer[0], execution, [writer[1], sequence],
                        "A write-after-write dependency is derived from ordered access to the same allocation.",
                        {"hazard": "WAW", "resourceObservationId": resource_id,
                         "versionScope": "whole-resource-conservative"}, "correlated", "high",
                    )
                for reader, reader_sequence in readers_since_write.get(resource_id, {}).items():
                    if reader != execution:
                        graph.edge(
                            "precedes", reader, execution, [reader_sequence, sequence],
                            "A write-after-read dependency is derived from ordered access to the same allocation.",
                            {"hazard": "WAR", "resourceObservationId": resource_id,
                             "versionScope": "whole-resource-conservative"}, "correlated", "high",
                        )
                write_version(resource_id, execution, sequence, roles)
                last_writer[resource_id] = (execution, sequence)
                readers_since_write[resource_id] = {}

    completion = manifest.get("completion", {})
    if manifest.get("status") != "complete" or completion.get("truncated"):
        graph.gap("The source capture is incomplete or truncated; absence of an edge is not evidence of absence.", blocking=True)
    if not resources:
        graph.gap("The capture contains no typed resource declarations.", blocking=True)
    graph.gap(
        "Resource versions and hazard edges are allocation-wide. Exact view-subresource overlap and the actual state returned by D3D11 hazard resolution are not yet observed.",
        blocking=False, kind="unsupported-route",
    )
    graph.hazard_adjustment_count = hazard_adjustment_count
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
    if not graph.is_acyclic():
        raise ValueError("derived resource-version graph contains a cycle")
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
        "schema": {"name": "csx.derived-render-graph", "major": 1, "minor": 2, "producerVersion": "resource-versions-1"},
        "reportId": f"render-graph-{manifest['captureId'].removeprefix('capture-')}",
        "generatedAtUtc": manifest.get("createdAtUtc", "1970-01-01T00:00:00Z"),
        "generatedBy": {"name": "csx-render-map-join", "version": "0.3.0", "gitCommit": git_commit(repo)},
        "inputs": inputs,
        "nodes": graph.nodes,
        "edges": graph.edges,
        "ambiguities": [],
        "gaps": graph.gaps,
        "decisionWindows": [],
        "extensions": {
            "csx.executionGranularity": "individual-immediate-context-call",
            "csx.resourceVersionModel": "whole-resource-write-epoch-v1",
            "csx.hazardModel": "conservative-same-allocation-v1",
            "csx.effectiveStateAdjustments": graph.hazard_adjustment_count,
            "csx.graphAcyclic": True,
            "csx.deferredContextCoverage": False,
            "csx.vrEyeAttribution": False,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "nodes": len(graph.nodes), "edges": len(graph.edges), "gaps": len(graph.gaps), "sha256": sha256(args.output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
