#!/usr/bin/env python3
"""Generate and finalize CSX build-provenance manifests.

The embedded Build ID identifies behavior-affecting build inputs.  The final
sidecar additionally records the SHA-256 of the linked DLL, which is the
authoritative identity for captured evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable


SCHEMA = "community-shaders.build-provenance"
SCHEMA_VERSION = 1
BUILD_ID_ALGORITHM = "sha256-canonical-json-v1"
SHADER_ABI_SCHEMA_VERSION = 2
DEFAULT_SHADER_CONTRACT_FILES = [
    "config/shader-cache-abi.json",
    "config/shader-compatibility-variants.json",
]


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalized_file_digest(path: Path) -> str:
    return sha256_bytes(path.read_bytes().replace(b"\r\n", b"\n"))


def tree_digest(root: Path, paths: Iterable[Path] | None = None) -> str:
    entries: list[dict[str, str]] = []
    candidates = paths if paths is not None else (
        path for path in root.rglob("*") if path.is_file()
    )
    for path in sorted((Path(path) for path in candidates), key=lambda item: item.as_posix().lower()):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        entries.append({"path": relative, "sha256": normalized_file_digest(path)})
    return sha256_bytes(canonical_bytes(entries))


def git(source_dir: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(source_dir), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed in {source_dir}: "
            f"{result.stderr.decode('utf-8', errors='replace').strip()}"
        )
    return result.stdout.decode("utf-8", errors="replace").strip()


def working_tree_state(repo: Path) -> tuple[bool, str]:
    status = git(repo, "status", "--porcelain=v1", "--untracked-files=all")
    if not status:
        return False, sha256_bytes(b"")

    diff = subprocess.run(
        ["git", "-C", str(repo), "diff", "--binary", "HEAD", "--"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if diff.returncode != 0:
        raise RuntimeError(diff.stderr.decode("utf-8", errors="replace"))

    untracked_raw = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "--others", "--exclude-standard", "-z"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if untracked_raw.returncode != 0:
        raise RuntimeError(untracked_raw.stderr.decode("utf-8", errors="replace"))

    untracked: list[dict[str, str]] = []
    for raw_path in untracked_raw.stdout.split(b"\0"):
        if not raw_path:
            continue
        relative = raw_path.decode("utf-8", errors="surrogateescape")
        path = repo / relative
        if path.is_file():
            untracked.append({"path": Path(relative).as_posix(), "sha256": sha256_file(path)})

    dirty_payload = {
        "diffSha256": sha256_bytes(diff.stdout),
        "status": status.splitlines(),
        "untracked": sorted(untracked, key=lambda item: item["path"].lower()),
    }
    return True, sha256_bytes(canonical_bytes(dirty_payload))


def read_submodules(source_dir: Path) -> list[dict[str, Any]]:
    gitmodules = source_dir / ".gitmodules"
    if not gitmodules.exists():
        return []

    output = subprocess.run(
        ["git", "config", "--file", str(gitmodules), "--get-regexp", r"^submodule\..*\.path$"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if output.returncode not in (0, 1):
        raise RuntimeError(output.stderr.decode("utf-8", errors="replace"))

    submodules: list[dict[str, Any]] = []
    for line in output.stdout.decode("utf-8", errors="replace").splitlines():
        _, relative = line.split(maxsplit=1)
        pinned_fields = git(source_dir, "ls-files", "-s", "--", relative).split()
        pinned = pinned_fields[1] if len(pinned_fields) >= 2 and pinned_fields[0] == "160000" else None
        checkout = source_dir / relative
        actual = None
        dirty = False
        dirty_digest = sha256_bytes(b"")
        if checkout.exists():
            actual = git(checkout, "rev-parse", "HEAD", check=False) or None
            if actual:
                dirty, dirty_digest = working_tree_state(checkout)
        submodules.append(
            {
                "path": Path(relative).as_posix(),
                "pinnedCommit": pinned,
                "checkedOutCommit": actual,
                "matchesPinned": bool(pinned and actual and pinned == actual),
                "dirty": dirty,
                "dirtyDigest": dirty_digest if dirty else None,
            }
        )
    return sorted(submodules, key=lambda item: item["path"].lower())


def explain_unclean_provenance(
    source_dir: Path,
    source_dirty: bool,
    submodules: list[dict[str, Any]],
) -> str:
    """Return actionable, relative-path diagnostics for a rejected clean build."""
    lines = ["clean provenance required, but Git state is not reproducible"]
    if source_dirty:
        status = git(
            source_dir,
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
            "--ignore-submodules=none",
        )
        lines.append("source status:")
        lines.extend(f"  {line}" for line in status.splitlines())

    for item in submodules:
        if not item["dirty"] and item["matchesPinned"]:
            continue
        lines.append(
            "submodule "
            f"{item['path']}: pinned={item['pinnedCommit'] or 'missing'} "
            f"checked-out={item['checkedOutCommit'] or 'missing'} "
            f"dirty={str(item['dirty']).lower()}"
        )
        checkout = source_dir / item["path"]
        if item["dirty"] and checkout.exists():
            status = git(
                checkout,
                "status",
                "--porcelain=v1",
                "--untracked-files=all",
                "--ignore-submodules=none",
                check=False,
            )
            lines.extend(f"  {line}" for line in status.splitlines())

    return "\n".join(lines)


def parse_key_values(values: list[str]) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"expected NAME=VALUE, got {value!r}")
        key, item_value = value.split("=", 1)
        if not key:
            raise ValueError(f"empty key in {value!r}")
        parsed[key] = item_value
    return dict(sorted(parsed.items()))


def vcpkg_identity(source_dir: Path, overlay_dir: Path | None) -> dict[str, Any]:
    manifest_path = source_dir / "vcpkg.json"
    identity: dict[str, Any] = {
        "manifestSha256": sha256_file(manifest_path) if manifest_path.exists() else None,
        "builtinBaseline": None,
        "overlayTreeSha256": None,
    }
    if manifest_path.exists():
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        identity["builtinBaseline"] = data.get("builtin-baseline")
    if overlay_dir and overlay_dir.exists():
        identity["overlayTreeSha256"] = tree_digest(overlay_dir)
    return identity


def shader_contract_identity(source_dir: Path, relative_paths: list[str]) -> dict[str, Any]:
    files = [source_dir / path for path in relative_paths]
    missing = [str(path) for path in files if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"shader ABI contract file(s) missing: {', '.join(missing)}")
    entries = [
        {
            "path": Path(relative).as_posix(),
            "sha256": normalized_file_digest(source_dir / relative),
        }
        for relative in sorted(relative_paths, key=str.lower)
    ]
    return {
        "schemaVersion": SHADER_ABI_SCHEMA_VERSION,
        "managedCompiler": "D3DCompileFromFile",
        "shaderModels": ["vs_5_0", "ps_5_0", "cs_5_0"],
        "contentDigest": "xxh3-128-crlf-normalized",
        "manifestSchemaVersion": 1,
        "contractFiles": entries,
        "contractTreeSha256": sha256_bytes(canonical_bytes(entries)),
    }


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def write_if_changed(path: Path, content: str) -> None:
    encoded = content.encode("utf-8")
    if path.exists() and path.read_bytes() == encoded:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)


def build_manifest(args: argparse.Namespace) -> dict[str, Any]:
    source_dir = args.source_dir.resolve()
    source_dirty, source_dirty_digest = working_tree_state(source_dir)
    submodules = read_submodules(source_dir)
    dependencies_dirty = any(
        item["dirty"] or not item["matchesPinned"] for item in submodules
    )
    if args.require_clean and (source_dirty or dependencies_dirty):
        raise RuntimeError(
            explain_unclean_provenance(source_dir, source_dirty, submodules)
        )

    source_commit = git(source_dir, "rev-parse", "HEAD")
    source_describe = git(source_dir, "describe", "--tags", "--always", "--dirty")
    remote = git(source_dir, "remote", "get-url", "origin", check=False) or None
    build_options = parse_key_values(args.build_option)
    shader_contract_files = args.shader_contract_file or DEFAULT_SHADER_CONTRACT_FILES
    shader_contract = shader_contract_identity(source_dir, shader_contract_files)
    shader_abi_id = sha256_bytes(canonical_bytes(shader_contract))

    toolchain_file = Path(args.toolchain_file).resolve() if args.toolchain_file else None
    toolchain_identity = {
        "compilerId": args.compiler_id,
        "compilerVersion": args.compiler_version,
        "compilerSha256": sha256_file(Path(args.compiler_path))
        if args.compiler_path and Path(args.compiler_path).is_file()
        else None,
        "generator": args.generator,
        "generatorPlatform": args.generator_platform or None,
        "generatorToolset": args.generator_toolset or None,
        "windowsSdkVersion": args.windows_sdk_version or None,
        "cmakeVersion": args.cmake_version,
        "targetTriplet": args.target_triplet or None,
        "toolchainFileSha256": sha256_file(toolchain_file) if toolchain_file and toolchain_file.is_file() else None,
    }
    identity = {
        "source": {
            "commit": source_commit,
            "dirty": source_dirty,
            "dirtyDigest": source_dirty_digest if source_dirty else None,
        },
        "dependencies": {
            "submodules": submodules,
            "vcpkg": vcpkg_identity(source_dir, args.overlay_dir.resolve() if args.overlay_dir else None),
        },
        "toolchain": toolchain_identity,
        "build": {
            "configuration": args.configuration,
            "runtime": args.runtime,
            "pluginVersion": args.plugin_version,
            "options": build_options,
        },
        "shaderCache": {
            "abiId": shader_abi_id,
            "contract": shader_contract,
        },
    }
    build_id = sha256_bytes(canonical_bytes(identity))
    return {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "buildIdAlgorithm": BUILD_ID_ALGORITHM,
        "buildId": build_id,
        "identity": identity,
        "sourceDisplay": {
            "describe": source_describe,
            "remote": remote,
        },
        "environment": {
            "compilerPath": args.compiler_path or None,
            "toolchainFile": str(toolchain_file) if toolchain_file else None,
        },
        "artifact": {
            "fileName": args.artifact_name,
            "sha256": None,
            "sizeBytes": None,
        },
    }


def render_header(manifest: dict[str, Any]) -> str:
    identity = manifest["identity"]
    source = identity["source"]
    shader_cache = identity["shaderCache"]
    embedded_manifest = json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return f"""#pragma once

