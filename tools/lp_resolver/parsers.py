from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable, Mapping

from .models import CandidateFile, LightPlacerEntry, ParseIssue, ParticleLightTarget
from .normalize import canonical_nif, normalized_settings, value_signature

_NIF_KEY_HINTS = ("nif", "mesh", "model", "path", "file")
_LP_FIELD_HINTS = ("radius", "intensity", "brightness", "color", "falloff", "fade", "flicker", "shadow")
_LP_STRUCT_HINTS = ("lights", "points", "data", "flags", "light")
_PL_FIELD_HINTS = ("particle", "billboard", "effectshader", "effect_shader", "vertexcolor", "vertex_color")


def _normalized_rel_path(relative_path: str) -> str:
    return relative_path.replace("\\", "/").lower()


def _path_suggests_light_placer(relative_path: str) -> bool:
    rel_path = _normalized_rel_path(relative_path)
    return "lightplacer/" in rel_path or "light placer" in rel_path or "light_placer" in rel_path


def _path_suggests_particle_lights(relative_path: str) -> bool:
    rel_path = _normalized_rel_path(relative_path)
    return (
        "particlelights/" in rel_path
        or "particle_lights/" in rel_path
        or ("particle" in rel_path and "light" in rel_path)
        or "communityshaders/lights/" in rel_path
    )


def _load_json(file_path: Path) -> Any:
    with file_path.open("r", encoding="utf-8", errors="ignore") as handle:
        return json.load(handle)


def _iter_dict_nodes(value: Any) -> Iterable[Mapping[str, Any]]:
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from _iter_dict_nodes(child)
        return
    if isinstance(value, list):
        for child in value:
            yield from _iter_dict_nodes(child)


def _extract_direct_nif_candidates(node: Mapping[str, Any]) -> set[str]:
    candidates: set[str] = set()
    for key, value in node.items():
        key_l = str(key).lower()
        if isinstance(value, str):
            value_l = value.lower()
            if ".nif" in value_l:
                candidates.add(value)
            elif any(hint in key_l for hint in _NIF_KEY_HINTS) and value.endswith(".nif"):
                candidates.add(value)
            continue

        if isinstance(value, list) and any(hint in key_l for hint in _NIF_KEY_HINTS):
            for item in value:
                if isinstance(item, str) and ".nif" in item.lower():
                    candidates.add(item)
            continue

        if isinstance(value, dict) and any(hint in key_l for hint in _NIF_KEY_HINTS):
            for nested_value in value.values():
                if isinstance(nested_value, str) and ".nif" in nested_value.lower():
                    candidates.add(nested_value)
    return candidates


def _looks_like_light_payload(node: Mapping[str, Any]) -> bool:
    for key in node.keys():
        key_l = str(key).lower()
        if any(hint in key_l for hint in _LP_FIELD_HINTS):
            return True
    return False


def _has_any_key_hint(node: Mapping[str, Any], hints: tuple[str, ...]) -> bool:
    for key in node.keys():
        key_l = str(key).lower()
        if any(hint in key_l for hint in hints):
            return True
    return False


def _node_looks_like_light_placer(node: Mapping[str, Any]) -> bool:
    if _has_any_key_hint(node, _LP_STRUCT_HINTS):
        return True
    return _looks_like_light_payload(node)


def _node_looks_like_particle_lights(node: Mapping[str, Any]) -> bool:
    return _has_any_key_hint(node, _PL_FIELD_HINTS)


def _root_has_light_placer_hints(root: Any) -> bool:
    for node in _iter_dict_nodes(root):
        if _node_looks_like_light_placer(node):
            return True
    return False


def _root_has_particle_hints(root: Any) -> bool:
    for node in _iter_dict_nodes(root):
        if _node_looks_like_particle_lights(node):
            return True
    return False


