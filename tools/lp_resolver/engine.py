from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .conflicts import detect_conflicts, filter_conflicts
from .discovery import DEFAULT_LP_GLOBS, DEFAULT_PL_GLOBS, discover_candidates
from .mo2 import read_enabled_mods, resolve_mods_dir, resolve_profile_path
from .models import Conflict, LightPlacerEntry, ParseIssue, ParticleLightTarget
from .parsers import parse_light_placer_files, parse_particle_light_files
from .pl_nif_scan import DEFAULT_PL_NIF_GLOBS, DEFAULT_PL_NIF_MOD_PATTERNS, discover_particle_light_nif_targets
from .reporting import build_report_payload, write_reports


@dataclass
class ScanConfig:
    mo2_root: Path
    profile: str | None = None
    profile_path: Path | None = None
    mods_dir: Path | None = None
    output_dir: Path = Path("dist/lp_resolver")
    lp_globs: list[str] = field(default_factory=list)
    pl_globs: list[str] = field(default_factory=list)
    pl_source: str = "nif"
    pl_nif_mod_patterns: list[str] = field(default_factory=list)
    pl_nif_globs: list[str] = field(default_factory=list)
    only_overlap: bool = False
    ignore_duplicate_exact: bool = False
    cross_mod_lp_duplicates_only: bool = False
    json_indent: int = 2


@dataclass
class ScanResult:
    config: ScanConfig
    mo2_root: Path
    profile_path: Path
    mods_dir: Path
    enabled_mod_count: int
    lp_candidate_files: int
    pl_candidate_files: int
    pl_json_candidate_files: int
    pl_nif_candidate_files: int
    pl_nif_matched_mods: int
    lp_entries: list[LightPlacerEntry]
    pl_targets: list[ParticleLightTarget]
    issues: list[ParseIssue]
    detected_conflicts: list[Conflict]
    conflicts: list[Conflict]
    report_payload: dict
    report_json_path: Path | None = None
    report_md_path: Path | None = None


def _validate_scan_config(config: ScanConfig) -> None:
    if not config.profile and not config.profile_path:
        raise ValueError("Provide either profile or profile_path.")
    if config.pl_source not in {"nif", "json", "both"}:
        raise ValueError("pl_source must be one of: nif, json, both")


def run_scan(config: ScanConfig, write_output_reports: bool = True) -> ScanResult:
    _validate_scan_config(config)

    mo2_root = config.mo2_root.expanduser().resolve()
    profile_path = resolve_profile_path(mo2_root, config.profile, config.profile_path)
    mods_dir = resolve_mods_dir(mo2_root, config.mods_dir)
    enabled_mods = read_enabled_mods(profile_path, mods_dir)

    lp_patterns = config.lp_globs or DEFAULT_LP_GLOBS
    pl_patterns = config.pl_globs or DEFAULT_PL_GLOBS
    pl_json_patterns = (
        pl_patterns if config.pl_source in {"json", "both"} else ["**/__lp_resolver_disabled_pl_json_scan__.json"]
    )
    lp_candidates, pl_candidates = discover_candidates(
        enabled_mods,
        lp_patterns=lp_patterns,
        pl_patterns=pl_json_patterns,
    )

    lp_entries, lp_issues = parse_light_placer_files(lp_candidates)
    pl_targets: list[ParticleLightTarget] = []
    pl_issues: list[ParseIssue] = []
    pl_json_candidate_count = 0
    pl_nif_candidate_count = 0
    pl_nif_matched_mods = 0

    if config.pl_source in {"json", "both"}:
        pl_targets_json, pl_issues = parse_particle_light_files(pl_candidates)
        pl_targets.extend(pl_targets_json)
        pl_json_candidate_count = len(pl_candidates)

    if config.pl_source in {"nif", "both"}:
        pl_nif_mod_patterns = config.pl_nif_mod_patterns or DEFAULT_PL_NIF_MOD_PATTERNS
        pl_nif_globs = config.pl_nif_globs or DEFAULT_PL_NIF_GLOBS
        pl_targets_nif, pl_nif_candidate_count, pl_nif_matched_mods = discover_particle_light_nif_targets(
            enabled_mods,
            mod_name_patterns=pl_nif_mod_patterns,
            nif_globs=pl_nif_globs,
        )
        pl_targets.extend(pl_targets_nif)

    if len(pl_targets) > 1:
        deduped_targets = {}
        for target in pl_targets:
            dedupe_key = (target.source_mod, target.source_file, target.nif_path_canonical)
            deduped_targets[dedupe_key] = target
        pl_targets = sorted(
            deduped_targets.values(),
            key=lambda item: (item.source_priority, item.source_mod.lower(), item.source_file.lower()),
        )

    all_issues = [*lp_issues, *pl_issues]
    pl_candidate_total = pl_json_candidate_count + pl_nif_candidate_count

    detected_conflicts = detect_conflicts(lp_entries, pl_targets)
    conflicts = filter_conflicts(
        detected_conflicts,
        only_overlap=config.only_overlap,
        ignore_duplicate_exact=config.ignore_duplicate_exact,
        cross_mod_lp_duplicates_only=config.cross_mod_lp_duplicates_only,
    )

    payload = build_report_payload(
        mo2_root=mo2_root,
        profile_path=profile_path,
        mods_dir=mods_dir,
        enabled_mod_count=len(enabled_mods),
        lp_candidate_files=len(lp_candidates),
        pl_candidate_files=pl_candidate_total,
        lp_entries=len(lp_entries),
        pl_targets=len(pl_targets),
        conflicts=conflicts,
        issues=all_issues,
    )

    json_path = None
    md_path = None
    if write_output_reports:
        json_path, md_path = write_reports(config.output_dir, payload, json_indent=config.json_indent)

    return ScanResult(
        config=config,
        mo2_root=mo2_root,
        profile_path=profile_path,
        mods_dir=mods_dir,
        enabled_mod_count=len(enabled_mods),
        lp_candidate_files=len(lp_candidates),
        pl_candidate_files=pl_candidate_total,
        pl_json_candidate_files=pl_json_candidate_count,
        pl_nif_candidate_files=pl_nif_candidate_count,
        pl_nif_matched_mods=pl_nif_matched_mods,
        lp_entries=lp_entries,
        pl_targets=pl_targets,
        issues=all_issues,
        detected_conflicts=detected_conflicts,
        conflicts=conflicts,
        report_payload=payload,
        report_json_path=json_path,
        report_md_path=md_path,
    )

