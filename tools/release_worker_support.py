"""Path, input and process ownership for the resumable release builder."""

from __future__ import annotations

import contextlib
import ctypes
import os
from pathlib import Path
import re

from build_provenance import sha256_file


class AlreadyRunning(OSError):
    """Another live worker owns a required writable directory."""


def checked_path(path: Path) -> Path:
    """Reject reparse points before resolving a configured or generated path."""
    path = Path(os.path.abspath(path))
    for part in (*reversed(path.parents), path):
        if part.is_symlink() or part.is_junction():
            raise ValueError(f"Worker paths must not traverse links: {part}")
    return path.resolve()


def file_inventory(root: Path) -> dict:
    """Fingerprint exact bytes and names, including non-shader package inputs."""
    root = checked_path(root)
    result = {}
    for path in sorted(root.rglob("*")):
        if "__pycache__" in path.relative_to(root).parts:
            continue
        checked_path(path)
        if path.is_file():
            result[path.relative_to(root).as_posix()] = {"sha256": sha256_file(path), "size": path.stat().st_size}
    return result


def validate_config(config: dict) -> dict:
    """Validate all paths and bounds before creating directories or locking."""
    required = {"repository", "taskRoot", "workspace", "output", "state", "core", "fxc",
                "sourceCommit", "pluginVersion", "jobs"}
    optional = {"package", "python", "pwsh", "taskName"}
    if not isinstance(config, dict) or not required <= config.keys() or config.keys() - required - optional:
        raise ValueError("Invalid worker configuration fields; see the release worker guide")
    if type(config["jobs"]) is not int or not 1 <= config["jobs"] <= 64:
        raise ValueError("jobs must be an integer in [1, 64]")
    path_values = [config[name] for name in ("repository", "taskRoot", "workspace", "output", "state", "core", "fxc")]
    path_values.extend(config[name] for name in ("python", "pwsh") if name in config)
    if not all(isinstance(value, str) and Path(value).is_absolute() for value in path_values):
        raise ValueError("Worker paths must be absolute strings")
    if not isinstance(config["sourceCommit"], str) or not re.fullmatch(r"[0-9a-f]{40}", config["sourceCommit"]):
        raise ValueError("sourceCommit must be a complete Git commit ID")
    if not isinstance(config["pluginVersion"], str) or not re.fullmatch(r"CSX \d+\.\d+\.\d+-VR", config["pluginVersion"]):
        raise ValueError("pluginVersion must use the production CSX version format")
    paths = {name: checked_path(Path(config[name])) for name in
             ("repository", "taskRoot", "workspace", "output", "state", "core", "fxc")}
    root = paths["taskRoot"]
    for name in ("workspace", "output", "state", "core"):
        if not paths[name].is_relative_to(root) or paths[name] == root:
            raise ValueError(f"{name} must be strictly inside taskRoot")
    mutable = [paths[name] for name in ("workspace", "output", "state")]
    for index, left in enumerate(mutable):
        for right in (*mutable[index + 1:], paths["core"], paths["repository"]):
            if left.is_relative_to(right) or right.is_relative_to(left):
                raise ValueError(f"Worker input and writable directories overlap: {left}, {right}")
    for name in ("core", "repository"):
        if not paths[name].is_dir():
            raise ValueError(f"Missing {name} directory")
    if not paths["fxc"].is_file():
        raise ValueError("Missing FXC executable")
    result = {**config, **{key: str(value) for key, value in paths.items()}}
    if "package" in config:
        package = config["package"]
        if not isinstance(package, dict) or set(package) != {"version", "sevenZip", "pwsh", "archiveName"}:
            raise ValueError("Invalid package settings")
        version = config["pluginVersion"].removeprefix("CSX ").removesuffix("-VR")
        if package["version"] != "csx" + version:
            raise ValueError("Package and plugin versions differ")
        expected_name = f"CSX_AIO-{version}-Production-ShaderCache-FOMOD.7z"
        if package["archiveName"] != expected_name:
            raise ValueError("Unexpected release archive name")
        result["package"] = dict(package)
        for name in ("sevenZip", "pwsh"):
            if not isinstance(package[name], str) or not Path(package[name]).is_absolute():
                raise ValueError("Package executable paths must be absolute")
            resolved = checked_path(Path(package[name]))
            if not resolved.is_file():
                raise ValueError(f"Missing package executable: {name}")
            result["package"][name] = str(resolved)
    for name in ("python", "pwsh"):
        if name in config and not checked_path(Path(config[name])).is_file():
            raise ValueError(f"Missing launcher executable: {name}")
        if name in config:
            result[name] = str(checked_path(Path(config[name])))
    if "taskName" in config and not re.fullmatch(r"CSX-Release-[A-Za-z0-9_.-]{1,100}", config["taskName"]):
        raise ValueError("taskName must be a simple CSX-Release-* name")
    return result


@contextlib.contextmanager
def exclusive_owner(path: Path):
    """Hold an OS byte lock; process exit releases it even after termination."""
    import msvcrt
    path = checked_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        if stream.tell() == 0:
            stream.write(b"\0")
            stream.flush()
        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except PermissionError as error:
            raise AlreadyRunning("Another worker owns a required writable directory") from error
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def own_process_job():
    """Keep compiler children in a Windows kill-on-owner-exit job."""
    from ctypes import wintypes as w

    class Basic(ctypes.Structure):
        _fields_ = [("processTime", ctypes.c_int64), ("jobTime", ctypes.c_int64),
                    ("flags", w.DWORD), ("minimum", ctypes.c_size_t),
                    ("maximum", ctypes.c_size_t), ("processes", w.DWORD),
                    ("affinity", ctypes.c_size_t), ("priority", w.DWORD),
                    ("scheduling", w.DWORD)]

    class Counters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint64) for name in
                    ("reads", "writes", "other", "readBytes", "writeBytes", "otherBytes")]

    class Extended(ctypes.Structure):
        _fields_ = [("basic", Basic), ("io", Counters),
                    ("processMemory", ctypes.c_size_t), ("jobMemory", ctypes.c_size_t),
                    ("peakProcess", ctypes.c_size_t), ("peakJob", ctypes.c_size_t)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]
    kernel.CreateJobObjectW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL
    kernel.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
    kernel.SetInformationJobObject.restype = w.BOOL
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
    kernel.AssignProcessToJobObject.restype = w.BOOL
    handle = kernel.CreateJobObjectW(None, None)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        limits = Extended()
        limits.basic.flags = 0x2000
        if not kernel.SetInformationJobObject(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not kernel.AssignProcessToJobObject(handle, kernel.GetCurrentProcess()):
            raise ctypes.WinError(ctypes.get_last_error())
    except BaseException:
        kernel.CloseHandle(handle)
        raise
    # Closing this handle would terminate this process; retain it until exit.
    return handle
