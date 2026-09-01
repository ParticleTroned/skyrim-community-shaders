#!/usr/bin/env python3
"""Validate private NVIDIA runtimes and assemble local-only CSX AIOs."""

from __future__ import annotations

import argparse
from contextlib import redirect_stderr, redirect_stdout
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any, Iterable


RUNTIME_DIRECTORY = Path("Shaders/Upscaling/Streamline")
DLSSG_DIRECTORY = Path("Shaders/Upscaling/StreamlineDX12")
NOTICE_NAME = "INTERNAL-NO-REDISTRIBUTION.txt"
SAFE_MANIFEST_NAME = "CSX.PrivateRuntime.json"
PRIVATE_BUILD_NAME = "CSX.PrivateBuild.json"
EXPECTED_LABELS = frozenset({"Vincent-se", "Gogh-se"})
X64_MACHINE = 0x8664

# These exact files come from the archive pinned as Streamline 2.12.0 in
# Streamline-Runtime.cmake. Keeping the list here prevents a private 2.13 DLL
# from crossing into the frame-generation directory during AIO assembly.
OFFICIAL_DLSSG_212 = {
    "nvngx_dlssg.dll": (
        "135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F",
        "310.7.0.0",
    ),
    "sl.common.dll": (
        "C57930EF5A8A3FE9BE85EFDF71A61D8107C1148E8A6AED456464547128F7F4AE",
        "2.12.0.0",
    ),
    "sl.dlss_g.dll": (
        "1FEC3F8FDFC59D78C4445C276C1A0FB798BF251985F348597DC2B44D0C995E52",
        "2.12.0.0",
    ),
    "sl.interposer.dll": (
        "2A79DB6857AE8C75BBD871A9489C48BC6A39F7FCC88B9B02AFD53D0376CBEC66",
        "2.12.0.0",
    ),
    "sl.pcl.dll": (
        "699AB461E64E95189A7FE6A21C79AD237CF56B60EA748CB6C840CD5431BA91D1",
        "2.12.0.0",
    ),
    "sl.reflex.dll": (
        "7E6E4CCC4B561BD449FB0DA90709D9B96B08C3F6F4697362CAAA359E72A58A67",
        "2.12.0.0",
    ),
}


class PrivateAioError(RuntimeError):
    """An intentionally path-sanitized validation failure."""


class VSFixedFileInfo(ctypes.Structure):
    _fields_ = [
        ("dwSignature", wintypes.DWORD),
        ("dwStrucVersion", wintypes.DWORD),
        ("dwFileVersionMS", wintypes.DWORD),
        ("dwFileVersionLS", wintypes.DWORD),
        ("dwProductVersionMS", wintypes.DWORD),
        ("dwProductVersionLS", wintypes.DWORD),
        ("dwFileFlagsMask", wintypes.DWORD),
        ("dwFileFlags", wintypes.DWORD),
        ("dwFileOS", wintypes.DWORD),
        ("dwFileType", wintypes.DWORD),
        ("dwFileSubtype", wintypes.DWORD),
        ("dwFileDateMS", wintypes.DWORD),
        ("dwFileDateLS", wintypes.DWORD),
    ]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def pe_machine(path: Path) -> int:
    try:
        with path.open("rb") as stream:
            if stream.read(2) != b"MZ":
                raise PrivateAioError(f"{path.name}: not a PE image")
            stream.seek(0x3C)
            offset_data = stream.read(4)
            if len(offset_data) != 4:
                raise PrivateAioError(f"{path.name}: truncated PE header")
            pe_offset = struct.unpack("<I", offset_data)[0]
            stream.seek(pe_offset)
            if stream.read(4) != b"PE\0\0":
                raise PrivateAioError(f"{path.name}: invalid PE signature")
            machine_data = stream.read(2)
            if len(machine_data) != 2:
                raise PrivateAioError(f"{path.name}: truncated COFF header")
            return struct.unpack("<H", machine_data)[0]
    except OSError as error:
        raise PrivateAioError(f"{path.name}: could not read PE image") from error


