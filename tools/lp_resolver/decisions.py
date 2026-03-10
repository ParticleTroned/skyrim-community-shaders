from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

from .models import Conflict

DECISIONS_VERSION = 1
VALID_ACTIONS = {"ignore", "keep_highest_priority", "choose_entry", "disable_lp"}


@dataclass
class Decision:
    action: str
    entry_id: str | None = None
    note: str = ""
    updated_at_utc: str = ""


def _decision_from_dict(raw: dict) -> Decision | None:
    action = str(raw.get("action", "")).strip()
    if action not in VALID_ACTIONS:
        return None
    entry_id = raw.get("entry_id")
    if entry_id is not None:
        entry_id = str(entry_id).strip() or None
    note = str(raw.get("note", ""))
    updated_at_utc = str(raw.get("updated_at_utc", ""))
    return Decision(action=action, entry_id=entry_id, note=note, updated_at_utc=updated_at_utc)


def load_decisions(path: Path) -> dict[str, Decision]:
    if not path.exists():
        return {}

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001
        return {}

    if not isinstance(payload, dict):
        return {}
    if int(payload.get("version", 0)) != DECISIONS_VERSION:
        return {}

    decisions_payload = payload.get("decisions", {})
    if not isinstance(decisions_payload, dict):
        return {}

    decisions: dict[str, Decision] = {}
    for nif_path, raw_decision in decisions_payload.items():
        if not isinstance(raw_decision, dict):
            continue
        decision = _decision_from_dict(raw_decision)
        if decision is None:
            continue
        decisions[str(nif_path)] = decision
    return decisions


def save_decisions(path: Path, decisions: dict[str, Decision]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": DECISIONS_VERSION,
        "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        "decisions": {nif_path: asdict(decision) for nif_path, decision in sorted(decisions.items())},
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=False), encoding="utf-8")


def apply_decisions(conflicts: list[Conflict], decisions: dict[str, Decision]) -> tuple[dict[str, Decision], list[str]]:
    available_nifs = {conflict.nif_path_canonical for conflict in conflicts}
    applied: dict[str, Decision] = {}
    stale: list[str] = []
    for nif_path, decision in decisions.items():
        if nif_path in available_nifs:
            applied[nif_path] = decision
        else:
            stale.append(nif_path)
    return applied, sorted(stale)


def make_decision(action: str, entry_id: str | None = None, note: str = "") -> Decision:
    if action not in VALID_ACTIONS:
        raise ValueError(f"Invalid decision action: {action}")
    return Decision(
        action=action,
        entry_id=entry_id,
        note=note,
        updated_at_utc=datetime.now(timezone.utc).isoformat(),
    )

