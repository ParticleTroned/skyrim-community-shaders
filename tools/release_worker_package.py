"""Checkpoint a fully extracted release AIO without modifying GitHub."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import uuid

from build_provenance import sha256_file
from release_worker_support import checked_path, file_inventory
from shader_compile_checkpoints import atomic_json


def package_inputs(config: dict) -> dict:
    """Bind package reuse to exact core, cache, installer and archive inputs."""
    source = Path(config["repository"])
    return {
        "schema": 1, "settings": config["package"],
        "core": file_inventory(Path(config["core"])),
        "SE": file_inventory(Path(config["output"]) / "SE"),
        "VR": file_inventory(Path(config["output"]) / "VR"),
        "fomodBuilder": sha256_file(source / "tools/build-fomod-package.py"),
        "archiver": sha256_file(Path(config["package"]["sevenZip"])),
    }


def completed_package(marker: Path, archive: Path, inputs: dict) -> bool:
    """Reuse only a previously extracted archive with the same exact inputs."""
    if not marker.is_file() or not archive.is_file():
        return False
    try:
        checked_path(marker)
        checked_path(archive)
        receipt = json.loads(marker.read_bytes())
        return (receipt["inputs"] == inputs and receipt["size"] == archive.stat().st_size
                and receipt["sha256"] == sha256_file(archive))
    except (OSError, ValueError, KeyError, TypeError):
        return False


def finish_package(config: dict, fomod) -> Path:
    """Publish only after canonical validation and an exact extraction comparison."""
    root = Path(config["taskRoot"])
    state = Path(config["state"])
    output = Path(config["output"])
    settings = config["package"]
    archive = checked_path(root / settings["archiveName"])
    marker = state / "package-complete-v2.json"
    inputs = package_inputs(config)
    if completed_package(marker, archive, inputs):
        print(f"Previously extracted AIO verified: {archive}", flush=True)
        return archive
    attempt = state / ("package-" + uuid.uuid4().hex)
    attempt.mkdir()
    stage = attempt / "fomod"
    fomod.stage_package(Path(config["core"]), output / "SE", output / "VR", stage,
                        settings["version"], include_se_ae=True, include_horizon_fix=True)
    expected = file_inventory(stage)
    if file_inventory(stage / "Core") != inputs["core"]:
        raise RuntimeError("Staged core differs from the preserved production core")
    for runtime, directory in (("SE", "ShaderCache-SE-AE"), ("VR", "ShaderCache-VR")):
        if file_inventory(stage / directory) != inputs[runtime]:
            raise RuntimeError(f"Staged {runtime} cache differs from validated inputs")
    pending = attempt / settings["archiveName"]

    def run(arguments, **kwargs):
        subprocess.run(arguments, check=True, **kwargs)

    run([settings["sevenZip"], "a", "-t7z", "-mx=5", "-mmt=8", "-bsp0", str(pending),
         "Core", "ShaderCache-VR", "ShaderCache-SE-AE", "fomod"], cwd=stage)
    run([settings["sevenZip"], "t", str(pending), "-bsp0"])
    extracted = attempt / "extracted"
    extracted.mkdir()
    run([settings["pwsh"], "-NoProfile", "-File", str(Path(config["repository"]) / "tools/cmake.ps1"),
         "-E", "tar", "xf", str(pending)], cwd=extracted)
    fomod.validate_staged_package(extracted, settings["version"], True, True)
    if file_inventory(extracted) != expected or package_inputs(config) != inputs:
        raise RuntimeError("Extracted AIO or build inputs differ from the validated package stage")
    receipt = {"inputs": inputs, "sha256": sha256_file(pending), "size": pending.stat().st_size}
    if archive.exists():
        retained = state / "displaced" / ("archive-" + uuid.uuid4().hex + ".7z")
        retained.parent.mkdir(parents=True, exist_ok=True)
        archive.replace(retained)
    pending.replace(archive)
    atomic_json(marker, receipt)
    print(f"Release AIO fully extracted and verified: {archive}", flush=True)
    return archive