def file_version(path: Path) -> str:
    if os.name != "nt":
        raise PrivateAioError("private NVIDIA runtime validation requires Windows")
    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, wintypes.LPDWORD]
    version.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version.GetFileVersionInfoW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
    ]
    version.GetFileVersionInfoW.restype = wintypes.BOOL
    version.VerQueryValueW.argtypes = [
        wintypes.LPCVOID,
        wintypes.LPCWSTR,
        ctypes.POINTER(wintypes.LPVOID),
        ctypes.POINTER(wintypes.UINT),
    ]
    version.VerQueryValueW.restype = wintypes.BOOL

    ignored = wintypes.DWORD()
    size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    if not size:
        raise PrivateAioError(f"{path.name}: missing Windows file-version resource")
    buffer = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
        raise PrivateAioError(f"{path.name}: unreadable Windows file-version resource")
    value = wintypes.LPVOID()
    value_size = wintypes.UINT()
    if not version.VerQueryValueW(
        buffer, "\\", ctypes.byref(value), ctypes.byref(value_size)
    ):
        raise PrivateAioError(f"{path.name}: missing fixed file-version record")
    fixed = ctypes.cast(value, ctypes.POINTER(VSFixedFileInfo)).contents
    if fixed.dwSignature != 0xFEEF04BD:
        raise PrivateAioError(f"{path.name}: invalid fixed file-version record")
    return ".".join(
        str(item)
        for item in (
            fixed.dwFileVersionMS >> 16,
            fixed.dwFileVersionMS & 0xFFFF,
            fixed.dwFileVersionLS >> 16,
            fixed.dwFileVersionLS & 0xFFFF,
        )
    )


def load_contract(path: Path) -> dict[str, Any]:
    try:
        contract = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PrivateAioError("private runtime contract is unreadable") from error
    if (
        contract.get("schema") != "community-shaders.private-nvidia-runtime"
        or contract.get("schemaVersion") != 1
        or contract.get("architecture") != "x64"
    ):
        raise PrivateAioError("private runtime contract schema is unsupported")
    files = contract.get("files")
    if not isinstance(files, list) or len(files) != 7:
        raise PrivateAioError("private runtime contract must contain seven files")
    names = [entry.get("name") for entry in files if isinstance(entry, dict)]
    if len(names) != 7 or len(set(names)) != 7:
        raise PrivateAioError("private runtime contract filenames are invalid")
    if names.count("nvngx_dlssnr.dll") != 1:
        raise PrivateAioError("private runtime contract must contain one NR runtime")
    return contract


def validate_runtime_file(path: Path, entry: dict[str, Any]) -> None:
    name = entry["name"]
    if path.name != name or not path.is_file():
        raise PrivateAioError(f"{name}: required runtime file is missing")
    if sha256_file(path) != entry.get("sha256"):
        raise PrivateAioError(f"{name}: SHA-256 does not match the pinned identity")
    if pe_machine(path) != X64_MACHINE:
        raise PrivateAioError(f"{name}: runtime is not an x64 PE image")
    if file_version(path) != entry.get("fileVersion"):
        raise PrivateAioError(f"{name}: embedded file version is not allowed")


def source_for(entry: dict[str, Any], runtime_dir: Path, nr_file: Path) -> Path:
    if entry.get("source") == "streamline":
        return runtime_dir / entry["name"]
    if entry.get("source") == "neural-rendering":
        return nr_file
    raise PrivateAioError(f"{entry.get('name', 'runtime')}: invalid source class")


def ensure_descendant(path: Path, root: Path, description: str) -> None:
    try:
        path.resolve().relative_to(root.resolve())
    except (OSError, ValueError) as error:
        raise PrivateAioError(f"{description} must remain inside the local build root") from error


def remove_local_tree(path: Path, local_root: Path) -> None:
    ensure_descendant(path, local_root, "temporary private path")
    if path.exists():
        shutil.rmtree(path)


def write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def safe_runtime_manifest(contract: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema": contract["schema"],
        "schemaVersion": contract["schemaVersion"],
        "architecture": contract["architecture"],
        "files": [
            {
                "name": entry["name"],
                "sha256": entry["sha256"],
                "fileVersion": entry["fileVersion"],
            }
            for entry in contract["files"]
        ],
    }


def stage_runtime(args: argparse.Namespace) -> int:
    contract = load_contract(args.contract)
    runtime_dir = args.runtime_dir.resolve()
    nr_file = args.nr_file.resolve()
    destination = args.destination.resolve()
    local_root = args.local_root.resolve()
    ensure_descendant(destination, local_root, "private runtime destination")
    if nr_file.name != "nvngx_dlssnr.dll":
        raise PrivateAioError("nvngx_dlssnr.dll: selected file has the wrong filename")

    sources: list[tuple[dict[str, Any], Path]] = []
    for entry in contract["files"]:
        source = source_for(entry, runtime_dir, nr_file)
        validate_runtime_file(source, entry)
        sources.append((entry, source))

    destination.parent.mkdir(parents=True, exist_ok=True)
    pending = destination.parent / f".{destination.name}.pending-{os.getpid()}"
    backup = destination.parent / f".{destination.name}.previous-{os.getpid()}"
    remove_local_tree(pending, local_root)
    remove_local_tree(backup, local_root)
    pending_runtime = pending / RUNTIME_DIRECTORY
    pending_runtime.mkdir(parents=True)
    try:
        for entry, source in sources:
            staged = pending_runtime / entry["name"]
            shutil.copy2(source, staged)
            validate_runtime_file(staged, entry)
        shutil.copy2(args.notice, pending / NOTICE_NAME)
        write_json(pending / SAFE_MANIFEST_NAME, safe_runtime_manifest(contract))

        if destination.exists():
            destination.rename(backup)
        pending.rename(destination)
        remove_local_tree(backup, local_root)
    except Exception:
        remove_local_tree(pending, local_root)
        if backup.exists() and not destination.exists():
            backup.rename(destination)
        raise
    print("private NVIDIA runtime: validated and staged seven pinned x64 files")
    return 0


