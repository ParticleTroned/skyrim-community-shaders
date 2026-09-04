#!/usr/bin/env python3
"""Derive an evidence-bearing resource-flow graph from a CSX render-map capture."""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
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


def input_schema_major(kind: str, data: dict[str, Any]) -> int:
    schema = data.get("schema")
    if isinstance(schema, dict) and isinstance(schema.get("major"), int):
        return schema["major"]
    if kind == "shader-manifest" and isinstance(data.get("schemaVersion"), int):
        return data["schemaVersion"]
    return 1


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
        self.material_input_match_count = 0
        self.prepared_geometry_draw_count = 0
        self.effective_state_contract = False
        self.effective_state_query_count = 0
        self.effective_state_verified_slots = 0
        self.effective_state_mismatch_slots = 0
        self.cpu_access_count = 0
        self.cpu_read_visibility_count = 0
        self.cpu_write_publication_count = 0
        self.cpu_map_stall_ticks = 0
        self.cpu_map_max_stall_ticks = 0
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


def parse_descriptor_pair(value: Any) -> tuple[int, int] | None:
    if not isinstance(value, str):
        return None
    match = re.fullmatch(
        r"vertex\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*pixel\s*=\s*(0x[0-9a-fA-F]+|\d+)",
        value,
    )
    if not match:
        return None
    return int(match.group(1), 0), int(match.group(2), 0)


def static_identity_indexes(
    shader_manifest: dict[str, Any] | None,
    engine_map: dict[str, Any] | None,
) -> tuple[dict[str, list[dict[str, Any]]], dict[tuple[str, int, int], list[dict[str, Any]]]]:
    compile_units_by_family: dict[str, list[dict[str, Any]]] = defaultdict(list)
    if shader_manifest:
        for unit in shader_manifest.get("compileUnits", []):
            if unit.get("kind") != "engine-shader-cache-family":
                continue
            source = unit.get("sourceVirtualPath")
            if isinstance(source, str) and source:
                compile_units_by_family[Path(source).stem.casefold()].append(unit)

    techniques_by_pair: dict[tuple[str, int, int], list[dict[str, Any]]] = defaultdict(list)
    if engine_map:
        for entity in engine_map.get("entities", []):
            if entity.get("kind") != "technique":
                continue
            attributes = entity.get("attributes", {})
            shader_type = attributes.get("shaderType")
            descriptor_pair = parse_descriptor_pair(attributes.get("techniqueId"))
            if isinstance(shader_type, str) and descriptor_pair:
                techniques_by_pair[(shader_type.casefold(), *descriptor_pair)].append(entity)

    return dict(compile_units_by_family), dict(techniques_by_pair)


def compatibility_requirement(
    registrations: list[dict[str, Any]], shader_family: str, shader_source: str
) -> dict[str, Any]:
    family = shader_family.casefold()
    source = shader_source.replace("\\", "/").casefold()
    applicable: list[dict[str, Any]] = []
    for registration in registrations:
        applies = False
        for scope in registration.get("scopes", []):
            kind = scope.get("kind")
            value = str(scope.get("value", "")).casefold()
            if kind == "global":
                applies = True
            elif kind == "shader-family" and value == family:
                applies = True
            elif kind == "shader-source" and (
                value == source or (source.endswith("/" + value) and len(source) > len(value))
            ):
                applies = True
            if applies:
                break
        if applies:
            applicable.append(registration)
    applicable.sort(key=lambda item: str(item.get("identity", "")))
    canonical = "".join(
        f"{len(str(item.get('canonical', '')).encode('utf-8'))}:{item.get('canonical', '')}\n"
        for item in applicable
    )
    return {
        "canonicalSha256": hashlib.sha256(canonical.encode("utf-8")).hexdigest(),
        "registrationIdentities": [item.get("identity") for item in applicable],
        "registrationHandles": [item.get("handle") for item in applicable],
    }


