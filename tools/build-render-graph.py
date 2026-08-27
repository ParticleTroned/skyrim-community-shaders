#!/usr/bin/env python3
"""Derive an evidence-bearing resource-flow graph from a CSX render-map capture."""

from __future__ import annotations

import argparse
from collections import deque
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
        self.ambiguities: list[dict[str, Any]] = []
        self.gaps: list[dict[str, Any]] = []
        self.decision_windows: list[dict[str, Any]] = []
        self._node_ids: dict[tuple[str, str], str] = {}
        self._kind_counts: dict[str, int] = {}
        self.eye_attribution_observed = False
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
             confidence: str = "confirmed") -> str:
        attributes = attributes or {}
        edge_key = (edge_type, source, target, json.dumps(attributes, sort_keys=True))
        if edge_key in self._edge_keys:
            return next(
                edge["id"] for edge in self.edges
                if (edge["type"], edge["from"], edge["to"], json.dumps(edge["attributes"], sort_keys=True)) == edge_key
            )
        self._edge_keys.add(edge_key)
        edge_id = f"edge-{len(self.edges) + 1:04d}"
        self.edges.append({
            "id": edge_id,
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
        return edge_id

    def ambiguity(self, question: str, candidate_edge_ids: list[str], resolution_required: str) -> str | None:
        edge_ids = sorted(set(candidate_edge_ids))
        if len(edge_ids) < 2:
            return None
        ambiguity_id = f"ambiguity-{len(self.ambiguities) + 1:04d}"
        self.ambiguities.append({
            "id": ambiguity_id,
            "question": question,
            "candidateEdgeIds": edge_ids,
            "resolutionRequired": resolution_required,
            "extensions": {},
        })
        for edge in self.edges:
            if edge["id"] in edge_ids:
                edge["ambiguityGroup"] = ambiguity_id
        return ambiguity_id

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
    resource_versions: dict[str, dict[str, Any]] = {}
    submissions: dict[str, dict[str, Any]] = {}
    submissions_by_candidate: dict[tuple[int, int], list[dict[str, Any]]] = {}
    draws_by_submission: dict[str, dict[str, Any]] = {}
    draw_output_resources_by_submission: dict[str, list[str]] = {}
    candidates: list[dict[str, Any]] = []
    results_by_frame: dict[int, dict[str, Any]] = {}
    eye_submissions_by_frame: dict[int, list[dict[str, Any]]] = {}
    last_event_by_frame: dict[int, dict[str, Any]] = {}
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

    def write_version(
        resource_id: str, execution: str, sequence: int, roles: list[str], preserves_prior: bool
    ) -> str | None:
        allocation = resource_node(resource_id)
        resource = resources.get(resource_id)
        if not allocation or not resource:
            return None
        previous_version = current_version.get(resource_id)
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
        if preserves_prior and previous_version and previous_version != version:
            graph.edge(
                "carries-forward", previous_version, version, [sequence],
                "A draw or dispatch may preserve prior allocation contents outside the pixels or elements it updates; exact pixel survival is not observed.",
                {"resourceObservationId": resource_id, "versionScope": "whole-resource-conservative"},
                "correlated", "medium",
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
        cpu_frame = event.get("frame", {}).get("cpuFrame")
        if isinstance(cpu_frame, int):
            last_event_by_frame[cpu_frame] = event
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
                    allocation_node = graph.node(
                        resource_id, "resource", resource_id, resource["sequence"], resource["payload"]
                    )
                    graph.edge(
                        "versions", allocation_node, version_node,
                        [resource["sequence"], sequence],
                        "A write epoch versions this observed resource and subresource range.",
                    )
                else:
                    graph.gap(
                        f"Resource version {version_id} refers to undeclared resource {resource_id}.",
                        [version_node], True,
                    )
        elif event_type == "visibility-candidate":
            producer_frame = payload.get("producerFrame")
            object_index = payload.get("objectIndex")
            candidate_node = graph.node(
                f"candidate-{producer_frame}-{object_index}-{sequence}", "visibility-test",
                f"visibility candidate {object_index} for frame {producer_frame}",
                sequence, payload,
            )
            if isinstance(producer_frame, int) and isinstance(object_index, int):
                candidates.append({
                    "event": event,
                    "node": candidate_node,
                    "producerFrame": producer_frame,
                    "objectIndex": object_index,
                })
            else:
                graph.gap(
                    f"Visibility candidate event {sequence} lacks a numeric producer frame or object index.",
                    [candidate_node], True,
                )
        elif event_type == "visibility-result-ready":
            producer_frame = payload.get("producerFrame")
            if isinstance(producer_frame, int):
                results_by_frame[producer_frame] = event
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
                    producer_frame = version["payload"].get("producerFrame")
                    object_index = payload.get("objectIndex")
                    if isinstance(producer_frame, int) and isinstance(object_index, int):
                        submissions_by_candidate.setdefault((producer_frame, object_index), []).append({
                            "event": event,
                            "node": submission_node,
                            "version": version,
                        })
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
            if isinstance(cpu_frame, int):
                eye_submissions_by_frame.setdefault(cpu_frame, []).append({
                    "event": event,
                    "node": eye_node,
                    "resourceObservationId": resource_id,
                })
            resource = resources.get(resource_id)
            if resource:
                ensure_version(resource_id)
                content_version_node = current_version.get(resource_id)
                if content_version_node:
                    graph.edge(
                        "presents", content_version_node, eye_node,
                        [resource["sequence"], sequence],
                        "The accepted OpenVR submission uses the current observed content version of this texture allocation.",
                        {"versionScope": "whole-resource-conservative"},
                    )
                allocation_node = resource_node(resource_id)
                if allocation_node:
                    graph.edge(
                        "uses", allocation_node, eye_node,
                        [resource["sequence"], sequence],
                        "The accepted OpenVR submission names this exact D3D11 texture allocation.",
                    )
                graph.eye_attribution_observed = True
            else:
                graph.gap(
                    f"Eye submission event {sequence} refers to undeclared resource {resource_id}.",
                    [eye_node], True,
                )
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
                    draws_by_submission[submission_id] = event
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
                write_version(
                    resource_id, execution, sequence, roles,
                    preserves_prior=event_type in {"draw", "dispatch"},
                )
                last_writer[resource_id] = (execution, sequence)
                readers_since_write[resource_id] = {}

            if event_type == "draw":
                submission_id = event.get("submissionObservationId") or payload.get("submissionObservationId")
                if submission_id:
                    draw_output_resources_by_submission[submission_id] = sorted(
                        write_roles, key=observation_sort_key
                    )

    def eye_routes(
        draw: dict[str, Any], submission_id: str | None
    ) -> tuple[list[dict[str, Any]], str, str, bool]:
        if not submission_id:
            return [], "not-proven", "The draw has no explicit visibility-submission identity.", False
        draw_sequence = draw["sequence"]
        draw_frame = draw.get("frame", {}).get("cpuFrame")
        output_resources = draw_output_resources_by_submission.get(submission_id, [])
        if not output_resources:
            return [], "not-proven", "The selected draw has no observed output resource.", False

        draw_node = graph.node(
            f"event-{draw_sequence}", "draw",
            f"{draw.get('payload', {}).get('operation', 'draw')} at event {draw_sequence}",
            draw_sequence, {
                "eventSequence": draw_sequence,
                "commandStreamSequence": draw.get("execution", {}).get("commandStreamSequence"),
                "cpuFrame": draw_frame,
                "eye": draw.get("frame", {}).get("eye", "unknown"),
                "operation": draw.get("payload", {}).get("operation", "draw"),
            },
        )

        route_edge_types = {"writes", "reads", "carries-forward", "presents"}
        adjacency: dict[str, list[dict[str, Any]]] = {}
        for edge in graph.edges:
            if edge["type"] in route_edge_types:
                adjacency.setdefault(edge["from"], []).append(edge)

        def graph_paths(target: str) -> tuple[list[list[dict[str, Any]]], bool]:
            paths: list[list[dict[str, Any]]] = []
            queue = deque([(draw_node, [], {draw_node})])
            expansions = 0
            while queue and len(paths) < 16 and expansions < 50_000:
                node, path, visited = queue.popleft()
                if node == target:
                    paths.append(path)
                    continue
                for edge in adjacency.get(node, []):
                    if edge["to"] in visited:
                        continue
                    queue.append((edge["to"], [*path, edge], {*visited, edge["to"]}))
                expansions += 1
            return paths, bool(queue)

        routes: list[dict[str, Any]] = []
        search_truncated = False
        nodes_by_id = {node["id"]: node for node in graph.nodes}
        for eye_submission in eye_submissions_by_frame.get(draw_frame, []):
            event = eye_submission["event"]
            submitted_resource = eye_submission["resourceObservationId"]
            if event["sequence"] <= draw_sequence or not submitted_resource:
                continue
            paths, path_search_truncated = graph_paths(eye_submission["node"])
            search_truncated = search_truncated or path_search_truncated
            for path in paths:
                if not path or path[0]["type"] != "writes":
                    continue
                resource_path: list[str] = []
                transfer_operations: list[str] = []
                for edge in path:
                    target_node = nodes_by_id.get(edge["to"], {})
                    allocation_id = target_node.get("attributes", {}).get("allocationObservationId")
                    if allocation_id and (not resource_path or resource_path[-1] != allocation_id):
                        resource_path.append(allocation_id)
                    operation = target_node.get("attributes", {}).get("operation")
                    if target_node.get("kind") == "copy" and operation:
                        transfer_operations.append(operation)
                if not resource_path or resource_path[0] not in output_resources or resource_path[-1] != submitted_resource:
                    continue
                event_sequences = sorted({
                    item for edge in path for evidence in edge["evidence"]
                    for item in evidence["eventSequences"] if item >= draw_sequence
                })
                routes.append({
                    "eye": event.get("payload", {}).get("eye", "unknown"),
                    "eyeSubmitNode": eye_submission["node"],
                    "sourceResourceObservationId": resource_path[0],
                    "submittedResourceObservationId": submitted_resource,
                    "submittedBounds": event.get("payload", {}).get("bounds"),
                    "mechanism": "same-allocation" if len(resource_path) == 1 else "resource-flow",
                    "eventSequences": event_sequences,
                    "resourceObservationIds": resource_path,
                    "transferOperations": transfer_operations,
                    "confidence": "medium" if any(edge["type"] == "carries-forward" for edge in path) else "high",
                })

        unique_routes: dict[tuple[Any, ...], dict[str, Any]] = {}
        for route in routes:
            key = (
                route["eyeSubmitNode"], route["sourceResourceObservationId"],
                tuple(route["eventSequences"]), tuple(route["resourceObservationIds"]),
            )
            unique_routes[key] = route
        routes = sorted(
            unique_routes.values(),
            key=lambda route: (route["eventSequences"][-1], route["sourceResourceObservationId"], route["eventSequences"]),
        )

        eyes = {route["eye"] for route in routes}
        complete = "both" in eyes or {"left", "right"}.issubset(eyes)
        routes_per_eye: dict[str, int] = {}
        for route in routes:
            routes_per_eye[route["eye"]] = routes_per_eye.get(route["eye"], 0) + 1
        ambiguous = any(count > 1 for count in routes_per_eye.values())
        if complete and search_truncated:
            return routes, "ambiguous", "Both-eye reachability is observed, but the bounded route search found more alternatives than it could enumerate.", True
        if complete and ambiguous:
            return routes, "ambiguous", "Both-eye reachability is observed, but at least one eye has multiple valid resource routes.", False
        if complete:
            return routes, "observed", "The selected draw reaches accepted submissions covering both OpenVR eyes in the same CPU frame.", False
        if routes:
            suffix = " The bounded route search was truncated." if search_truncated else ""
            return routes, "not-proven", "Only partial eye coverage is observed for the selected draw in the same CPU frame." + suffix, search_truncated
        suffix = " The bounded route search was truncated." if search_truncated else ""
        return routes, "not-proven", "No same-frame resource route connects the selected draw to an accepted OpenVR eye submission." + suffix, search_truncated

    def event_point(event: dict[str, Any], readiness_domain: str) -> dict[str, Any]:
        return {
            "captureId": capture_id,
            "sequence": event["sequence"],
            "timestampQpc": event["timestampQpc"],
            "cpuFrame": event.get("frame", {}).get("cpuFrame"),
            "eye": event.get("frame", {}).get("eye", "unknown"),
            "readinessDomain": readiness_domain,
            "gpuTimestampTicks": event.get("execution", {}).get("gpuTimestampTicks"),
        }

    for candidate in sorted(candidates, key=lambda item: item["event"]["sequence"]):
        candidate_event = candidate["event"]
        candidate_node = candidate["node"]
        producer_frame = candidate["producerFrame"]
        object_index = candidate["objectIndex"]
        ready_event = results_by_frame.get(producer_frame)
        version_id = ready_event and ready_event.get("payload", {}).get("resourceVersionObservationId")
        version = resource_versions.get(version_id)
        if version:
            version_node = graph.node(
                version_id, "resource-version", version_id, version["sequence"], version["payload"]
            )
            graph.edge(
                "tests", candidate_node, version_node,
                [candidate_event["sequence"], ready_event["sequence"]],
                "The current-frame visibility resource version contains this indexed candidate's result.",
                {"objectIndex": object_index, "producerFrame": producer_frame},
            )

        matched_submission = None
        matched_draw = None
        for submission in submissions_by_candidate.get((producer_frame, object_index), []):
            submission_id = submission["event"].get("payload", {}).get("submissionObservationId")
            draw = draws_by_submission.get(submission_id)
            if draw and (not ready_event or submission["event"]["sequence"] > ready_event["sequence"]):
                matched_submission = submission
                matched_draw = draw
                break

        if ready_event:
            visibility_available = event_point(ready_event, "gpu-resource-consumable")
        else:
            visibility_available = event_point(candidate_event, "unknown")

        if matched_draw:
            deadline_event = matched_draw
            decision_deadline = event_point(deadline_event, "gpu-ordered")
        else:
            later_eye_submissions = [
                item["event"] for item in eye_submissions_by_frame.get(producer_frame, [])
                if item["event"]["sequence"] > visibility_available["sequence"]
            ]
            deadline_event = later_eye_submissions[0] if later_eye_submissions else last_event_by_frame.get(producer_frame, candidate_event)
            decision_deadline = event_point(deadline_event, "cpu-observed")

        viable = False
        if ready_event and matched_submission and matched_draw and version:
            submission_event = matched_submission["event"]
            ready_command = ready_event.get("execution", {}).get("commandStreamSequence")
            submission_command = submission_event.get("execution", {}).get("commandStreamSequence")
            draw_command = matched_draw.get("execution", {}).get("commandStreamSequence")
            same_context = (
                ready_event.get("deviceContextObservationId") is not None and
                ready_event.get("deviceContextObservationId") == submission_event.get("deviceContextObservationId") == matched_draw.get("deviceContextObservationId")
            )
            ordered = (
                isinstance(ready_command, int) and isinstance(submission_command, int) and isinstance(draw_command, int) and
                ready_command < submission_command <= draw_command
            )
            viable = (
                same_context and ordered and
                ready_event["sequence"] < submission_event["sequence"] < matched_draw["sequence"] and
                matched_draw.get("frame", {}).get("cpuFrame") == producer_frame and
                submission_event.get("payload", {}).get("resourceVersionObservationId") == version_id and
                submission_event.get("payload", {}).get("bindingMatches") is True
            )

        evidence_sequences = [candidate_event["sequence"], visibility_available["sequence"], deadline_event["sequence"]]
        if matched_submission:
            evidence_sequences.append(matched_submission["event"]["sequence"])
        forced_visible = bool(matched_submission and matched_submission["event"].get("payload", {}).get("forcedVisible"))
        submission_id = matched_submission and matched_submission["event"].get("payload", {}).get("submissionObservationId")
        routes, eye_coverage_result, eye_coverage_reason, route_search_truncated = eye_routes(matched_draw, submission_id) if matched_draw else (
            [], "not-proven", "No explicitly associated draw was observed.", False
        )
        route_edge_ids: list[str] = []
        if matched_draw:
            draw_node = graph.node(
                f"event-{matched_draw['sequence']}", "draw",
                f"{matched_draw.get('payload', {}).get('operation', 'draw')} at event {matched_draw['sequence']}",
                matched_draw["sequence"], {
                    "eventSequence": matched_draw["sequence"],
                    "commandStreamSequence": matched_draw.get("execution", {}).get("commandStreamSequence"),
                    "cpuFrame": matched_draw.get("frame", {}).get("cpuFrame"),
                    "eye": matched_draw.get("frame", {}).get("eye", "unknown"),
                    "operation": matched_draw.get("payload", {}).get("operation", "draw"),
                },
            )
            for route in routes:
                route_edge_ids.append(graph.edge(
                    "contributes-to", draw_node, route["eyeSubmitNode"], route["eventSequences"],
                    "The draw output allocation reaches this accepted OpenVR submission through the observed same-frame resource route; exact pixel survival remains correlated.",
                    {
                        "eye": route["eye"],
                        "mechanism": route["mechanism"],
                        "resourceObservationIds": route["resourceObservationIds"],
                        "transferOperations": route["transferOperations"],
                        "routeEventSequences": route["eventSequences"],
                    }, "correlated", route["confidence"],
                ))
        ambiguity_ids: list[str] = []
        routes_by_eye: dict[str, list[str]] = {}
        for route, edge_id in zip(routes, route_edge_ids):
            routes_by_eye.setdefault(route["eye"], []).append(edge_id)
        for eye, edge_ids in sorted(routes_by_eye.items()):
            ambiguity_id = graph.ambiguity(
                f"Which observed resource route carries the selected draw to the {eye} eye submission?",
                edge_ids,
                "Capture narrower target state or subresource-preservation evidence to select one route.",
            )
            if ambiguity_id:
                ambiguity_ids.append(ambiguity_id)
        if viable:
            control_note = " The final value was deliberately forced visible for the control." if forced_visible else ""
            result = "viable"
            reason = (
                "The candidate, current-frame visibility version, effective t127 binding, and explicitly associated draw "
                "share one producer frame and increase monotonically in the same immediate-context command stream."
                + control_note
            )
        else:
            result = "not-proven"
            reason = (
                "No complete same-frame candidate-to-version-to-effective-binding-to-draw chain was observed before "
                "the captured decision deadline."
            )

        graph.decision_windows.append({
            "id": f"decision-window-{len(graph.decision_windows) + 1:04d}",
            "candidateNode": candidate_node,
            "suppressionStage": "vertex-shader",
            "visibilityAvailable": visibility_available,
            "decisionDeadline": decision_deadline,
            "result": result,
            "savings": ["gpu-vertex", "gpu-pixel"],
            "evidence": [{
                "captureId": capture_id,
                "eventSequences": sorted(set(evidence_sequences)),
                "engineEvidenceRefs": [],
                "note": "Decision-window evidence uses explicit producer-frame, resource-version, binding, submission, and draw identities.",
            }],
            "reason": reason,
            "eyeCoverage": {
                "result": eye_coverage_result,
                "eyes": sorted({route["eye"] for route in routes}),
                "physicalSubmissionCount": len({route["eyeSubmitNode"] for route in routes}),
                "stereoMechanism": (
                    "single-both-eye-submission" if {route["eye"] for route in routes} == {"both"} else
                    "shared-resource-distinct-bounds" if {"left", "right"}.issubset({route["eye"] for route in routes}) and
                    len({route["submittedResourceObservationId"] for route in routes}) == 1 and
                    len({json.dumps(route["submittedBounds"], sort_keys=True) for route in routes}) > 1 else
                    "shared-resource-same-bounds" if {"left", "right"}.issubset({route["eye"] for route in routes}) and
                    len({route["submittedResourceObservationId"] for route in routes}) == 1 else
                    "distinct-resources" if {"left", "right"}.issubset({route["eye"] for route in routes}) else
                    "not-proven"
                ),
                "routes": routes,
                "ambiguityIds": ambiguity_ids,
                "searchTruncated": route_search_truncated,
                "reason": eye_coverage_reason,
            },
            "extensions": {
                "csx.objectIndex": object_index,
                "csx.producerFrame": producer_frame,
                "csx.resourceVersionObservationId": version_id,
                "csx.submissionObservationId": matched_submission and matched_submission["event"].get("payload", {}).get("submissionObservationId"),
                "csx.bindingMatches": matched_submission and matched_submission["event"].get("payload", {}).get("bindingMatches"),
                "csx.forcedVisible": forced_visible,
            },
        })

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
        "schema": {"name": "csx.derived-render-graph", "major": 1, "minor": 3, "producerVersion": "resource-versions-1"},
        "reportId": f"render-graph-{manifest['captureId'].removeprefix('capture-')}",
        "generatedAtUtc": manifest.get("createdAtUtc", "1970-01-01T00:00:00Z"),
        "generatedBy": {"name": "csx-render-map-join", "version": "0.4.0", "gitCommit": git_commit(repo)},
        "inputs": inputs,
        "nodes": graph.nodes,
        "edges": graph.edges,
        "ambiguities": graph.ambiguities,
        "gaps": graph.gaps,
        "decisionWindows": graph.decision_windows,
        "extensions": {
            "csx.executionGranularity": "individual-immediate-context-call",
            "csx.resourceVersionModel": "whole-resource-write-epoch-v1",
            "csx.hazardModel": "conservative-same-allocation-v1",
            "csx.effectiveStateAdjustments": graph.hazard_adjustment_count,
            "csx.graphAcyclic": True,
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