def iter_tree(root: Path) -> Iterable[Path]:
    for path in root.rglob("*"):
        if path.is_symlink():
            raise PrivateAioError("private AIO input contains a filesystem link")
        if path.is_file():
            yield path


def utf16_pattern(value: bytes) -> bytes:
    return b"".join(bytes((byte, 0)) for byte in value)


def machine_secret_needles(source_root: Path) -> list[bytes]:
    values: set[str] = set()
    for key in ("USERPROFILE", "HOMEDRIVE", "HOMEPATH", "USERNAME", "COMPUTERNAME"):
        value = os.environ.get(key, "").strip()
        if len(value) >= 3:
            values.add(value)
    values.add(str(source_root.resolve()))
    values.add(str(source_root.resolve()).replace("\\", "/"))
    result: list[bytes] = []
    for value in values:
        encoded = value.casefold().encode("utf-8", errors="ignore")
        if encoded:
            result.extend((encoded, utf16_pattern(encoded)))
    return result


GENERIC_PRIVATE_PATH_PATTERNS = (
    re.compile(rb"[a-z]:[\\/](?:users|documents and settings)[\\/]", re.I),
    re.compile(
        rb"\\\\[a-z0-9][a-z0-9._-]{0,62}[\\/]"
        rb"[a-z0-9$._-]+(?:[\\/]|$)",
        re.I,
    ),
)

CREDENTIAL_PATTERNS = (
    re.compile(rb"(?:github_pat_|ghp_|glpat-)[a-z0-9_-]{8,}", re.I),
    re.compile(
        rb"(?:password|passwd|token|secret|authorization)\s*"
        rb"(?:=(?!=)|:(?!:))\s*[^\s,;{}\x00-\x1f\x7f-\xff]{8,}",
        re.I,
    ),
    re.compile(
        rb"(?:https?|ssh)://[a-z0-9._~%+-]+"
        rb"(?::[a-z0-9._~%+!$&'()*+,;=-]+)?@",
        re.I,
    ),
)

OPAQUE_THIRD_PARTY_SUFFIXES = frozenset(
    {".dds", ".dll", ".esp", ".nif", ".png", ".ttf", ".wpc"}
)

ASCII_PRINTABLE_RUN = re.compile(rb"[\x20-\x7e]{4,}")
UTF16_PRINTABLE_RUN = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")


def iter_printable_strings(data: bytes) -> Iterable[bytes]:
    """Yield complete ASCII and UTF-16LE strings without crossing binary data."""
    for match in ASCII_PRINTABLE_RUN.finditer(data):
        yield match.group(0)
    for match in UTF16_PRINTABLE_RUN.finditer(data):
        yield match.group(0)[::2]


def scan_file(
    path: Path,
    source_root: Path,
    *,
    strict_path_patterns: bool = True,
    secret_needles: Iterable[bytes] | None = None,
) -> None:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise PrivateAioError(f"privacy scan could not read {path.name}") from error
    folded = data.lower()
    needles = (
        machine_secret_needles(source_root)
        if secret_needles is None
        else secret_needles
    )
    for needle in needles:
        if needle in folded:
            raise PrivateAioError(f"privacy scan rejected machine-specific data in {path.name}")
    for candidate in iter_printable_strings(data):
        for pattern in CREDENTIAL_PATTERNS:
            if pattern.search(candidate):
                raise PrivateAioError(
                    f"privacy scan rejected credential-shaped data in {path.name}"
                )
        if strict_path_patterns:
            for pattern in GENERIC_PRIVATE_PATH_PATTERNS:
                if pattern.search(candidate):
                    raise PrivateAioError(
                        f"privacy scan rejected private path data in {path.name}"
                    )