def derive(
    manifest: dict[str, Any],
    events: list[dict[str, Any]],
    shader_manifest: dict[str, Any] | None = None,
    engine_map: dict[str, Any] | None = None,
) -> Graph:
    capture_id = manifest["captureId"]
    graph = Graph(capture_id)
    resources: dict[str, dict[str, Any]] = {}
    views: dict[str, dict[str, Any]] = {}
    target_bindings: dict[str, dict[str, Any]] = {}
    resource_versions: dict[str, dict[str, Any]] = {}
    cpu_maps: dict[str, dict[str, Any]] = {}
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
    effective_state_contract = any(
        event.get("type") == "resource-view-state-observed" for event in events
    )
    predicted_srv_state: dict[tuple[str, int], str | None] = {}
    predicted_uav_state: dict[tuple[str, int], str | None] = {}
    predicted_target_binding: str | None = None
    effective_state_query_count = 0
    effective_state_verified_slots = 0
    effective_state_mismatch_slots = 0
    version_number: dict[str, int] = {}
    current_version: dict[str, str] = {}
    last_writer: dict[str, tuple[str, int]] = {}
    readers_since_write: dict[str, dict[str, int]] = {}
    hazard_adjustment_count = 0
    hazard_overlap_fallback_count = 0
    stage_shader_payloads: dict[str, dict[str, Any]] = {}
    scene_objects: dict[str, dict[str, Any]] = {}
    geometry_observations: dict[str, dict[str, Any]] = {}
    material_observations: dict[str, dict[str, Any]] = {}
    geometry_setup_bindings: dict[str, dict[str, Any]] = {}
    device_contexts: dict[str, dict[str, Any]] = {}
    command_recordings: dict[str, dict[str, Any]] = {}
    command_lists: dict[str, dict[str, Any]] = {}
    typed_identity_specs = {
        "device-context-observed": ("device-context", "deviceContextObservationId"),
        "command-recording-observed": ("command-recording", "commandRecordingObservationId"),
        "command-list-observed": ("command-list", "commandListObservationId"),
    }
    typed_identity_declarations: dict[tuple[str, str], str] = {}
    typed_identity_kinds: dict[str, set[str]] = defaultdict(set)
    conflicted_typed_identities: set[tuple[str, str]] = set()
    for declaration_event in events:
        spec = typed_identity_specs.get(str(declaration_event.get("type")))
        if not spec:
            continue
        identity_kind, identity_field = spec
        declaration_payload = declaration_event.get("payload", {})
        identity_id = declaration_payload.get(identity_field)
        if not isinstance(identity_id, str):
            continue
        identity_key = (identity_kind, identity_id)
        semantic_declaration = {
            "payload": declaration_payload,
            "envelope": {
                "deviceContextObservationId": declaration_event.get("deviceContextObservationId"),
                "commandRecordingObservationId": declaration_event.get("commandRecordingObservationId"),
                "observationDomain": declaration_event.get("execution", {}).get("observationDomain"),
            },
        }
        signature = json.dumps(semantic_declaration, sort_keys=True, separators=(",", ":"))
        previous = typed_identity_declarations.get(identity_key)
        if previous is not None and previous != signature:
            conflicted_typed_identities.add(identity_key)
        else:
            typed_identity_declarations.setdefault(identity_key, signature)
        typed_identity_kinds[identity_id].add(identity_kind)
    for identity_id, identity_kinds in typed_identity_kinds.items():
        if len(identity_kinds) > 1:
            conflicted_typed_identities.update((kind, identity_id) for kind in identity_kinds)
    reported_typed_identity_conflicts: set[tuple[str, str]] = set()
    compile_units_by_family, techniques_by_pair = static_identity_indexes(shader_manifest, engine_map)
    qpc_frequency = int(manifest.get("clock", {}).get("frequencyHz") or 0)
    shader_compilation = manifest.get("extensions", {}).get("csx.shaderCompilation", {})
    compile_context_node: str | None = None
    compatibility_registrations: list[dict[str, Any]] = []
    if shader_compilation.get("availability") == "observed":
        compile_state = shader_compilation.get("globalCompileState", {})
        digest = compile_state.get("digest")
        compile_context_node = graph.node(
            f"shader-compilation-{digest or 'unknown'}",
            "shader-compilation-context",
            f"capture-start shader compilation context {digest or 'unknown'}",
            None,
            shader_compilation,
            [{"kind": "capture-manifest", "value": "extensions.csx.shaderCompilation"}],
        )
        registry = shader_compilation.get("compatibilityRegistry", {})
        if registry.get("complete") is True and isinstance(registry.get("registrations"), list):
            compatibility_registrations = registry["registrations"]
        else:
            graph.gap(
                "The capture-start shader compatibility registration snapshot is incomplete; "
                "shader-specific compile requirements cannot be fully derived.",
                [compile_context_node], True, "incomplete-capture",
            )

    def event_source_refs(event: dict[str, Any], observation_id: str | None = None) -> list[dict[str, Any]]:
        refs: list[dict[str, Any]] = [{"kind": "capture-event", "value": event["sequence"]}]
        if observation_id:
            refs.append({"kind": "observation", "value": observation_id})
        refs.extend({"kind": "shader-manifest", "value": value} for value in event.get("manifestRefs", []))
        refs.extend({"kind": "engine-map", "value": value} for value in event.get("engineRefs", []))
        return refs

    def observed_identity_node(
        event: dict[str, Any], observation_id: str | None, kind: str, label: str,
        attributes: dict[str, Any] | None = None,
    ) -> str | None:
        if not observation_id:
            return None
        return graph.node(
            observation_id, kind, label, event["sequence"],
            attributes if attributes is not None else event.get("payload", {}),
            event_source_refs(event, observation_id),
        )

    def typed_identity_is_valid(kind: str, identity_id: str | None, node: str | None = None) -> bool:
        if not identity_id:
            return False
        identity_key = (kind, identity_id)
        if identity_key not in conflicted_typed_identities:
            return True
        if identity_key not in reported_typed_identity_conflicts:
            graph.gap(
                f"Typed {kind} identity {identity_id} has incompatible declarations; "
                "authoritative provenance using this identity was suppressed.",
                [node] if node else [], True, "other",
            )
            reported_typed_identity_conflicts.add(identity_key)
        return False

    def valid_context(context_id: str | None) -> dict[str, Any] | None:
        context = device_contexts.get(context_id or "")
        return context if context and context.get("valid") is True else None

    def valid_recording(recording_id: str | None) -> dict[str, Any] | None:
        recording = command_recordings.get(recording_id or "")
        return recording if recording and recording.get("valid") is True else None

    def valid_command_list(list_id: str | None) -> dict[str, Any] | None:
        command_list = command_lists.get(list_id or "")
        return command_list if command_list and command_list.get("valid") is True else None

    def recording_context_matches(
        recording: dict[str, Any] | None, context_id: str | None,
        related: list[str], description: str,
    ) -> bool:
        if not recording:
            return False
        owner_context_id = recording.get("ownerContextId")
        if context_id == owner_context_id:
            return True
        graph.gap(
            f"{description} names context {context_id}, but recording owner is {owner_context_id}; "
            "authoritative provenance was suppressed.",
            related, True, "other",
        )
        return False

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

    def view_subresource_spans(view_id: str | None) -> list[tuple[int, int]] | None:
        view = views.get(view_id or "")
        if not view:
            return None
        payload = view["payload"]
        resource = resources.get(str(payload.get("resourceObservationId") or ""))
        if not resource:
            return None
        resource_payload = resource["payload"]
        resource_dimension = str(resource_payload.get("dimension") or "")
        if resource_dimension == "buffer":
            # Direct3D 11 defines a buffer as one subresource. Element ranges are
            # view metadata, not distinct D3D11 subresource identities.
            return [(0, 1)]
        if resource_dimension not in {"texture-1d", "texture-2d", "texture-3d"}:
            return None

        mip_levels = max(1, int(resource_payload.get("mipLevels") or 1))
        array_slices = 1 if resource_dimension == "texture-3d" else max(
            1, int(resource_payload.get("depthOrArraySize") or 1)
        )
        raw = payload.get("subresources") or {}
        first_mip = int(raw.get("mipSliceOrFirstMip") or 0)
        first_array = int(raw.get("firstArraySlice") or 0)
        overloaded_count = int(raw.get("arraySizeOrMipCount") or 0)
        second_count = int(raw.get("elementCountOrArraySize") or 0)
        kind = str(payload.get("kind") or "")
        dimension = int(payload.get("viewDimension") or 0)

        mip_count = 1
        array_count = 1
        if kind == "render-target":
            if dimension in {3, 5, 7}:
                array_count = overloaded_count
            elif dimension not in {1, 2, 4, 6, 8}:
                return None
        elif kind == "depth-target":
            if dimension in {2, 4, 6}:
                array_count = overloaded_count
            elif dimension not in {1, 3, 5}:
                return None
        elif kind == "shader-resource-view":
            if dimension in {1, 11}:
                return [(0, 1)]
            if dimension in {2, 4, 8, 9}:
                mip_count = overloaded_count
                if dimension == 9:
                    array_count = 6
            elif dimension in {3, 5}:
                mip_count = overloaded_count
                array_count = second_count
            elif dimension == 6:
                first_mip = 0
            elif dimension == 7:
                first_mip = 0
                array_count = overloaded_count
            elif dimension == 10:
                mip_count = overloaded_count
                array_count = second_count
            else:
                return None
        elif kind == "unordered-access-view":
            if dimension == 1:
                return [(0, 1)]
            if dimension in {3, 5}:
                array_count = overloaded_count
            elif dimension not in {2, 4, 8}:
                return None
        else:
            return None

        def resolve_count(value: int, available: int) -> int | None:
            if available <= 0:
                return None
            if value in {0, 0xFFFFFFFF}:
                return available
            return min(value, available)

        if first_mip < 0 or first_mip >= mip_levels or first_array < 0 or first_array >= array_slices:
            return None
        mip_count = resolve_count(mip_count, mip_levels - first_mip)
        array_count = resolve_count(array_count, array_slices - first_array)
        if not mip_count or not array_count:
            return None
        return [
            (array_index * mip_levels + first_mip, mip_count)
            for array_index in range(first_array, first_array + array_count)
        ]

    def views_overlap(left_view_id: str | None, right_view_id: str | None) -> tuple[bool, bool]:
        if not left_view_id or not right_view_id:
            return False, True
        if view_resource(left_view_id) != view_resource(right_view_id):
            return False, True
        left_spans = view_subresource_spans(left_view_id)
        right_spans = view_subresource_spans(right_view_id)
        if left_spans is None or right_spans is None:
            return True, False
        for left_first, left_count in left_spans:
            left_end = left_first + left_count
            for right_first, right_count in right_spans:
                right_end = right_first + right_count
                if left_first < right_end and right_first < left_end:
                    return True, True
        return False, True

    def conflicts_any(view_id: str | None, output_view_ids: list[str]) -> bool:
        nonlocal hazard_overlap_fallback_count
        for output_view_id in output_view_ids:
            overlaps, exact = views_overlap(view_id, output_view_id)
            if not exact:
                hazard_overlap_fallback_count += 1
            if overlaps:
                return True
        return False

    def output_views(
        target_binding_id: str | None,
        unordered_state: dict[tuple[str, int], str | None],
    ) -> list[str]:
        result: list[str] = []
        binding = target_bindings.get(target_binding_id or "")
        if binding:
            target_payload = binding["payload"]
            for view_id in target_payload.get("renderTargetObservationIds", []):
                if view_id:
                    result.append(view_id)
            depth_view_id = target_payload.get("depthTargetObservationId")
            if depth_view_id:
                result.append(depth_view_id)
        for view_id in unordered_state.values():
            if view_id:
                result.append(view_id)
        return result

    def active_output_views() -> list[str]:
        return output_views(active_target_binding, uav_state)

    def active_output_resources() -> set[str]:
        return {
            resource_id for resource_id in (view_resource(view_id) for view_id in active_output_views())
            if resource_id
        }

    def clear_conflicting_srvs(
        shader_state: dict[tuple[str, int], str | None],
        output_view_ids: list[str],
    ) -> int:
        cleared = 0
        for key, view_id in list(shader_state.items()):
            if view_id and conflicts_any(view_id, output_view_ids):
                shader_state[key] = None
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
        if event_type == "device-context-observed":
            context_id = payload.get("deviceContextObservationId")
            context_node = observed_identity_node(
                event, context_id, "device-context",
                f"{payload.get('kind', 'unknown')} device context {context_id}",
            )
            if context_id and context_node and context_id not in device_contexts:
                valid = typed_identity_is_valid("device-context", context_id, context_node)
                if event.get("deviceContextObservationId") != context_id:
                    graph.gap(
                        f"Device-context declaration {context_id} disagrees with envelope context "
                        f"{event.get('deviceContextObservationId')}; authoritative provenance was suppressed.",
                        [context_node], True, "other",
                    )
                    valid = False
                device_contexts[context_id] = {
                    "event": event, "payload": payload, "node": context_node, "valid": valid,
                }
        elif event_type == "command-recording-observed":
            recording_id = payload.get("commandRecordingObservationId")
            context_id = payload.get("deviceContextObservationId")
            recording_node = observed_identity_node(
                event, recording_id, "command-recording",
                f"deferred recording epoch {payload.get('epoch', 'unknown')}",
            )
            if recording_id and recording_node and recording_id not in command_recordings:
                valid = typed_identity_is_valid("command-recording", recording_id, recording_node)
                if event.get("commandRecordingObservationId") != recording_id:
                    graph.gap(
                        f"Command-recording declaration {recording_id} disagrees with envelope recording "
                        f"{event.get('commandRecordingObservationId')}; authoritative provenance was suppressed.",
                        [recording_node], True, "other",
                    )
                    valid = False
                if event.get("deviceContextObservationId") != context_id:
                    graph.gap(
                        f"Command-recording declaration {recording_id} names context {context_id}, but its "
                        f"envelope names {event.get('deviceContextObservationId')}; authoritative provenance was suppressed.",
                        [recording_node], True, "other",
                    )
                    valid = False
                context = valid_context(context_id)
                if not context:
                    graph.gap(
                        f"Command recording {recording_id} refers to undeclared or invalid context {context_id}.",
                        [recording_node], True,
                    )
                    valid = False
                elif context["payload"].get("kind") != "deferred":
                    graph.gap(
                        f"Command recording {recording_id} is owned by non-deferred context {context_id}; "
                        "authoritative provenance was suppressed.",
                        [context["node"], recording_node], True, "other",
                    )
                    valid = False
                command_recordings[recording_id] = {
                    "event": event, "payload": payload, "node": recording_node,
                    "ownerContextId": context_id, "valid": valid,
                }
                if valid and context:
                    graph.edge(
                        "records", context["node"], recording_node,
                        [context["event"]["sequence"], sequence],
                        "This deferred device context owns the explicitly declared recording epoch.",
                    )
        elif event_type == "command-list-observed":
            list_id = payload.get("commandListObservationId")
            recording_id = payload.get("sourceCommandRecordingObservationId")
            source_context_id = payload.get("sourceDeviceContextObservationId")
            list_node = observed_identity_node(
                event, list_id, "command-list", f"command list {list_id}",
            )
            if list_id and list_node and list_id not in command_lists:
                valid = typed_identity_is_valid("command-list", list_id, list_node)
                recording = valid_recording(recording_id)
                if event.get("commandRecordingObservationId") != recording_id:
                    graph.gap(
                        f"Command-list declaration {list_id} names recording {recording_id}, but its envelope "
                        f"names {event.get('commandRecordingObservationId')}; authoritative provenance was suppressed.",
                        [list_node], True, "other",
                    )
                    valid = False
                if recording_id:
                    if not recording:
                        graph.gap(
                            f"Command list {list_id} has no valid declared source recording {recording_id}.",
                            [list_node], True, "incomplete-capture",
                        )
                        valid = False
                    elif source_context_id != recording.get("ownerContextId"):
                        graph.gap(
                            f"Command list {list_id} names source context {source_context_id}, but recording "
                            f"{recording_id} is owned by {recording.get('ownerContextId')}; authoritative provenance was suppressed.",
                            [recording["node"], list_node], True, "other",
                        )
                        valid = False
                    if event.get("deviceContextObservationId") != source_context_id:
                        graph.gap(
                            f"Command-list declaration {list_id} names source context {source_context_id}, but its "
                            f"envelope names {event.get('deviceContextObservationId')}; authoritative provenance was suppressed.",
                            [list_node], True, "other",
                        )
                        valid = False
                elif source_context_id:
                    graph.gap(
                        f"Command list {list_id} names source context {source_context_id} without a source recording; "
                        "authoritative provenance was suppressed.",
                        [list_node], True, "other",
                    )
                    valid = False
                command_lists[list_id] = {
                    "event": event, "payload": payload, "node": list_node,
                    "sourceContextId": source_context_id, "sourceRecordingId": recording_id,
                    "valid": valid,
                }
                if valid and recording:
                    graph.edge(
                        "materializes", recording["node"], list_node,
                        [recording["event"]["sequence"], sequence],
                        "FinishCommandList materialized this exact observed recording epoch.",
                        {"sourceRecordingComplete": payload.get("sourceRecordingComplete")},
                    )
                    if payload.get("sourceRecordingComplete") is not True:
                        graph.gap(
                            f"Command list {list_id} was materialized from an incomplete source recording: "
                            f"{payload.get('sourceRecordingIncompleteReasons', [])}.",
                            [recording["node"], list_node], False, "incomplete-capture",
                        )
                elif not recording_id:
                    graph.gap(
                        f"Command list {list_id} has no declared source recording {recording_id}; "
                        f"completeness={payload.get('sourceRecordingComplete')} reasons="
                        f"{payload.get('sourceRecordingIncompleteReasons', [])}.",
                        [list_node], payload.get("sourceRecordingComplete") is True,
                        "incomplete-capture",
                    )
        elif event_type == "finish-command-list":
            recording_id = payload.get("commandRecordingObservationId")
            list_id = payload.get("commandListObservationId")
            finish_node = graph.node(
                f"event-{sequence}", "command-list-finish",
                f"FinishCommandList at event {sequence}", sequence,
                {**payload, "commandStreamSequence": event.get("execution", {}).get("commandStreamSequence")},
                event_source_refs(event),
            )
            coherent = True
            recording = valid_recording(recording_id)
            if event.get("commandRecordingObservationId") != recording_id:
                graph.gap(
                    f"FinishCommandList event {sequence} envelope recording "
                    f"{event.get('commandRecordingObservationId')} disagrees with payload recording {recording_id}; "
                    "authoritative provenance was suppressed.",
                    [finish_node], True, "other",
                )
                coherent = False
            if recording and not recording_context_matches(
                recording, event.get("deviceContextObservationId"),
                [recording["node"], finish_node], f"FinishCommandList event {sequence}",
            ):
                coherent = False
            if not recording:
                graph.gap(
                    f"FinishCommandList event {sequence} has no valid declared source recording {recording_id}.",
                    [finish_node], payload.get("sourceRecordingComplete") is True,
                    "incomplete-capture",
                )
                coherent = False
            command_list = valid_command_list(list_id)
            if payload.get("succeeded") is True:
                if not command_list:
                    graph.gap(
                        f"Successful FinishCommandList event {sequence} has no valid observed command list {list_id}.",
                        [finish_node], True, "incomplete-capture",
                    )
                    coherent = False
                elif (
                    command_list.get("sourceRecordingId") != recording_id or
                    (recording and command_list.get("sourceContextId") != recording.get("ownerContextId")) or
                    command_list["payload"].get("commandListPointer") != payload.get("commandListPointer") or
                    command_list["payload"].get("sourceRecordingComplete") != payload.get("sourceRecordingComplete") or
                    command_list["payload"].get("sourceRecordingIncompleteReasons") !=
                        payload.get("sourceRecordingIncompleteReasons")
                ):
                    graph.gap(
                        f"Successful FinishCommandList event {sequence} does not match command list {list_id}'s "
                        "declared source recording, owner context, or pointer; authoritative provenance was suppressed.",
                        [command_list["node"], finish_node], True, "other",
                    )
                    coherent = False
            elif list_id:
                graph.gap(
                    f"Failed FinishCommandList event {sequence} unexpectedly names command list {list_id}.",
                    [finish_node], True,
                )
                coherent = False
            if coherent and recording:
                graph.edge(
                    "finishes", recording["node"], finish_node,
                    [recording["event"]["sequence"], sequence],
                    "This FinishCommandList call terminated the exact deferred recording epoch.",
                    {"restoreDeferredContextState": payload.get("restoreDeferredContextState")},
                )
            if payload.get("sourceRecordingComplete") is not True:
                graph.gap(
                    f"FinishCommandList event {sequence} ended an incomplete source recording: "
                    f"{payload.get('sourceRecordingIncompleteReasons', [])}.",
                    [finish_node], False, "incomplete-capture",
                )
        elif event_type == "execute-command-list":
            list_id = payload.get("commandListObservationId")
            execution_node = graph.node(
                f"event-{sequence}", "command-execution",
                f"ExecuteCommandList at event {sequence}", sequence,
                {**payload, "commandStreamSequence": event.get("execution", {}).get("commandStreamSequence")},
                event_source_refs(event),
            )
            command_list = valid_command_list(list_id)
            coherent = bool(command_list)
            execution_context_id = event.get("deviceContextObservationId")
            execution_context = valid_context(execution_context_id)
            execution = event.get("execution", {})
            command_stream_sequence = execution.get("commandStreamSequence")
            if not execution_context:
                graph.gap(
                    f"ExecuteCommandList event {sequence} has no valid declared execution context "
                    f"{execution_context_id}; authoritative provenance was suppressed.",
                    [execution_node], True, "other",
                )
                coherent = False
            elif execution_context["payload"].get("kind") != "immediate":
                graph.gap(
                    f"ExecuteCommandList event {sequence} names non-immediate context "
                    f"{execution_context_id}; authoritative provenance was suppressed.",
                    [execution_context["node"], execution_node], True, "other",
                )
                coherent = False
            if (
                execution.get("observationDomain") != "cpu-call" or
                not isinstance(command_stream_sequence, int) or
                isinstance(command_stream_sequence, bool)
            ):
                graph.gap(
                    f"ExecuteCommandList event {sequence} is not a sequenced CPU-call observation; "
                    "authoritative provenance was suppressed.",
                    [execution_node], True, "other",
                )
                coherent = False
            if event.get("commandRecordingObservationId") is not None:
                graph.gap(
                    f"ExecuteCommandList event {sequence} unexpectedly carries recording envelope "
                    f"{event.get('commandRecordingObservationId')}; authoritative provenance was suppressed.",
                    [execution_node], True, "other",
                )
                coherent = False
            if not command_list:
                graph.gap(
                    f"ExecuteCommandList event {sequence} refers to undeclared or invalid command list {list_id}.",
                    [execution_node], True,
                )
            elif (
                payload.get("sourceCommandRecordingObservationId") != command_list.get("sourceRecordingId") or
                payload.get("commandListPointer") != command_list["payload"].get("commandListPointer")
            ):
                graph.gap(
                    f"ExecuteCommandList event {sequence} does not match command list {list_id}'s declared "
                    "source recording or pointer; authoritative provenance was suppressed.",
                    [command_list["node"], execution_node], True, "other",
                )
                coherent = False
            if coherent and command_list:
                graph.edge(
                    "executes", command_list["node"], execution_node,
                    [command_list["event"]["sequence"], sequence],
                    "The immediate-context execution names this exact command-list observation.",
                )
            if payload.get("restoreContextState") is False:
                srv_state.clear()
                uav_state.clear()
                active_target_binding = None
                predicted_srv_state.clear()
                predicted_uav_state.clear()
                predicted_target_binding = None
                graph.gap(
                    f"ExecuteCommandList event {sequence} used restoreContextState=false; immediate-context "
                    "SRV, UAV, and target bindings were reset to D3D11 defaults, and command-list-local state "
                    "is not projected into later immediate events.",
                    [execution_node], False, "incomplete-capture",
                )
        elif event_type == "shader-observed":
            shader_id = payload.get("shaderObservationId")
            observed_identity_node(
                event, shader_id, "engine-shader",
                payload.get("fxpFilename") or payload.get("imageSpaceName") or shader_id or "engine shader",
            )
        elif event_type == "stage-shader-observed":
            stage_shader_id = payload.get("stageShaderObservationId")
            stage_node = observed_identity_node(
                event, stage_shader_id, "pipeline-state",
                f"{payload.get('stage', 'unknown')} shader {stage_shader_id}",
                {**payload, "pipelineRole": "stage-shader"},
            )
            if stage_shader_id:
                stage_shader_payloads[stage_shader_id] = payload
            if stage_node:
                aliases = [
                    alias for alias in payload.get("engineAliases", [])
                    if isinstance(alias.get("loaderType"), str) and isinstance(alias.get("descriptor"), int)
                ]
                for alias in aliases:
                    family = alias["loaderType"]
                    compile_source = alias.get("compileSourceName")
                    compile_identity = (
                        Path(compile_source).stem.casefold()
                        if isinstance(compile_source, str) and compile_source else family.casefold()
                    )
                    compile_candidates = compile_units_by_family.get(compile_identity, [])
                    candidate_edges: list[str] = []
                    for unit in compile_candidates:
                        compile_id = unit.get("id")
                        if not isinstance(compile_id, str):
                            continue
                        compile_node = graph.node(
                            compile_id,
                            "shader-compile-unit",
                            f"{compile_id} {unit.get('sourceVirtualPath', family)}",
                            None,
                            {
                                "compileUnitId": compile_id,
                                "kind": unit.get("kind"),
                                "owner": unit.get("owner"),
                                "sourceVirtualPath": unit.get("sourceVirtualPath"),
                                "sourcePath": unit.get("sourcePath"),
                                "entryPoint": unit.get("entryPoint"),
                                "defineMode": unit.get("defineMode"),
                                "dependencyClosure": unit.get("dependencyClosure", []),
                            },
                            [{"kind": "shader-manifest", "value": compile_id}],
                        )
                        candidate_edges.append(graph.edge(
                            "implemented-by", stage_node, compile_node, [sequence],
                            "The runtime compile-source identity names this engine shader-cache compile family in the supplied static shader manifest; older captures fall back to loader-family identity.",
                            {
                                "loaderType": family,
                                "compileSourceName": compile_source,
                                "joinBasis": "compile-source" if compile_source else "loader-family-fallback",
                                "descriptor": alias["descriptor"],
                                "stage": payload.get("stage"),
                            },
                            "correlated", "confirmed" if len(compile_candidates) == 1 else "medium",
                        ))
                        if compile_context_node:
                            requirement = compatibility_requirement(
                                compatibility_registrations,
                                family,
                                str(unit.get("sourceVirtualPath") or unit.get("sourcePath") or ""),
                            )
                            graph.edge(
                                "compiled-under", stage_node, compile_context_node, [sequence],
                                "The capture-start global compile state and retained compatibility registrations "
                                "deterministically describe the dynamic inputs used for this observed shader family/source.",
                                {
                                    "compileUnitId": compile_id,
                                    "loaderType": family,
                                    "compileSourceName": compile_source,
                                    "descriptor": alias["descriptor"],
                                    "globalCompileStateDigest": shader_compilation.get("globalCompileState", {}).get("digest"),
                                    "compatibilityRequirementSha256": requirement["canonicalSha256"],
                                    "compatibilityRegistrationIdentities": requirement["registrationIdentities"],
                                    "compatibilityRegistrationHandles": requirement["registrationHandles"],
                                },
                                "correlated", "confirmed" if len(compile_candidates) == 1 else "medium",
                            )
                    graph.ambiguity(
                        f"Which static compile unit implements runtime shader source {compile_source or family}?",
                        candidate_edges,
                        "Capture the effective compile-source name or refine the shader manifest until the runtime source has one compile-unit match.",
                    )
        elif event_type == "object-observed":
            object_id = payload.get("sceneObjectObservationId")
            object_node = observed_identity_node(
                event, object_id, "scene-object",
                payload.get("referenceName") or payload.get("baseFormName") or object_id or "scene object",
            )
            if object_id and object_node:
                scene_objects[object_id] = {
                    "event": event, "payload": payload, "node": object_node,
                }
        elif event_type == "geometry-observed":
            geometry_id = payload.get("geometryObservationId")
            geometry_node = observed_identity_node(
                event, geometry_id, "geometry",
                payload.get("name") or payload.get("runtimeTypeName") or geometry_id or "geometry",
                {**payload, "identityRole": "observed-geometry"},
            )
            if geometry_id and geometry_node:
                geometry_observations[geometry_id] = {
                    "event": event, "payload": payload, "node": geometry_node,
                }
                object_id = payload.get("sceneObjectObservationId")
                if object_id:
                    object = scene_objects.get(object_id)
                    if object:
                        graph.edge(
                            "represented-by", object["node"], geometry_node,
                            [object["event"]["sequence"], sequence],
                            "The geometry declaration names this exact earlier scene-object observation as its represented object.",
                            {"sceneObjectObservationId": object_id, "geometryObservationId": geometry_id},
                        )
                    else:
                        graph.gap(
                            f"Geometry observation {geometry_id} refers to undeclared scene object {object_id}.",
                            [geometry_node], True,
                        )
        elif event_type == "material-observed":
            material_id = payload.get("materialStateObservationId")
            material_node = observed_identity_node(
                event, material_id, "material",
                payload.get("shaderPropertyRuntimeTypeName") or material_id or "material state",
                {**payload, "identityRole": "observed-material-state"},
            )
            if material_id and material_node:
                material_observations[material_id] = {
                    "event": event, "payload": payload, "node": material_node,
                    "bindings": [],
                }
                for binding_ordinal, binding in enumerate(payload.get("textureBindings", [])):
                    binding_index = binding.get("bindingIndex", binding_ordinal)
                    binding_role = binding.get("role", "unknown")
                    binding_path = binding.get("path")
                    binding_node = graph.node(
                        f"{material_id}-texture-{binding_ordinal}",
                        "material-texture-binding",
                        binding_path or f"{binding_role}[{binding_index}]",
                        sequence,
                        {
                            **binding,
                            "materialStateObservationId": material_id,
                            "bindingOrdinal": binding_ordinal,
                            "bindingIndexSemantics": "runtime-material-list-position-not-shader-register",
                        },
                        event_source_refs(event, material_id),
                    )
                    graph.edge(
                        "binds-texture", material_node, binding_node, [sequence],
                        "The observed material-state revision declared this bounded runtime texture binding.",
                        {
                            "role": binding_role,
                            "bindingIndex": binding_index,
                            "bindingIndexSemantics": "runtime-material-list-position-not-shader-register",
                        },
                    )
                    material_observations[material_id]["bindings"].append({
                        "node": binding_node,
                        "resourceObservationId": binding.get("resourceObservationId"),
                        "role": binding_role,
                        "bindingIndex": binding_index,
                        "bindingOrdinal": binding_ordinal,
                    })
                    resource_id = binding.get("resourceObservationId")
                    if resource_id:
                        resource = resources.get(resource_id)
                        allocation_node = resource_node(resource_id)
                        if resource and allocation_node:
                            graph.edge(
                                "resolves-to-resource", binding_node, allocation_node,
                                [resource["sequence"], sequence],
                                "The material binding names this exact earlier capture-local D3D resource observation.",
                                {"resourceObservationId": resource_id},
                            )
                        else:
                            graph.gap(
                                f"Material texture binding {binding_ordinal} in {material_id} refers to undeclared resource {resource_id}.",
                                [binding_node], True,
                            )
        elif event_type == "render-pass-enter":
            render_pass_id = event.get("scopes", {}).get("renderPass")
            observed_identity_node(
                event, render_pass_id, "render-pass",
                f"render pass {payload.get('passEnum', payload.get('technique', 'unknown'))}",
            )
        elif event_type == "technique-begin":
            technique_id = event.get("scopes", {}).get("technique")
            technique_node = observed_identity_node(
                event, technique_id, "technique",
                f"technique {payload.get('vertexDescriptor', 'unknown')}/{payload.get('pixelDescriptor', 'unknown')}",
            )
            render_pass_id = event.get("scopes", {}).get("renderPass")
            if technique_node and render_pass_id:
                render_pass_node = observed_identity_node(
                    event, render_pass_id, "render-pass", f"render pass {render_pass_id}",
                    {"identityDeclaredByScope": True},
                )
                graph.edge(
                    "selects", render_pass_node, technique_node, [sequence],
                    "The technique began inside this exact capture-local render-pass scope.",
                )
            shader_id = payload.get("shaderObservationId")
            if technique_node and shader_id:
                shader_node = observed_identity_node(
                    event, shader_id, "engine-shader", f"engine shader {shader_id}",
                    {"identityDeclaredByReference": True},
                )
                graph.edge(
                    "selects", shader_node, technique_node, [sequence],
                    "The engine shader observation selected this descriptor pair at the technique boundary.",
                )
        elif event_type == "technique-resolved":
            technique_id = event.get("scopes", {}).get("technique")
            technique_node = observed_identity_node(
                event, technique_id, "technique", f"technique {technique_id}",
                {"identityDeclaredByScope": True},
            )
            for stage in ("vertex", "pixel"):
                stage_shader_id = payload.get(f"{stage}ShaderObservationId")
                if technique_node and stage_shader_id:
                    stage_node = observed_identity_node(
                        event, stage_shader_id, "pipeline-state",
                        f"{stage} shader {stage_shader_id}",
                        {"pipelineRole": "stage-shader", "stage": stage, "identityDeclaredByReference": True},
                    )
                    graph.edge(
                        "selects", technique_node, stage_node, [sequence],
                        "Technique resolution selected this exact capture-local stage-shader observation.",
                        {"stage": stage, "route": payload.get(f"{stage}Route")},
                    )
        elif event_type == "geometry-setup-begin":
            geometry_scope_id = event.get("scopes", {}).get("geometry")
            geometry_scope_node = observed_identity_node(
                event, geometry_scope_id, "geometry-setup",
                f"geometry setup {payload.get('geometryPointer', geometry_scope_id)}",
                {**payload, "identityRole": "geometry-setup-scope"},
            )
            render_pass_id = event.get("scopes", {}).get("renderPass")
            if geometry_scope_node and render_pass_id:
                render_pass_node = observed_identity_node(
                    event, render_pass_id, "render-pass", f"render pass {render_pass_id}",
                    {"identityDeclaredByScope": True},
                )
                graph.edge(
                    "uses", render_pass_node, geometry_scope_node, [sequence],
                    "Geometry setup occurred inside this exact capture-local render-pass scope.",
                )
            observed_geometry_id = payload.get("geometryObservationId")
            material_id = payload.get("materialStateObservationId")
            observed_geometry = geometry_observations.get(observed_geometry_id or "")
            material = material_observations.get(material_id or "")
            if geometry_scope_id and geometry_scope_node:
                geometry_setup_bindings[geometry_scope_id] = {
                    "event": event,
                    "node": geometry_scope_node,
                    "geometry": observed_geometry,
                    "material": material,
                }
            if observed_geometry_id:
                if observed_geometry and geometry_scope_node:
                    graph.edge(
                        "same-observed-object", geometry_scope_node, observed_geometry["node"],
                        [observed_geometry["event"]["sequence"], sequence],
                        "The v2 setup boundary binds this exact earlier geometry observation to the temporal setup scope.",
                        {"geometryObservationId": observed_geometry_id},
                    )
                elif geometry_scope_node:
                    graph.gap(
                        f"Geometry setup {geometry_scope_id} refers to undeclared geometry observation {observed_geometry_id}.",
                        [geometry_scope_node], True,
                    )
            if material_id:
                if material and geometry_scope_node:
                    graph.edge(
                        "uses-material-state", geometry_scope_node, material["node"],
                        [material["event"]["sequence"], sequence],
                        "The v2 setup boundary binds this exact earlier material-state revision to the temporal setup scope.",
                        {"materialStateObservationId": material_id},
                    )
                elif geometry_scope_node:
                    graph.gap(
                        f"Geometry setup {geometry_scope_id} refers to undeclared material-state observation {material_id}.",
                        [geometry_scope_node], True,
                    )
        elif event_type == "resource-observed":
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
                source = payload.get("source")
                if effective_state_contract and source == "observed-call":
                    predicted_target_binding = binding_id
                    hazard_adjustment_count += clear_conflicting_srvs(
                        predicted_srv_state,
                        output_views(predicted_target_binding, predicted_uav_state),
                    )
                else:
                    active_target_binding = binding_id
                    if not effective_state_contract:
                        hazard_adjustment_count += clear_conflicting_srvs(
                            srv_state, active_output_views()
                        )
        elif event_type == "resource-view-bind":
            key = (str(payload.get("stage")), int(payload.get("slot", 0)))
            view_id = payload.get("viewObservationId")
            source = payload.get("source")
            requested = effective_state_contract and source == "requested-call"
            shader_state = predicted_srv_state if requested else srv_state
            unordered_state = predicted_uav_state if requested else uav_state
            target_binding_id = predicted_target_binding if requested else active_target_binding
            if payload.get("bindingKind") == "shader-resource":
                if requested and view_id and conflicts_any(
                    view_id, output_views(target_binding_id, unordered_state)
                ):
                    shader_state[key] = None
                    hazard_adjustment_count += 1
                elif not effective_state_contract and view_id and conflicts_any(view_id, active_output_views()):
                    shader_state[key] = None
                    hazard_adjustment_count += 1
                else:
                    shader_state[key] = view_id
            else:
                unordered_state[key] = view_id
                if requested and view_id:
                    hazard_adjustment_count += clear_conflicting_srvs(shader_state, [view_id])
                elif not effective_state_contract and view_id:
                    hazard_adjustment_count += clear_conflicting_srvs(srv_state, [view_id])
        elif event_type == "resource-view-state-observed":
            effective_state_query_count += 1
            stage = str(payload.get("stage"))
            start_slot = int(payload.get("startSlot", 0))
            count = int(payload.get("count", 0))
            actual_state = srv_state if payload.get("bindingKind") == "shader-resource" else uav_state
            predicted_state = (
                predicted_srv_state if payload.get("bindingKind") == "shader-resource"
                else predicted_uav_state
            )
            for slot in range(start_slot, start_slot + count):
                key = (stage, slot)
                if key not in predicted_state:
                    continue
                if actual_state.get(key) == predicted_state.get(key):
                    effective_state_verified_slots += 1
                else:
                    effective_state_mismatch_slots += 1
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
        elif event_type in {"draw", "dispatch", "resource-flow", "resource-cpu-access"}:
            operation = payload.get("operation", event_type)
            if event_type == "resource-cpu-access":
                operation = str(payload.get("phase") or "cpu-access")
            execution_kind = "draw" if event_type == "draw" else ("dispatch" if event_type == "dispatch" else
                ("copy" if operation in {"copy-resource", "copy-subresource-region", "resolve-subresource", "copy-structure-count"}
                 else "resource-operation"))
            execution_attributes = {
                "eventSequence": sequence,
                "commandStreamSequence": event.get("execution", {}).get("commandStreamSequence"),
                "observationDomain": event.get("execution", {}).get("observationDomain"),
                "deviceContextObservationId": event.get("deviceContextObservationId"),
                "commandRecordingObservationId": event.get("commandRecordingObservationId"),
                "cpuFrame": event.get("frame", {}).get("cpuFrame"),
                "eye": event.get("frame", {}).get("eye", "unknown"),
                "operation": operation,
            }
            if event_type == "resource-cpu-access":
                duration_ticks = int(payload.get("durationQpcTicks") or 0)
                execution_attributes.update({
                    "mapObservationId": payload.get("mapObservationId"),
                    "resourceObservationId": payload.get("resourceObservationId"),
                    "subresource": payload.get("subresource"),
                    "mapType": payload.get("mapType"),
                    "mapFlags": payload.get("mapFlags"),
                    "succeeded": payload.get("succeeded"),
                    "matchedMap": payload.get("matchedMap"),
                    "durationQpcTicks": duration_ticks,
                    "durationMicroseconds": (
                        duration_ticks * 1_000_000.0 / qpc_frequency if qpc_frequency > 0 else None
                    ),
                    "visibilityBoundary": payload.get("visibilityBoundary"),
                    "publicationBoundary": payload.get("publicationBoundary"),
                })
            if event_type == "resource-flow":
                execution_attributes["sourceSubresource"] = payload.get("sourceSubresource")
                execution_attributes["destinationSubresource"] = payload.get("destinationSubresource")
            execution = graph.node(
                f"event-{sequence}", execution_kind, f"{operation} at event {sequence}", sequence,
                execution_attributes, event_source_refs(event),
            )
            recording_id = event.get("commandRecordingObservationId")
            observation_domain = event.get("execution", {}).get("observationDomain")
            if recording_id:
                recording = valid_recording(recording_id)
                context_id = event.get("deviceContextObservationId")
                if recording and recording_context_matches(
                    recording, context_id, [recording["node"], execution],
                    f"Recorded execution event {sequence}",
                ):
                    graph.edge(
                        "records", recording["node"], execution,
                        [recording["event"]["sequence"], sequence],
                        "This draw or dispatch was observed in the exact deferred recording epoch; it is not immediate execution.",
                    )
                elif not recording:
                    graph.gap(
                        f"Recorded execution event {sequence} refers to undeclared recording or invalid recording {recording_id}.",
                        [execution], True,
                    )
            if event_type in {"draw", "dispatch"}:
                context_id = event.get("deviceContextObservationId")
                context = device_contexts.get(context_id or "")
                is_deferred = bool(context and context["payload"].get("kind") == "deferred")
                is_recording_domain = observation_domain == "command-recording"
                if is_recording_domain or recording_id or is_deferred:
                    if is_recording_domain and not recording_id:
                        graph.gap(
                            f"Command-recording {event_type} event {sequence} has no recording identity; "
                            "immediate-context state was forbidden.",
                            [execution], True,
                        )
                    if is_recording_domain and not context_id:
                        graph.gap(
                            f"Command-recording {event_type} event {sequence} has no device-context identity; "
                            "immediate-context state was forbidden.",
                            [execution], True,
                        )
                    if is_recording_domain and context_id and context is None:
                        graph.gap(
                            f"Command-recording {event_type} event {sequence} refers to undeclared "
                            f"device context {context_id}; immediate-context state was forbidden.",
                            [execution], True,
                        )
                    if is_recording_domain and context and context["payload"].get("kind") != "deferred":
                        graph.gap(
                            f"Command-recording {event_type} event {sequence} conflicts with "
                            f"{context['payload'].get('kind', 'unknown')} device context {context_id}; "
                            "immediate-context state was forbidden.",
                            [execution], True,
                        )
                    for stage, stage_shader_id in (
                        ("vertex", payload.get("vertexShaderObservationId")),
                        ("pixel", payload.get("pixelShaderObservationId")),
                        ("compute", payload.get("computeShaderObservationId")),
                    ):
                        if not stage_shader_id:
                            continue
                        stage_node = observed_identity_node(
                            event, stage_shader_id, "pipeline-state",
                            f"{stage} shader {stage_shader_id}",
                            {"pipelineRole": "stage-shader", "stage": stage,
                             "identityDeclaredByReference": True},
                        )
                        graph.edge(
                            "binds", stage_node, execution, [sequence],
                            "The recorded command payload names this exact stage-shader observation.",
                            {"stage": stage},
                        )
                    graph.gap(
                        f"Deferred {event_type} event {sequence} has no command-list-local SRV, UAV, "
                        "target-binding, resource-version, or hazard-state provenance; immediate-context "
                        "state was deliberately not applied.",
                        [execution], False, "incomplete-capture",
                    )
                    continue
            if event_type == "resource-cpu-access":
                graph.cpu_access_count += 1
                map_id = payload.get("mapObservationId")
                if operation == "map":
                    if map_id:
                        cpu_maps[map_id] = {"node": execution, "event": event, "payload": payload}
                    if payload.get("succeeded") is True:
                        duration_ticks = int(payload.get("durationQpcTicks") or 0)
                        graph.cpu_map_stall_ticks += duration_ticks
                        graph.cpu_map_max_stall_ticks = max(graph.cpu_map_max_stall_ticks, duration_ticks)
                elif operation == "unmap":
                    mapped = cpu_maps.get(map_id or "")
                    if mapped:
                        graph.edge(
                            "precedes", mapped["node"], execution,
                            [mapped["event"]["sequence"], sequence],
                            "The capture-local map identity pairs this successful Map return with its Unmap publication boundary.",
                            {"mapObservationId": map_id, "hazard": "CPU-map-lifetime"},
                        )
                    else:
                        graph.gap(
                            f"Unmap event {sequence} has no observed successful Map identity.",
                            [execution], False, "incomplete-capture",
                        )

            if event_type in {"draw", "dispatch"}:
                active_material_observation: dict[str, Any] | None = None
                scopes = event.get("scopes", {})
                scope_kinds = (
                    ("renderPass", "render-pass", "draws"),
                    ("technique", "technique", "draws"),
                    ("geometry", "geometry-setup", "draws"),
                )
                for scope_name, node_kind, edge_type in scope_kinds:
                    scope_id = scopes.get(scope_name)
                    if scope_id:
                        scope_node = observed_identity_node(
                            event, scope_id, node_kind, f"{scope_name} {scope_id}",
                            {"identityDeclaredByScope": True},
                        )
                        graph.edge(
                            edge_type, scope_node, execution, [sequence],
                            f"The execution event carried this exact active {scope_name} scope.",
                        )
                geometry_scope_id = scopes.get("geometry")
                prepared_geometry_id = (
                    payload.get("preparedGeometrySetupObservationId")
                    if event_type == "draw" else None
                )
                if geometry_scope_id and prepared_geometry_id and geometry_scope_id != prepared_geometry_id:
                    graph.gap(
                        f"Execution event {sequence} carries conflicting active and prepared geometry setup identities.",
                        [execution], True,
                    )
                geometry_binding_id = geometry_scope_id or prepared_geometry_id
                association_basis = (
                    "active-geometry-scope" if geometry_scope_id
                    else "same-thread-next-immediate-context-draw" if prepared_geometry_id
                    else None
                )
                if prepared_geometry_id and not geometry_scope_id:
                    prepared_node = observed_identity_node(
                        event, prepared_geometry_id, "geometry-setup",
                        f"prepared geometry {prepared_geometry_id}",
                        {"identityDeclaredByReference": True},
                    )
                    graph.edge(
                        "prepares", prepared_node, execution, [sequence],
                        "The draw explicitly consumed the one-shot prepared geometry identity published by the same-thread selected SetupGeometry call.",
                        {"associationBasis": association_basis,
                         "geometrySetupObservationId": prepared_geometry_id},
                    )
                    graph.prepared_geometry_draw_count += 1
                binding = geometry_setup_bindings.get(geometry_binding_id or "")
                if binding:
                    active_material_observation = binding.get("material")
                    for semantic, role in (
                        (binding.get("geometry"), "geometry"),
                        (binding.get("material"), "material-state"),
                    ):
                        if semantic:
                            graph.edge(
                                "same-observed-object", semantic["node"], execution,
                                [semantic["event"]["sequence"], binding["event"]["sequence"], sequence],
                                f"This exact {role} observation reaches the execution through its v2 geometry-setup identity.",
                                {"projection": role, "geometrySetupObservationId": geometry_binding_id,
                                 "associationBasis": association_basis},
                            )
                    observed_geometry = binding.get("geometry")
                    object_id = observed_geometry and observed_geometry["payload"].get("sceneObjectObservationId")
                    scene_object = scene_objects.get(object_id or "")
                    if scene_object:
                        graph.edge(
                            "same-observed-object", scene_object["node"], execution,
                            [scene_object["event"]["sequence"], observed_geometry["event"]["sequence"],
                             binding["event"]["sequence"], sequence],
                            "This exact scene-object observation reaches the execution through its v2 geometry-setup identity and geometry declaration.",
                            {"projection": "scene-object", "geometrySetupObservationId": geometry_binding_id,
                             "associationBasis": association_basis},
                        )
                elif prepared_geometry_id:
                    graph.gap(
                        f"Draw event {sequence} refers to undeclared prepared geometry setup {prepared_geometry_id}.",
                        [execution], True,
                    )
                stage_fields = (
                    ("vertex", payload.get("vertexShaderObservationId")),
                    ("pixel", payload.get("pixelShaderObservationId")),
                    ("compute", payload.get("computeShaderObservationId")),
                )
                for stage, stage_shader_id in stage_fields:
                    if stage_shader_id:
                        stage_node = observed_identity_node(
                            event, stage_shader_id, "pipeline-state",
                            f"{stage} shader {stage_shader_id}",
                            {"pipelineRole": "stage-shader", "stage": stage, "identityDeclaredByReference": True},
                        )
                        graph.edge(
                            "binds", stage_node, execution, [sequence],
                            "The execution payload names this exact bound stage-shader observation.",
                            {"stage": stage},
                        )

                vertex_id = payload.get("vertexShaderObservationId")
                pixel_id = payload.get("pixelShaderObservationId")
                vertex_payload = stage_shader_payloads.get(vertex_id or "", {})
                pixel_payload = stage_shader_payloads.get(pixel_id or "", {})
                vertex_aliases = {
                    (alias.get("loaderType", "").casefold(), alias.get("descriptor"))
                    for alias in vertex_payload.get("engineAliases", [])
                    if isinstance(alias.get("loaderType"), str) and isinstance(alias.get("descriptor"), int)
                }
                pixel_aliases = {
                    (alias.get("loaderType", "").casefold(), alias.get("descriptor"))
                    for alias in pixel_payload.get("engineAliases", [])
                    if isinstance(alias.get("loaderType"), str) and isinstance(alias.get("descriptor"), int)
                }
                technique_candidates: list[dict[str, Any]] = []
                for vertex_family, vertex_descriptor in sorted(vertex_aliases):
                    for pixel_family, pixel_descriptor in sorted(pixel_aliases):
                        if vertex_family == pixel_family:
                            technique_candidates.extend(techniques_by_pair.get(
                                (vertex_family, vertex_descriptor, pixel_descriptor), []
                            ))
                technique_edges: list[str] = []
                for entity in technique_candidates:
                    entity_id = entity.get("id")
                    if not isinstance(entity_id, str):
                        continue
                    technique_node = graph.node(
                        entity_id,
                        "technique",
                        entity.get("name", entity_id),
                        None,
                        {
                            **entity.get("attributes", {}),
                            "engineEntityId": entity_id,
                        },
                        [{"kind": "engine-map", "value": entity_id}],
                    )
                    technique_edges.append(graph.edge(
                        "draws", technique_node, execution, [sequence],
                        "The draw bound a vertex/pixel loader-family descriptor pair that uniquely matches this supplied engine-map technique.",
                        {
                            "vertexShaderObservationId": vertex_id,
                            "pixelShaderObservationId": pixel_id,
                        },
                        "correlated", "confirmed" if len(technique_candidates) == 1 else "medium",
                    ))
                    for stage, stage_id in (("vertex", vertex_id), ("pixel", pixel_id)):
                        if stage_id:
                            stage_node = observed_identity_node(
                                event, stage_id, "pipeline-state", f"{stage} shader {stage_id}",
                                {"pipelineRole": "stage-shader", "stage": stage, "identityDeclaredByReference": True},
                            )
                            graph.edge(
                                "selects", technique_node, stage_node, [sequence],
                                "The engine-map descriptor pair matches this exact bound runtime stage alias.",
                                {"stage": stage}, "correlated",
                                "confirmed" if len(technique_candidates) == 1 else "medium",
                            )
                graph.ambiguity(
                    "Which engine-map technique owns the bound vertex/pixel descriptor pair?",
                    technique_edges,
                    "Refine the engine map until the loader family and descriptor pair have one technique match.",
                )

            read_views: list[tuple[str, str, int]] = []
            write_views: list[tuple[str, str, int]] = []
            direct_reads: list[tuple[str, str]] = []
            direct_writes: list[tuple[str, str]] = []
            material_input_matches: dict[str, list[dict[str, Any]]] = defaultdict(list)
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
            elif event_type == "resource-cpu-access":
                resource_id = payload.get("resourceObservationId")
                if operation == "map" and payload.get("succeeded") is True and payload.get("readable") is True:
                    direct_reads.append((resource_id, "cpu-readable-after-map-return"))
                    graph.cpu_read_visibility_count += 1
                elif operation == "unmap" and payload.get("matchedMap") is True and payload.get("writable") is True:
                    direct_writes.append((resource_id, "gpu-visible-after-unmap-return"))
                    graph.cpu_write_publication_count += 1
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
                    if event_type == "draw" and active_material_observation:
                        for material_binding in active_material_observation.get("bindings", []):
                            if material_binding.get("resourceObservationId") != resource_id:
                                continue
                            material_input_matches[resource_id].append({
                                "materialTextureBindingNodeId": material_binding["node"],
                                "materialStateObservationId": active_material_observation["payload"].get(
                                    "materialStateObservationId"
                                ),
                                "materialObservationSequence": active_material_observation["event"]["sequence"],
                                "role": material_binding["role"],
                                "bindingIndex": material_binding["bindingIndex"],
                                "bindingOrdinal": material_binding["bindingOrdinal"],
                                "resourceObservationId": resource_id,
                                "viewObservationId": view_id,
                                "viewObservationSequence": view["sequence"],
                                "stage": stage,
                                "slot": slot,
                            })
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
                input_matches = material_input_matches.get(resource_id, [])
                read_attributes: dict[str, Any] = {
                    "roles": sorted(set(roles)),
                    "versionScope": "whole-resource-conservative",
                }
                evidence_sequences = [resource["sequence"], sequence]
                read_note = "Ordered immediate-context state identifies the current content version as an execution input."
                if input_matches:
                    read_attributes["materialInputMatches"] = input_matches
                    evidence_sequences.extend(
                        match["materialObservationSequence"] for match in input_matches
                    )
                    evidence_sequences.extend(
                        match["viewObservationSequence"] for match in input_matches
                    )
                    read_note = (
                        "Ordered immediate-context state identifies the current content version as an execution input; "
                        "the active geometry material independently names the same capture-local allocation."
                    )
                    graph.material_input_match_count += len(input_matches)
                graph.edge(
                    "reads", version, execution, sorted(set(evidence_sequences)),
                    read_note,
                    read_attributes,
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
    if not effective_state_contract:
        graph.gap(
            "Automatic D3D11 hazard resolution is derived from ordered setter calls and exact observed view-subresource overlap where descriptors are complete; this capture predates post-call effective-state queries.",
            blocking=False, kind="unsupported-route",
        )
    graph.hazard_adjustment_count = hazard_adjustment_count
    graph.hazard_overlap_fallback_count = hazard_overlap_fallback_count
    graph.effective_state_contract = effective_state_contract
    graph.effective_state_query_count = effective_state_query_count
    graph.effective_state_verified_slots = effective_state_verified_slots
    graph.effective_state_mismatch_slots = effective_state_mismatch_slots
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
    shader_manifest = json.loads(args.shader_manifest.read_text(encoding="utf-8-sig")) if args.shader_manifest else None
    engine_map = json.loads(args.engine_map.read_text(encoding="utf-8-sig")) if args.engine_map else None
    graph = derive(manifest, events, shader_manifest, engine_map)
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
            inputs.append({"kind": kind, "path": str(path), "sha256": sha256(path), "schemaMajor": input_schema_major(kind, data)})
    output = {
        "schema": {"name": "csx.derived-render-graph", "major": 1, "minor": 12, "producerVersion": "static-semantic-resource-graph-10"},
        "reportId": f"render-graph-{manifest['captureId'].removeprefix('capture-')}",
        "generatedAtUtc": manifest.get("createdAtUtc", "1970-01-01T00:00:00Z"),
        "generatedBy": {"name": "csx-render-map-join", "version": "0.12.0", "gitCommit": git_commit(repo)},
        "inputs": inputs,
        "nodes": graph.nodes,
        "edges": graph.edges,
        "ambiguities": graph.ambiguities,
        "gaps": graph.gaps,
        "decisionWindows": graph.decision_windows,
        "extensions": {
            "csx.executionGranularity": "individual-context-call-with-deferred-recording-boundaries",
            "csx.resourceVersionModel": "whole-resource-write-epoch-v1",
            "csx.hazardModel": "observed-post-call-effective-state-v1" if graph.effective_state_contract else "exact-view-subresource-v1",
            "csx.effectiveStateAdjustments": graph.hazard_adjustment_count,
            "csx.hazardOverlapFallbacks": graph.hazard_overlap_fallback_count,
            "csx.effectiveStateQueryCount": graph.effective_state_query_count,
            "csx.effectiveStateVerifiedSlots": graph.effective_state_verified_slots,
            "csx.effectiveStateMismatchSlots": graph.effective_state_mismatch_slots,
            "csx.cpuAccessCount": graph.cpu_access_count,
            "csx.cpuReadVisibilityCount": graph.cpu_read_visibility_count,
            "csx.cpuWritePublicationCount": graph.cpu_write_publication_count,
            "csx.cpuMapStallTicks": graph.cpu_map_stall_ticks,
            "csx.cpuMapMaxStallTicks": graph.cpu_map_max_stall_ticks,
            "csx.cpuMapStallMicroseconds": (
                graph.cpu_map_stall_ticks * 1_000_000.0 / int(manifest.get("clock", {}).get("frequencyHz") or 1)
            ),
            "csx.graphAcyclic": True,
            "csx.deferredContextCoverage": False,
            "csx.vrEyeAttribution": graph.eye_attribution_observed,
            "csx.semanticIdentityProjection": True,
            "csx.materialInputExecutionCorrelation": True,
            "csx.materialInputMatchCount": graph.material_input_match_count,
            "csx.preparedGeometryDrawCorrelation": True,
            "csx.preparedGeometryDrawCount": graph.prepared_geometry_draw_count,
            "csx.samplerStateCoverage": False,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "nodes": len(graph.nodes), "edges": len(graph.edges), "gaps": len(graph.gaps), "sha256": sha256(args.output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
