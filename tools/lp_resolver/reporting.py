from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .models import Conflict, ParseIssue


def build_report_payload(
    *,
    mo2_root: Path,
    profile_path: Path,
    mods_dir: Path,
    enabled_mod_count: int,
    lp_candidate_files: int,
    pl_candidate_files: int,
    lp_entries: int,
    pl_targets: int,
    conflicts: list[Conflict],
    issues: list[ParseIssue],
) -> dict[str, Any]:
    return {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "mo2_root": str(mo2_root),
        "profile_path": str(profile_path),
        "mods_dir": str(mods_dir),
        "summary": {
            "enabled_mod_count": enabled_mod_count,
            "lp_candidate_files": lp_candidate_files,
            "pl_candidate_files": pl_candidate_files,
            "lp_entries": lp_entries,
            "pl_targets": pl_targets,
            "conflict_count": len(conflicts),
        },
        "issues": [
            {
                "severity": issue.severity,
                "message": issue.message,
                "source_mod": issue.source_mod,
                "source_file": issue.source_file,
            }
            for issue in issues
        ],
        "conflicts": [
            {
                "nif_path_canonical": conflict.nif_path_canonical,
                "conflict_types": conflict.conflict_types,
                "lp_entries": [
                    {
                        "entry_id": entry.entry_id,
                        "source_mod": entry.source_mod,
                        "source_priority": entry.source_priority,
                        "source_file": entry.source_file,
                        "nif_path_raw": entry.nif_path_raw,
                        "nif_path_canonical": entry.nif_path_canonical,
                        "settings": entry.settings,
                    }
                    for entry in conflict.lp_entries
                ],
                "pl_targets": [
                    {
                        "source_mod": target.source_mod,
                        "source_priority": target.source_priority,
                        "source_file": target.source_file,
                        "nif_path_raw": target.nif_path_raw,
                        "nif_path_canonical": target.nif_path_canonical,
                    }
                    for target in conflict.pl_targets
                ],
            }
            for conflict in conflicts
        ],
    }


def render_markdown_report(payload: dict[str, Any]) -> str:
    summary = payload["summary"]
    lines: list[str] = [
        "# Light Placer Conflict Report",
        "",
        f"- Generated: {payload['generated_at_utc']}",
        f"- MO2 Root: `{payload['mo2_root']}`",
        f"- Profile: `{payload['profile_path']}`",
        f"- Mods Dir: `{payload['mods_dir']}`",
        "",
        "## Summary",
        f"- Enabled mods: {summary['enabled_mod_count']}",
        f"- Light Placer candidate files: {summary['lp_candidate_files']}",
        f"- Particle Lights candidate files: {summary['pl_candidate_files']}",
        f"- Normalized LP entries: {summary['lp_entries']}",
        f"- Normalized PL targets: {summary['pl_targets']}",
        f"- Conflicts: {summary['conflict_count']}",
    ]

    issues = payload.get("issues", [])
    if issues:
        lines.extend(["", "## Parse Issues"])
        for issue in issues:
            lines.append(
                f"- [{issue['severity']}] `{issue['source_mod']}/{issue['source_file']}`: {issue['message']}"
            )

    conflicts = payload.get("conflicts", [])
    if conflicts:
        lines.extend(["", "## Conflicts"])
        for conflict in conflicts:
            lines.extend(
                [
                    "",
                    f"### `{conflict['nif_path_canonical']}`",
                    f"- Types: {', '.join(conflict['conflict_types'])}",
                    f"- LP candidates: {len(conflict['lp_entries'])}",
                    f"- PL overlap: {'yes' if conflict['pl_targets'] else 'no'}",
                ]
            )
            if conflict["lp_entries"]:
                lines.append("- LP entries:")
                for entry in conflict["lp_entries"]:
                    lines.append(
                        f"  - {entry['source_mod']} (prio {entry['source_priority']}), `{entry['source_file']}`"
                    )
            if conflict["pl_targets"]:
                lines.append("- PL targets:")
                for target in conflict["pl_targets"]:
                    lines.append(
                        f"  - {target['source_mod']} (prio {target['source_priority']}), `{target['source_file']}`"
                    )
    else:
        lines.extend(["", "## Conflicts", "- No conflicts detected."])

    return "\n".join(lines) + "\n"


def write_reports(output_dir: Path, payload: dict[str, Any], json_indent: int = 2) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "report.json"
    md_path = output_dir / "report.md"

    json_path.write_text(json.dumps(payload, indent=json_indent, sort_keys=False), encoding="utf-8")
    md_path.write_text(render_markdown_report(payload), encoding="utf-8")
    return json_path, md_path

