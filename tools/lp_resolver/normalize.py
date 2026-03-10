from __future__ import annotations

import hashlib
import json
import posixpath
import re
from typing import Any

_PATH_KEY_HINTS = ("nif", "mesh", "model", "path", "file")
_EXCLUDED_SETTING_KEYS = {
    "nif",
    "mesh",
    "model",
    "path",
    "filepath",
    "file_path",
    "source",
    "sourcemod",
    "source_mod",
    "sourcefile",
    "source_file",
    "comment",
    "comments",
    "description",
    "name",
    "id",
    "editorid",
    "editor_id",
}


def canonical_nif(path: str | None) -> str | None:
    if not path:
        return None
    value = path.strip().replace("\\", "/").lower()
    value = re.sub(r"[?#].*$", "", value)
    value = re.sub(r"/+", "/", value)
    while value.startswith("./"):
        value = value[2:]
    if value.startswith("data/"):
        value = value[5:]
    value = posixpath.normpath(value).replace("\\", "/")
    if value in (".", ""):
        return None
    if not value.startswith("meshes/"):
        value = f"meshes/{value.lstrip('/')}"
    if not value.endswith(".nif"):
        return None
    return value


def _normalize_value(value: Any, parent_key: str = "") -> Any:
    if isinstance(value, dict):
        normalized: dict[str, Any] = {}
        for key in sorted(value.keys(), key=lambda k: k.lower()):
            key_l = key.lower()
            if key_l in _EXCLUDED_SETTING_KEYS:
                continue
            if any(hint in key_l for hint in _PATH_KEY_HINTS):
                continue
            normalized[key] = _normalize_value(value[key], key_l)
        return normalized
    if isinstance(value, list):
        return [_normalize_value(item, parent_key) for item in value]
    if isinstance(value, float):
        return round(value, 6)
    return value


def normalized_settings(payload: dict[str, Any]) -> dict[str, Any]:
    normalized = _normalize_value(payload)
    if isinstance(normalized, dict):
        return normalized
    return {}


def value_signature(value: Any) -> str:
    packed = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha1(packed.encode("utf-8")).hexdigest()

