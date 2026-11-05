"""Atomic, verified compilation checkpoints outside the installable cache."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

from build_provenance import canonical_bytes, sha256_bytes, sha256_file
from shader_bytecode import validate_dxbc
from release_worker_support import checked_path

MAX_BYTECODE_BYTES = 512 * 1024 * 1024


def atomic_bytes(path: Path, data: bytes) -> None:
    """Publish a complete file; interrupted writes never become checkpoints."""
    checked_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def atomic_json(path: Path, value: dict) -> None:
    atomic_bytes(path, canonical_bytes(value))


class Checkpoints:
    """Reuse only bytecode whose inputs, hash, length, and shader stage match."""

    def __init__(self, root: Path):
        self.root = checked_path(root)
        self.root.mkdir(parents=True, exist_ok=True)

    def paths(self, inputs: dict) -> tuple[Path, Path]:
        key = sha256_bytes(canonical_bytes(inputs))
        return self.root / f"{key}.json", self.root / f"{key}.dxbc"

    def restore(self, inputs: dict, target: Path, extension: str) -> dict | None:
        inputs = json.loads(canonical_bytes(inputs))
        record_path, object_path = self.paths(inputs)
        if not record_path.exists():
            return None
        try:
            checked_path(record_path)
            checked_path(object_path)
            if record_path.stat().st_size > 4 * 1024 * 1024 or object_path.stat().st_size > MAX_BYTECODE_BYTES:
                raise ValueError("Checkpoint size limit exceeded")
            record = json.loads(record_path.read_bytes())
            data = object_path.read_bytes()
            if (record["inputs"] != inputs or type(record["size"]) is not int or record["size"] != len(data)
                    or record["sha256"] != sha256_bytes(data)):
                raise ValueError("Checkpoint identity or bytecode hash mismatch")
            if record.get("origin") != "compiled" or not isinstance(record.get("diagnostics"), str):
                raise ValueError("Checkpoint lacks compiler provenance or diagnostics")
            validate_dxbc(data, extension)
        except (OSError, ValueError, KeyError, TypeError) as error:
            print(f"Rejected checkpoint {record_path.name}: {error}", flush=True)
            return None
        checked_path(target)
        if not target.is_file() or sha256_file(target) != record["sha256"]:
            atomic_bytes(target, data)
        return record

    def save(self, inputs: dict, source: Path, extension: str,
             diagnostics: str = "") -> dict:
        inputs = json.loads(canonical_bytes(inputs))
        checked_path(source)
        if source.stat().st_size > MAX_BYTECODE_BYTES:
            raise ValueError("Bytecode size limit exceeded")
        data = source.read_bytes()
        validate_dxbc(data, extension)
        record_path, object_path = self.paths(inputs)
        record = {"inputs": inputs, "size": len(data), "sha256": sha256_bytes(data),
                  "diagnostics": diagnostics, "origin": "compiled"}
        atomic_bytes(object_path, data)
        atomic_json(record_path, record)
        return record
