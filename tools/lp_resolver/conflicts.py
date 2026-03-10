from __future__ import annotations

from collections import defaultdict

from .models import Conflict, LightPlacerEntry, ParticleLightTarget
from .normalize import value_signature


def detect_conflicts(lp_entries: list[LightPlacerEntry], pl_targets: list[ParticleLightTarget]) -> list[Conflict]:
    lp_by_nif: dict[str, list[LightPlacerEntry]] = defaultdict(list)
    pl_by_nif: dict[str, list[ParticleLightTarget]] = defaultdict(list)

    for entry in lp_entries:
        lp_by_nif[entry.nif_path_canonical].append(entry)
    for target in pl_targets:
        pl_by_nif[target.nif_path_canonical].append(target)

    conflicts: list[Conflict] = []
    all_nifs = sorted(set(lp_by_nif.keys()) | set(pl_by_nif.keys()))
    for nif in all_nifs:
        lp_candidates = sorted(
            lp_by_nif.get(nif, []),
            key=lambda item: (item.source_priority, item.source_mod.lower(), item.source_file.lower(), item.entry_id),
        )
        pl_candidates = sorted(
            pl_by_nif.get(nif, []),
            key=lambda item: (item.source_priority, item.source_mod.lower(), item.source_file.lower()),
        )

        conflict_types: list[str] = []
        if len(lp_candidates) > 1:
            sigs = {value_signature(entry.settings) for entry in lp_candidates}
            conflict_types.append("duplicate_exact" if len(sigs) == 1 else "duplicate_divergent")
        if lp_candidates and pl_candidates:
            conflict_types.append("lp_vs_pl_overlap")

        if conflict_types:
            conflicts.append(
                Conflict(
                    nif_path_canonical=nif,
                    conflict_types=conflict_types,
                    lp_entries=lp_candidates,
                    pl_targets=pl_candidates,
                )
            )
    return conflicts


def filter_conflicts(
    conflicts: list[Conflict],
    *,
    only_overlap: bool = False,
    ignore_duplicate_exact: bool = False,
    cross_mod_lp_duplicates_only: bool = False,
) -> list[Conflict]:
    filtered: list[Conflict] = []
    duplicate_types = {"duplicate_exact", "duplicate_divergent"}
    for conflict in conflicts:
        conflict_types = [
            conflict_type
            for conflict_type in conflict.conflict_types
            if not (ignore_duplicate_exact and conflict_type == "duplicate_exact")
        ]

        if cross_mod_lp_duplicates_only:
            source_mods = {entry.source_mod for entry in conflict.lp_entries}
            has_cross_mod_duplicates = len(source_mods) > 1
            if not has_cross_mod_duplicates:
                conflict_types = [conflict_type for conflict_type in conflict_types if conflict_type not in duplicate_types]

        if only_overlap and "lp_vs_pl_overlap" not in conflict_types:
            continue
        if not conflict_types:
            continue

        filtered.append(
            Conflict(
                nif_path_canonical=conflict.nif_path_canonical,
                conflict_types=conflict_types,
                lp_entries=conflict.lp_entries,
                pl_targets=conflict.pl_targets,
            )
        )
    return filtered
