from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from .decisions import Decision
from .engine import ScanResult
from .models import LightPlacerEntry
from .reporting import render_markdown_report


@dataclass
class PatchWriteResult:
    patch_mod_dir: Path
    patch_json_path: Path
    decisions_path: Path
    report_path: Path
    selected_nif_count: int
    selected_entry_count: int
    warnings: list[str]


def _select_entry_for_decision(
    entries: list[LightPlacerEntry],
    decision: Decision,
    warnings: list[str],
    nif_path: str,
) -> list[LightPlacerEntry]:
    if not entries:
        return []
    sorted_entries = sorted(
        entries,
        key=lambda item: (item.source_priority, item.source_mod.lower(), item.source_file.lower(), item.entry_id),
    )
    highest_priority_entry = sorted_entries[-1]

    if decision.action == "disable_lp":
        return []
    if decision.action == "ignore":
        return []
    if decision.action == "keep_highest_priority":
        return [highest_priority_entry]
    if decision.action == "choose_entry":
        if decision.entry_id:
            for entry in sorted_entries:
                if entry.entry_id == decision.entry_id:
                    return [entry]
        warnings.append(f"{nif_path}: choose_entry could not find entry_id '{decision.entry_id}', used highest priority.")
        return [highest_priority_entry]

    warnings.append(f"{nif_path}: unknown decision action '{decision.action}', no changes applied.")
    return []


def write_patch_mod(
    scan_result: ScanResult,
    decisions: dict[str, Decision],
    patch_mod_name: str = "LP_ConflictPatch",
) -> PatchWriteResult:
    patch_mod_dir = scan_result.mods_dir / patch_mod_name
    patch_json_path = patch_mod_dir / "LightPlacer" / patch_mod_name / "resolved.json"
    decisions_path = patch_mod_dir / "resolver_decisions.json"
    report_path = patch_mod_dir / "resolver_report.md"

    patch_json_path.parent.mkdir(parents=True, exist_ok=True)
    patch_mod_dir.mkdir(parents=True, exist_ok=True)

    lp_entries_by_nif: dict[str, list[LightPlacerEntry]] = {}
    for entry in scan_result.lp_entries:
        lp_entries_by_nif.setdefault(entry.nif_path_canonical, []).append(entry)

    warnings: list[str] = []
    selected_entries: list[LightPlacerEntry] = []
    selected_nif_count = 0
    for nif_path, decision in sorted(decisions.items()):
        entries = lp_entries_by_nif.get(nif_path, [])
        chosen = _select_entry_for_decision(entries, decision, warnings, nif_path)
        if decision.action != "ignore":
            selected_nif_count += 1
        selected_entries.extend(chosen)

    deduped_entries: dict[str, LightPlacerEntry] = {entry.entry_id: entry for entry in selected_entries}
    ordered_entries = sorted(
        deduped_entries.values(),
        key=lambda item: (item.nif_path_canonical, item.source_priority, item.source_mod.lower(), item.entry_id),
    )
    payload = [entry.full_payload for entry in ordered_entries]
    patch_json_path.write_text(json.dumps(payload, indent=2, sort_keys=False), encoding="utf-8")

    decisions_payload = {
        "version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "patch_mod_name": patch_mod_name,
        "decisions": {
            nif_path: {
                "action": decision.action,
                "entry_id": decision.entry_id,
                "note": decision.note,
                "updated_at_utc": decision.updated_at_utc,
            }
            for nif_path, decision in sorted(decisions.items())
        },
    }
    decisions_path.write_text(json.dumps(decisions_payload, indent=2, sort_keys=False), encoding="utf-8")

    report_text = render_markdown_report(scan_result.report_payload)
    report_text += "\n## Patch Export\n"
    report_text += f"- Patch mod dir: `{patch_mod_dir}`\n"
    report_text += f"- Selected NIF decisions: {selected_nif_count}\n"
    report_text += f"- Exported LP entries: {len(ordered_entries)}\n"
    if warnings:
        report_text += "- Warnings:\n"
        for warning in warnings:
            report_text += f"  - {warning}\n"
    report_path.write_text(report_text, encoding="utf-8")

    return PatchWriteResult(
        patch_mod_dir=patch_mod_dir,
        patch_json_path=patch_json_path,
        decisions_path=decisions_path,
        report_path=report_path,
        selected_nif_count=selected_nif_count,
        selected_entry_count=len(ordered_entries),
        warnings=warnings,
    )

