from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class ModEntry:
    name: str
    priority: int
    path: Path


@dataclass(frozen=True)
class CandidateFile:
    category: str
    mod_name: str
    mod_priority: int
    relative_path: str
    file_path: Path


@dataclass(frozen=True)
class ParseIssue:
    severity: str
    message: str
    source_file: str
    source_mod: str


@dataclass(frozen=True)
class LightPlacerEntry:
    entry_id: str
    source_mod: str
    source_priority: int
    source_file: str
    nif_path_raw: str
    nif_path_canonical: str
    settings: dict[str, Any]
    full_payload: dict[str, Any]


@dataclass(frozen=True)
class ParticleLightTarget:
    source_mod: str
    source_priority: int
    source_file: str
    nif_path_raw: str
    nif_path_canonical: str
    payload: dict[str, Any]


@dataclass
class Conflict:
    nif_path_canonical: str
    conflict_types: list[str]
    lp_entries: list[LightPlacerEntry] = field(default_factory=list)
    pl_targets: list[ParticleLightTarget] = field(default_factory=list)

