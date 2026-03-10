from __future__ import annotations

from pathlib import Path

from .models import CandidateFile, ModEntry

DEFAULT_LP_GLOBS = [
    "**/LightPlacer/**/*.json",
    "**/lightplacer/**/*.json",
    "**/*light*placer*.json",
]

DEFAULT_PL_GLOBS = [
    "**/ParticleLights/**/*.json",
    "**/particlelights/**/*.json",
    "**/*particle*light*.json",
]


def _discover_for_category(mods: list[ModEntry], patterns: list[str], category: str) -> list[CandidateFile]:
    discovered: list[CandidateFile] = []
    for mod in mods:
        if not mod.path.exists() or not mod.path.is_dir():
            continue
        seen_rel_paths: set[str] = set()
        for pattern in patterns:
            for match in mod.path.glob(pattern):
                if not match.is_file() or match.suffix.lower() != ".json":
                    continue
                relative_path = match.relative_to(mod.path).as_posix()
                if relative_path in seen_rel_paths:
                    continue
                seen_rel_paths.add(relative_path)
                discovered.append(
                    CandidateFile(
                        category=category,
                        mod_name=mod.name,
                        mod_priority=mod.priority,
                        relative_path=relative_path,
                        file_path=Path(match),
                    )
                )

    discovered.sort(key=lambda item: (item.mod_priority, item.mod_name.lower(), item.relative_path.lower()))
    return discovered


def discover_candidates(
    mods: list[ModEntry],
    lp_patterns: list[str] | None = None,
    pl_patterns: list[str] | None = None,
) -> tuple[list[CandidateFile], list[CandidateFile]]:
    lp = _discover_for_category(mods, lp_patterns or DEFAULT_LP_GLOBS, category="lp")
    pl = _discover_for_category(mods, pl_patterns or DEFAULT_PL_GLOBS, category="pl")
    return lp, pl
