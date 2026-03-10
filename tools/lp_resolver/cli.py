from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .engine import ScanConfig, run_scan


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Scan MO2 mods for Light Placer and Particle Light conflicts.")
    parser.add_argument("--mo2-root", required=True, type=Path, help="Path to MO2 root (contains mods/, profiles/).")
    parser.add_argument("--profile", type=str, help="MO2 profile name (under profiles/).")
    parser.add_argument("--profile-path", type=Path, help="Explicit profile directory path.")
    parser.add_argument("--mods-dir", type=Path, help="Explicit mods directory path override.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("dist/lp_resolver"),
        help="Output directory for report.json and report.md.",
    )
    parser.add_argument(
        "--lp-glob",
        action="append",
        default=[],
        help="Override/add Light Placer JSON glob(s). May be passed multiple times.",
    )
    parser.add_argument(
        "--pl-glob",
        action="append",
        default=[],
        help="Override/add Particle Lights JSON glob(s). Used when --pl-source is json/both.",
    )
    parser.add_argument(
        "--pl-source",
        choices=["nif", "json", "both"],
        default="nif",
        help="Particle Light source: ENB NIF scan (default), JSON scan, or both.",
    )
    parser.add_argument(
        "--pl-nif-mod-pattern",
        action="append",
        default=[],
        help="Mod name wildcard for ENB Particle Lights NIF scan (repeatable).",
    )
    parser.add_argument(
        "--pl-nif-glob",
        action="append",
        default=[],
        help="NIF path glob inside matching mods for ENB Particle Lights scan (repeatable).",
    )
    parser.add_argument("--json-indent", type=int, default=2, help="JSON indent size.")
    parser.add_argument(
        "--only-overlap",
        action="store_true",
        help="Keep only conflicts that include lp_vs_pl_overlap.",
    )
    parser.add_argument(
        "--ignore-duplicate-exact",
        action="store_true",
        help="Drop duplicate_exact conflict type (and remove conflicts that only had duplicate_exact).",
    )
    parser.add_argument(
        "--cross-mod-lp-duplicates-only",
        action="store_true",
        help="Keep LP duplicate types only when LP entries come from more than one source mod.",
    )
    parser.add_argument("--fail-on-conflicts", action="store_true", help="Exit with code 2 when conflicts are found.")
    parser.add_argument("--verbose", action="store_true", help="Print extra scan details.")
    return parser


def _config_from_args(args: argparse.Namespace) -> ScanConfig:
    return ScanConfig(
        mo2_root=args.mo2_root,
        profile=args.profile,
        profile_path=args.profile_path,
        mods_dir=args.mods_dir,
        output_dir=args.output_dir,
        lp_globs=args.lp_glob,
        pl_globs=args.pl_glob,
        pl_source=args.pl_source,
        pl_nif_mod_patterns=args.pl_nif_mod_pattern,
        pl_nif_globs=args.pl_nif_glob,
        only_overlap=args.only_overlap,
        ignore_duplicate_exact=args.ignore_duplicate_exact,
        cross_mod_lp_duplicates_only=args.cross_mod_lp_duplicates_only,
        json_indent=args.json_indent,
    )


def run(args: argparse.Namespace) -> int:
    scan_result = run_scan(_config_from_args(args), write_output_reports=True)

    print(f"Enabled mods: {scan_result.enabled_mod_count}")
    print(f"LP candidate files: {scan_result.lp_candidate_files}")
    print(f"PL source mode: {scan_result.config.pl_source}")
    print(f"PL candidate files: {scan_result.pl_candidate_files}")
    if scan_result.config.pl_source in {"json", "both"}:
        print(f"PL JSON candidate files: {scan_result.pl_json_candidate_files}")
    if scan_result.config.pl_source in {"nif", "both"}:
        print(f"PL NIF candidate files: {scan_result.pl_nif_candidate_files}")
        print(f"PL NIF matched mods: {scan_result.pl_nif_matched_mods}")
    print(f"LP entries: {len(scan_result.lp_entries)}")
    print(f"PL targets: {len(scan_result.pl_targets)}")

    if (
        scan_result.config.only_overlap
        or scan_result.config.ignore_duplicate_exact
        or scan_result.config.cross_mod_lp_duplicates_only
    ):
        print(f"Conflicts (raw): {len(scan_result.detected_conflicts)}")
        print(f"Conflicts (filtered): {len(scan_result.conflicts)}")
    else:
        print(f"Conflicts: {len(scan_result.conflicts)}")

    if scan_result.report_json_path is not None:
        print(f"Wrote: {scan_result.report_json_path}")
    if scan_result.report_md_path is not None:
        print(f"Wrote: {scan_result.report_md_path}")

    if args.verbose and scan_result.issues:
        for issue in scan_result.issues:
            if issue.severity == "info":
                continue
            print(f"[{issue.severity}] {issue.source_mod}/{issue.source_file}: {issue.message}")

    if args.fail_on_conflicts and scan_result.conflicts:
        return 2
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

