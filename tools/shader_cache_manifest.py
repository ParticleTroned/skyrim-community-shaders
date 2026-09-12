"""Runtime-parity manifests using the pinned compiler's resolved macro tasks."""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
from collections.abc import Callable, Iterable
from itertools import groupby
from pathlib import Path


SCHEMA_VERSION = 2
STAGE_EXTENSIONS = {"PSHADER": ".pso", "VSHADER": ".vso", "CSHADER": ".cso"}
CompileTask = tuple[str, str, str, list[str]]


def prepare_fxc_defines(config: dict) -> None:
    """Make bare captured D3D macros explicitly empty instead of FXC's default 1."""
    def explicit_values(values: list) -> list:
        return [
            explicit_values(value) if isinstance(value, list)
            else value if "=" in value else value + "="
            for value in values
        ]

    for shader in config["shaders"]:
        for stage in shader["configs"].values():
            stage["common_defines"] = explicit_values(stage.get("common_defines", []))
            for entry in stage.get("entries", []):
                entry["defines"] = explicit_values(entry.get("defines", []))


def compile_task_defines(tasks: Iterable[CompileTask]) -> dict[tuple[str, str], str]:
    """Use the same file, descriptor and flattened defines as hlslkit-compile."""
    result: dict[tuple[str, str], str] = {}
    for source, stage, entry, defines in tasks:
        descriptor = entry.rsplit(":", 1)[-1]
        extension = STAGE_EXTENSIONS.get(stage.upper())
        if extension is None or not re.fullmatch(r"[0-9A-F]{1,8}", descriptor):
            raise ValueError(f"invalid cache compile task: {source}/{stage}/{entry}")
        key = (Path(source).stem, descriptor + extension)
        if key in result:
            raise ValueError(f"duplicate cache compile task: {key}")
        macros = []
        for define in defines:
            name, separator, value = define.partition("=")
            macros.append((name, value if separator else "1"))
        canonical = [value for value, _ in groupby(sorted(macros, key=lambda value: value[0]))]
        result[key] = "".join(
            f"{len(text.encode('utf-8'))}:{text}"
            for macro in canonical for text in macro
        )
    return result


def write_manifest(
    cache_dir: Path,
    shader_root: Path,
    global_defines_state: str,
    manifest_path: Path,
    resolve_source_name: Callable[[str], str] | None = None,
    *,
    compile_tasks: Iterable[CompileTask],
) -> int:
    """Fail closed if any blob lacks its exact compiler input or source closure."""
    from hlslkit.shader_digest import (
        combine_hashes,
        compute_shader_content_digest,
        hash_string,
        to_hex,
    )

    if sys.platform != "win32":
        raise RuntimeError("shader-cache include ordering requires Windows")
    task_defines = compile_task_defines(compile_tasks)
    global_digest = hash_string(global_defines_state)
    digest_cache: dict[str, int | None] = {}
    entries: dict[str, str] = {}
    seen_tasks: set[tuple[str, str]] = set()
    for blob in sorted(cache_dir.rglob("*")):
        if not blob.is_file() or blob.suffix.lower() not in STAGE_EXTENSIONS.values():
            continue
        source_name = resolve_source_name(blob.parent.name) if resolve_source_name else blob.parent.name
        task_key = (source_name, blob.name)
        if task_key not in task_defines:
            raise ValueError(f"compiled blob has no matching macro task: {blob}")
        if task_key in seen_tasks:
            raise ValueError(f"multiple compiled blobs map to one macro task: {task_key}")
        seen_tasks.add(task_key)
        source = shader_root / f"{source_name}.hlsl"
        source_digest = compute_shader_content_digest(source, shader_root, digest_cache)
        if source_digest is None:
            raise ValueError(f"cannot verify compiled shader source: {source}")
        compile_digest = combine_hashes(global_digest, hash_string(task_defines[task_key]))
        entries[blob.relative_to(cache_dir).as_posix()] = to_hex(
            combine_hashes(source_digest, compile_digest)
        )
    if missing := task_defines.keys() - seen_tasks:
        raise ValueError(f"compiler tasks have no output blobs: {sorted(missing)[:5]}")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=manifest_path.parent, delete=False) as stream:
            temporary_path = Path(stream.name)
            json.dump({"schemaVersion": SCHEMA_VERSION, "entries": entries}, stream, sort_keys=True)
            stream.flush()
            os.fsync(stream.fileno())
        temporary_path.replace(manifest_path)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    return len(entries)
