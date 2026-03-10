from __future__ import annotations

from fnmatch import fnmatch

from .models import ModEntry, ParticleLightTarget
from .normalize import canonical_nif

DEFAULT_PL_NIF_MOD_PATTERNS = [
    "*enb*particle*light*",
    "*particle*light*enb*",
    "enb-particlelights*",
]

DEFAULT_PL_NIF_GLOBS = [
    "meshes/**/*.nif",
]


def _matches_any_pattern(value: str, patterns: list[str]) -> bool:
    lowered_value = value.lower()
    for pattern in patterns:
        if fnmatch(lowered_value, pattern.lower()):
            return True
    return False


def discover_particle_light_nif_targets(
    mods: list[ModEntry],
    mod_name_patterns: list[str] | None = None,
    nif_globs: list[str] | None = None,
) -> tuple[list[ParticleLightTarget], int, int]:
    patterns = mod_name_patterns or DEFAULT_PL_NIF_MOD_PATTERNS
    meshes_globs = nif_globs or DEFAULT_PL_NIF_GLOBS

    targets: list[ParticleLightTarget] = []
    scanned_nif_files = 0
    matched_mods = 0

    for mod in mods:
        if not _matches_any_pattern(mod.name, patterns):
            continue
        if not mod.path.exists() or not mod.path.is_dir():
            continue

        matched_mods += 1
        seen_rel_paths: set[str] = set()
        for mesh_glob in meshes_globs:
            for nif_file in mod.path.glob(mesh_glob):
                if not nif_file.is_file() or nif_file.suffix.lower() != ".nif":
                    continue

                relative_path = nif_file.relative_to(mod.path).as_posix()
                if relative_path in seen_rel_paths:
                    continue
                seen_rel_paths.add(relative_path)
                scanned_nif_files += 1

                canonical = canonical_nif(relative_path)
                if canonical is None:
                    continue

                targets.append(
                    ParticleLightTarget(
                        source_mod=mod.name,
                        source_priority=mod.priority,
                        source_file=relative_path,
                        nif_path_raw=relative_path,
                        nif_path_canonical=canonical,
                        payload={
                            "kind": "enb_particle_lights_nif",
                            "nif_file": relative_path,
                        },
                    )
                )

    targets.sort(key=lambda item: (item.source_priority, item.source_mod.lower(), item.nif_path_canonical))
    return targets, scanned_nif_files, matched_mods