class LightPlacerAdapter:
    name = "light_placer"

    def can_parse(self, candidate: CandidateFile) -> bool:
        return candidate.file_path.suffix.lower() == ".json"

    def extract_entries(self, candidate: CandidateFile) -> tuple[list[LightPlacerEntry], list[ParseIssue]]:
        entries: list[LightPlacerEntry] = []
        issues: list[ParseIssue] = []
        if not self.can_parse(candidate):
            return entries, issues

        likely_lp_path = _path_suggests_light_placer(candidate.relative_path)
        try:
            root = _load_json(candidate.file_path)
        except Exception as exc:  # noqa: BLE001
            if likely_lp_path:
                issues.append(
                    ParseIssue(
                        severity="warn",
                        message=f"JSON parse failed: {exc}",
                        source_file=candidate.relative_path,
                        source_mod=candidate.mod_name,
                    )
                )
            return entries, issues

        root_has_lp_hints = _root_has_light_placer_hints(root)
        if not likely_lp_path and not root_has_lp_hints:
            return entries, issues

        seen: set[tuple[str, str]] = set()
        for node in _iter_dict_nodes(root):
            nif_candidates = _extract_direct_nif_candidates(node)
            if not nif_candidates:
                continue
            if not _node_looks_like_light_placer(node) and not likely_lp_path:
                continue

            settings = normalized_settings(dict(node))
            for raw_path in sorted(nif_candidates):
                canonical = canonical_nif(raw_path)
                if canonical is None:
                    issues.append(
                        ParseIssue(
                            severity="warn",
                            message=f"Invalid NIF path '{raw_path}'",
                            source_file=candidate.relative_path,
                            source_mod=candidate.mod_name,
                        )
                    )
                    continue

                sig = value_signature(settings)
                dedupe_key = (canonical, sig)
                if dedupe_key in seen:
                    continue
                seen.add(dedupe_key)

                entry_id = value_signature(
                    {
                        "mod": candidate.mod_name,
                        "priority": candidate.mod_priority,
                        "file": candidate.relative_path,
                        "nif": canonical,
                        "settings": settings,
                    }
                )
                entries.append(
                    LightPlacerEntry(
                        entry_id=entry_id,
                        source_mod=candidate.mod_name,
                        source_priority=candidate.mod_priority,
                        source_file=candidate.relative_path,
                        nif_path_raw=raw_path,
                        nif_path_canonical=canonical,
                        settings=settings,
                        full_payload=dict(node),
                    )
                )

        if likely_lp_path and not entries:
            issues.append(
                ParseIssue(
                    severity="info",
                    message="No Light Placer-like entries found",
                    source_file=candidate.relative_path,
                    source_mod=candidate.mod_name,
                )
            )
        return entries, issues

    def build_output(self, entries: list[LightPlacerEntry]) -> list[dict[str, Any]]:
        return [entry.full_payload for entry in entries]


class ParticleLightsAdapter:
    name = "particle_lights"

    def can_parse(self, candidate: CandidateFile) -> bool:
        return candidate.file_path.suffix.lower() == ".json"

    def extract_entries(self, candidate: CandidateFile) -> tuple[list[ParticleLightTarget], list[ParseIssue]]:
        targets: list[ParticleLightTarget] = []
        issues: list[ParseIssue] = []
        if not self.can_parse(candidate):
            return targets, issues

        likely_pl_path = _path_suggests_particle_lights(candidate.relative_path)
        path_is_lightplacer = _path_suggests_light_placer(candidate.relative_path)
        try:
            root = _load_json(candidate.file_path)
        except Exception as exc:  # noqa: BLE001
            if likely_pl_path:
                issues.append(
                    ParseIssue(
                        severity="warn",
                        message=f"JSON parse failed: {exc}",
                        source_file=candidate.relative_path,
                        source_mod=candidate.mod_name,
                    )
                )
            return targets, issues

        root_has_pl_hints = _root_has_particle_hints(root)
        if path_is_lightplacer and not root_has_pl_hints:
            # Avoid treating LightPlacer JSON as Particle Lights based on generic nif fields.
            return targets, issues
        if not likely_pl_path and not root_has_pl_hints:
            return targets, issues

        seen: set[tuple[str, str]] = set()
        for node in _iter_dict_nodes(root):
            if not _node_looks_like_particle_lights(node):
                continue
            nif_candidates = _extract_direct_nif_candidates(node)
            if not nif_candidates:
                continue
            payload_signature = value_signature(node)
            for raw_path in sorted(nif_candidates):
                canonical = canonical_nif(raw_path)
                if canonical is None:
                    issues.append(
                        ParseIssue(
                            severity="warn",
                            message=f"Invalid NIF path '{raw_path}'",
                            source_file=candidate.relative_path,
                            source_mod=candidate.mod_name,
                        )
                    )
                    continue

                dedupe_key = (canonical, payload_signature)
                if dedupe_key in seen:
                    continue
                seen.add(dedupe_key)

                targets.append(
                    ParticleLightTarget(
                        source_mod=candidate.mod_name,
                        source_priority=candidate.mod_priority,
                        source_file=candidate.relative_path,
                        nif_path_raw=raw_path,
                        nif_path_canonical=canonical,
                        payload=dict(node),
                    )
                )
        return targets, issues

    def build_output(self, targets: list[ParticleLightTarget]) -> list[dict[str, Any]]:
        return [target.payload for target in targets]


def parse_light_placer_files(candidates: list[CandidateFile]) -> tuple[list[LightPlacerEntry], list[ParseIssue]]:
    adapter = LightPlacerAdapter()
    entries: list[LightPlacerEntry] = []
    issues: list[ParseIssue] = []
    for candidate in candidates:
        parsed_entries, parsed_issues = adapter.extract_entries(candidate)
        entries.extend(parsed_entries)
        issues.extend(parsed_issues)
    return entries, issues


def parse_particle_light_files(candidates: list[CandidateFile]) -> tuple[list[ParticleLightTarget], list[ParseIssue]]:
    adapter = ParticleLightsAdapter()
    targets: list[ParticleLightTarget] = []
    issues: list[ParseIssue] = []
    for candidate in candidates:
        parsed_targets, parsed_issues = adapter.extract_entries(candidate)
        targets.extend(parsed_targets)
        issues.extend(parsed_issues)
    return targets, issues