def scan_staging_tree(root: Path, source_root: Path) -> None:
    """Scan one complete unpacked package using the opaque-binary policy."""
    if not root.is_dir():
        raise PrivateAioError("privacy scan input tree is missing")
    secret_needles = machine_secret_needles(source_root)
    for path in iter_tree(root):
        # Opaque third-party assets can contain random path-shaped bytes or
        # publisher build paths. Exact machine values and credentials remain
        # forbidden; our DLL and all inspectable artifacts stay fully strict.
        opaque_third_party_binary = (
            path.suffix.casefold() in OPAQUE_THIRD_PARTY_SUFFIXES
            and path.name.casefold() != "communityshaders.dll"
        )
        scan_file(
            path,
            source_root,
            strict_path_patterns=not opaque_third_party_binary,
            secret_needles=secret_needles,
        )


def validate_provenance(path: Path, plugin: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PrivateAioError("CSX.BuildManifest.json is missing or invalid") from error
    forbidden_keys = {"compilerPath", "toolchainFile", "remote"}

    def walk(value: Any) -> None:
        if isinstance(value, dict):
            if forbidden_keys.intersection(value):
                raise PrivateAioError("build provenance contains a private path or remote")
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)

    walk(manifest)
    expected = manifest.get("artifact", {}).get("sha256", "").upper()
    if expected != sha256_file(plugin):
        raise PrivateAioError("build provenance does not match CommunityShaders.dll")
    return manifest


def validate_private_payload(root: Path, contract: dict[str, Any]) -> None:
    expected = {entry["name"]: entry for entry in contract["files"]}
    runtime = root / RUNTIME_DIRECTORY
    actual = {path.name for path in runtime.glob("*.dll") if path.is_file()}
    if actual != set(expected):
        raise PrivateAioError("staged private runtime file set is not exact")
    for name, entry in expected.items():
        validate_runtime_file(runtime / name, entry)
    if not (root / NOTICE_NAME).is_file() or not (root / SAFE_MANIFEST_NAME).is_file():
        raise PrivateAioError("staged private runtime notice or manifest is missing")


def validate_dlssg_212(core: Path) -> None:
    directory = core / DLSSG_DIRECTORY
    actual = {path.name for path in directory.glob("*.dll") if path.is_file()}
    if actual != set(OFFICIAL_DLSSG_212):
        raise PrivateAioError("StreamlineDX12 is not the exact official 2.12 file set")
    for name, (expected_hash, expected_version) in OFFICIAL_DLSSG_212.items():
        path = directory / name
        if sha256_file(path) != expected_hash or pe_machine(path) != X64_MACHINE:
            raise PrivateAioError(f"{name}: StreamlineDX12 identity is not official 2.12")
        if file_version(path) != expected_version:
            raise PrivateAioError(f"{name}: StreamlineDX12 version is not official 2.12")


def copy_core_without_pdb(source: Path, destination: Path) -> None:
    list(iter_tree(source))

    def ignore(_directory: str, names: list[str]) -> set[str]:
        return {name for name in names if name.lower().endswith(".pdb")}

    shutil.copytree(source, destination, ignore=ignore)
    if any(path.suffix.casefold() == ".pdb" for path in destination.rglob("*")):
        raise PrivateAioError("private AIO staging retained a PDB")