#include <string_view>

namespace CSX::EmbeddedBuildProvenance
{{
    using namespace std::literals;
    inline constexpr auto BUILD_ID = {cpp_string(manifest['buildId'])}sv;
    inline constexpr auto BUILD_ID_SHORT = {cpp_string(manifest['buildId'][:12])}sv;
    inline constexpr auto SHADER_CACHE_ABI_ID = {cpp_string(shader_cache['abiId'])}sv;
    inline constexpr auto SOURCE_COMMIT = {cpp_string(source['commit'])}sv;
    inline constexpr auto SOURCE_DESCRIBE = {cpp_string(manifest['sourceDisplay']['describe'])}sv;
    inline constexpr auto CONFIGURATION = {cpp_string(identity['build']['configuration'])}sv;
    inline constexpr bool SOURCE_DIRTY = {str(source['dirty']).lower()};
    inline constexpr auto MANIFEST_JSON = R"CSXPROV({embedded_manifest})CSXPROV"sv;
}}
"""


def generate(args: argparse.Namespace) -> int:
    manifest = build_manifest(args)
    base_manifest = json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    write_if_changed(args.output_manifest, base_manifest)
    write_if_changed(args.output_header, render_header(manifest))
    print(manifest["buildId"])
    return 0


def validate_manifest_identity(manifest: dict[str, Any]) -> None:
    if manifest.get("schema") != SCHEMA or manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError("unsupported build-provenance manifest schema")
    expected = sha256_bytes(canonical_bytes(manifest["identity"]))
    if manifest.get("buildId") != expected:
        raise ValueError(f"manifest Build ID mismatch: expected {expected}, got {manifest.get('buildId')}")
    shader_contract = manifest["identity"]["shaderCache"]["contract"]
    expected_shader_abi = sha256_bytes(canonical_bytes(shader_contract))
    if manifest["identity"]["shaderCache"].get("abiId") != expected_shader_abi:
        raise ValueError("manifest shader ABI ID mismatch")


def finalize(args: argparse.Namespace) -> int:
    manifest = json.loads(args.base_manifest.read_text(encoding="utf-8"))
    validate_manifest_identity(manifest)
    manifest["artifact"] = {
        "fileName": args.artifact.name,
        "sha256": sha256_file(args.artifact),
        "sizeBytes": args.artifact.stat().st_size,
    }
    write_if_changed(
        args.output_manifest,
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
    )
    print(manifest["artifact"]["sha256"])
    return 0


def verify(args: argparse.Namespace) -> int:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    validate_manifest_identity(manifest)
    artifact = args.artifact
    if artifact:
        actual = sha256_file(artifact)
        expected = manifest.get("artifact", {}).get("sha256")
        if actual != expected:
            raise ValueError(f"artifact SHA-256 mismatch: expected {expected}, got {actual}")
    print(json.dumps({"buildId": manifest["buildId"], "verified": True}, sort_keys=True))
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    generate_parser = subparsers.add_parser("generate", help="generate the embedded header and base manifest")
    generate_parser.add_argument("--source-dir", type=Path, required=True)
    generate_parser.add_argument("--output-header", type=Path, required=True)
    generate_parser.add_argument("--output-manifest", type=Path, required=True)
    generate_parser.add_argument("--runtime", required=True)
    generate_parser.add_argument("--plugin-version", required=True)
    generate_parser.add_argument("--configuration", required=True)
    generate_parser.add_argument("--artifact-name", default="CommunityShaders.dll")
    generate_parser.add_argument("--compiler-id", required=True)
    generate_parser.add_argument("--compiler-version", required=True)
    generate_parser.add_argument("--compiler-path", default="")
    generate_parser.add_argument("--generator", required=True)
    generate_parser.add_argument("--generator-platform", default="")
    generate_parser.add_argument("--generator-toolset", default="")
    generate_parser.add_argument("--windows-sdk-version", default="")
    generate_parser.add_argument("--cmake-version", required=True)
    generate_parser.add_argument("--target-triplet", default="")
    generate_parser.add_argument("--toolchain-file", default="")
    generate_parser.add_argument("--overlay-dir", type=Path)
    generate_parser.add_argument("--build-option", action="append", default=[])
    generate_parser.add_argument("--shader-contract-file", action="append", default=[])
    generate_parser.add_argument("--require-clean", action="store_true")
    generate_parser.set_defaults(handler=generate)

    finalize_parser = subparsers.add_parser("finalize", help="bind a base manifest to a linked artifact")
    finalize_parser.add_argument("--base-manifest", type=Path, required=True)
    finalize_parser.add_argument("--artifact", type=Path, required=True)
    finalize_parser.add_argument("--output-manifest", type=Path, required=True)
    finalize_parser.set_defaults(handler=finalize)

    verify_parser = subparsers.add_parser("verify", help="verify a manifest and optional artifact")
    verify_parser.add_argument("--manifest", type=Path, required=True)
    verify_parser.add_argument("--artifact", type=Path)
    verify_parser.set_defaults(handler=verify)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.handler(args)
    except Exception as error:
        print(f"build provenance: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
