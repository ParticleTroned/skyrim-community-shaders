from __future__ import annotations

import configparser
from pathlib import Path

from .models import ModEntry


def resolve_profile_path(mo2_root: Path, profile_name: str | None, profile_path: Path | None) -> Path:
    if profile_path is not None:
        resolved = profile_path.expanduser().resolve()
    elif profile_name is not None:
        resolved = (mo2_root / "profiles" / profile_name).resolve()
    else:
        raise ValueError("Either profile_name or profile_path must be provided.")

    if not resolved.exists():
        raise FileNotFoundError(f"Profile path does not exist: {resolved}")
    if not resolved.is_dir():
        raise NotADirectoryError(f"Profile path is not a directory: {resolved}")
    return resolved


def resolve_mods_dir(mo2_root: Path, explicit_mods_dir: Path | None = None) -> Path:
    if explicit_mods_dir is not None:
        resolved = explicit_mods_dir.expanduser().resolve()
        if not resolved.exists():
            raise FileNotFoundError(f"Mods directory does not exist: {resolved}")
        return resolved

    ini_path = mo2_root / "ModOrganizer.ini"
    if ini_path.exists():
        parser = configparser.ConfigParser()
        parser.read(ini_path, encoding="utf-8")
        if parser.has_option("General", "mod_directory"):
            configured = parser.get("General", "mod_directory").strip()
            if configured:
                candidate = Path(configured)
                if not candidate.is_absolute():
                    candidate = (mo2_root / candidate).resolve()
                if candidate.exists():
                    return candidate

    default_mods_dir = (mo2_root / "mods").resolve()
    if not default_mods_dir.exists():
        raise FileNotFoundError(
            f"Could not resolve mods directory; expected at {default_mods_dir} or from ModOrganizer.ini."
        )
    return default_mods_dir


def read_enabled_mods(profile_path: Path, mods_dir: Path) -> list[ModEntry]:
    modlist_path = profile_path / "modlist.txt"
    if not modlist_path.exists():
        raise FileNotFoundError(f"modlist.txt not found: {modlist_path}")

    lines = modlist_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    enabled_names: list[str] = []
    for raw_line in lines:
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        marker = line[0]
        mod_name = line[1:].strip()
        if marker == "+" and mod_name:
            enabled_names.append(mod_name)

    entries: list[ModEntry] = []
    for priority, mod_name in enumerate(enabled_names):
        entries.append(ModEntry(name=mod_name, priority=priority, path=mods_dir / mod_name))
    return entries