def import_shader_packager(source_root: Path) -> Any:
    path = source_root / "tools/build-shader-cache.py"
    spec = importlib.util.spec_from_file_location("csx_shader_cache_packager", path)
    if not spec or not spec.loader:
        raise PrivateAioError("shader-cache FOMOD assembler is unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception as error:
        sys.modules.pop(spec.name, None)
        raise PrivateAioError("shader-cache FOMOD assembler could not be loaded") from error
    return module


def create_flat_archive(core: Path, candidate: Path, cmake: Path) -> None:
    result = subprocess.run(
        [str(cmake), "-E", "tar", "cf", str(candidate), "--format=7zip", "--", "."],
        cwd=core,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0 or not candidate.is_file() or candidate.stat().st_size == 0:
        raise PrivateAioError("local-only AIO archive creation failed")


def create_fomod_archive(
    core: Path,
    cache_root: Path,
    workspace: Path,
    candidate: Path,
    source_root: Path,
    cmake: Path,
    label: str,
) -> None:
    packager = import_shader_packager(source_root)
    plugin_version = packager.default_plugin_version(source_root)
    compatibility_tag = packager.validate_csx_plugin_version(plugin_version)
    captured = io.StringIO()
    try:
        with redirect_stdout(captured), redirect_stderr(captured):
            produced = packager.prepare_aio_archive(
                core_root=core,
                cache_root=cache_root,
                workspace=workspace,
                runtime=packager.TARGET_RUNTIME,
                label=label,
                plugin_version=plugin_version,
                compatibility_tag=compatibility_tag,
                cmake=str(cmake),
            )
        scan_staging_tree(workspace / "aio-fomod", source_root)
        produced.replace(candidate)
    except (Exception, SystemExit) as error:
        raise PrivateAioError("shared shader-cache FOMOD assembly failed") from error


def assemble_aio(args: argparse.Namespace) -> int:
    if args.label not in EXPECTED_LABELS:
        raise PrivateAioError("private AIO label must be Vincent-se or Gogh-se")
    contract = load_contract(args.contract)
    source_root = args.source_root.resolve()
    build_root = args.build_root.resolve()
    base_aio = args.base_aio.resolve()
    payload = args.private_runtime.resolve()
    output_root = args.output_root.resolve()
    ensure_descendant(base_aio, build_root, "base AIO")
    ensure_descendant(payload, build_root, "private runtime payload")
    ensure_descendant(output_root, build_root, "private artifact output")
    validate_private_payload(payload, contract)

    plugin = base_aio / "SKSE/Plugins/CommunityShaders.dll"
    provenance = base_aio / "SKSE/Plugins/CSX.BuildManifest.json"
    if not plugin.is_file():
        raise PrivateAioError("base AIO is missing CommunityShaders.dll")
    manifest = validate_provenance(provenance, plugin)
    output_root.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix=".private-aio-", dir=output_root) as temp:
        workspace = Path(temp)
        core = workspace / "core"
        copy_core_without_pdb(base_aio, core)
        private_runtime = core / RUNTIME_DIRECTORY
        if private_runtime.exists():
            shutil.rmtree(private_runtime)
        shutil.copytree(payload / RUNTIME_DIRECTORY, private_runtime)
        shutil.copy2(payload / NOTICE_NAME, core / NOTICE_NAME)
        shutil.copy2(payload / SAFE_MANIFEST_NAME, core / SAFE_MANIFEST_NAME)
        validate_dlssg_212(core)

        private_build = {
            "schema": "community-shaders.private-aio",
            "schemaVersion": 1,
            "label": args.label,
            "redistributable": False,
            "pluginSha256": sha256_file(plugin),
            "buildId": manifest.get("buildId"),
            "shaderCacheIncluded": bool(args.shader_cache_root),
        }
        write_json(core / PRIVATE_BUILD_NAME, private_build)

        scan_staging_tree(core, source_root)

        candidate = workspace / (
            f"CSX_AIO-{args.label}-INTERNAL-NO-REDISTRIBUTION.7z"
        )
        if args.shader_cache_root:
            cache_root = args.shader_cache_root.resolve()
            create_fomod_archive(
                core,
                cache_root,
                workspace,
                candidate,
                source_root,
                args.cmake,
                args.label,
            )
        else:
            create_flat_archive(core, candidate, args.cmake)
        archive = output_root / candidate.name
        staging = output_root / f".{candidate.name}.publishing"
        if staging.exists():
            staging.unlink()
        shutil.copy2(candidate, staging)
        os.replace(staging, archive)
    print(
        "private AIO: assembled a local-only archive"
        + (" with the shared shader-cache FOMOD" if args.shader_cache_root else "")
    )
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    stage = subparsers.add_parser("stage-runtime")
    stage.add_argument("--contract", type=Path, required=True)
    stage.add_argument("--runtime-dir", type=Path, required=True)
    stage.add_argument("--nr-file", type=Path, required=True)
    stage.add_argument("--destination", type=Path, required=True)
    stage.add_argument("--local-root", type=Path, required=True)
    stage.add_argument("--notice", type=Path, required=True)
    stage.set_defaults(handler=stage_runtime)

    assemble = subparsers.add_parser("assemble-aio")
    assemble.add_argument("--contract", type=Path, required=True)
    assemble.add_argument("--source-root", type=Path, required=True)
    assemble.add_argument("--build-root", type=Path, required=True)
    assemble.add_argument("--base-aio", type=Path, required=True)
    assemble.add_argument("--private-runtime", type=Path, required=True)
    assemble.add_argument("--output-root", type=Path, required=True)
    assemble.add_argument("--label", required=True)
    assemble.add_argument("--cmake", type=Path, required=True)
    assemble.add_argument("--shader-cache-root", type=Path)
    assemble.set_defaults(handler=assemble_aio)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.handler(args)
    except PrivateAioError as error:
        print(f"private AIO: {error}", file=sys.stderr)
        return 1
    except Exception:
        print("private AIO: unexpected validation failure", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
