#!/usr/bin/env python3
"""Validate NR shader source assets and optionally hash-match a deployed Data root."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

FEATURE = Path("features/Neural Rendering")
SHADERS = Path("Shaders/Upscaling/NeuralRendering")
RUNTIME_SHADERS = {
    "ColorPipeline.cpp": ("ColorPrepareCS.hlsl", "ColorReconstructCS.hlsl", "ColorMeasureCS.hlsl"),
    "ExposureCapture.cpp": ("ColorExposureCS.hlsl",),
    "Renderer.cpp": ("CopyDepthGuideCS.hlsl", "CopyCompactDepthGuideCS.hlsl"),
}
EXTERNAL_RUNTIME_SHADERS = {"Renderer.cpp": {"Data/Shaders/DLSS5ActorProtectionCS.hlsl"}}
NAMES = ("ColorCommon.hlsli", *(name for names in RUNTIME_SHADERS.values() for name in names))
MANIFEST_VERSION = re.compile(r"^\s*Version\s*=\s*(\d+)-(\d+)-(\d+)\s*$", re.MULTILINE)
REGISTRY_VERSION = re.compile(r'"NeuralRendering"sv,\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}')
RUNTIME_TOKENS = re.compile(
    r'//[^\n]*|/\*.*?\*/|L"(?P<path>(?:\\.|[^"\\])*)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\\r\n])*\'',
    re.DOTALL)


def verify(root: Path, deployed_data: Path | None = None) -> dict:
    mappings = [(FEATURE / "Shaders/Features/NeuralRendering.ini", Path("Shaders/Features/NeuralRendering.ini"))]
    mappings += [(FEATURE / SHADERS / name, SHADERS / name) for name in NAMES]
    errors: list[str] = []
    rows: list[dict] = []
    registry = root / "include/FeatureVersions.h"
    try:
        registry_versions = REGISTRY_VERSION.findall(registry.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError) as error:
        registry_versions = []
        errors.append(f"Unreadable Neural Rendering feature registry: {error}")
    if len(registry_versions) != 1:
        errors.append("Feature registry must declare exactly one Neural Rendering version")
    expected_version = "-".join(registry_versions[0]) if len(registry_versions) == 1 else None
    packaged = {(root / source).resolve() for source, _ in mappings}
    if not (root / FEATURE / "CORE").is_file():
        errors.append("Missing colour CORE marker")
    try:
        providers = [root / "package", *(path for path in (root / "features").iterdir() if path.is_dir())]
    except OSError as error:
        providers = []
        errors.append(f"Unreadable shader source providers: {error}")
    for source, target in mappings:
        path = root / source
        row = {"source": str(source), "destination": str(target), "present": path.is_file()}
        # Feature overlays share installed paths; even identical copies hide ownership drift.
        for provider in providers:
            candidate = provider / target
            if candidate != path and candidate.is_file():
                errors.append(f"Duplicate shader asset provider for {target}: {candidate.relative_to(root)}")
        if not path.is_file():
            errors.append(f"Missing source: {source}")
        else:
            try:
                raw = path.read_bytes()
                row["sourceSha256"] = hashlib.sha256(raw).hexdigest()
                text = raw.decode("utf-8-sig")
            except (OSError, UnicodeError) as error:
                errors.append(f"Unreadable source {source}: {error}")
                rows.append(row)
                continue
            if path.suffix in (".hlsl", ".hlsli"):
                for include in re.findall(r'^\s*#\s*include\s+"([^"]+)"', text, re.M):
                    # Util::CustomInclude resolves every include from Data/Shaders.
                    dependency = (root / FEATURE / "Shaders" / include).resolve()
                    if not dependency.is_file():
                        errors.append(f"Unresolved shader include {include} from {source}")
                    elif dependency not in packaged:
                        # A locally present helper not in the deployment mapping is
                        # not a complete shader package. Every mapped include is
                        # itself scanned and hash-checked, closing dependencies.
                        errors.append(f"Shader include is not packaged: {include} from {source}")
            if path.suffix == ".ini":
                manifest_versions = MANIFEST_VERSION.findall(text)
                if len(manifest_versions) != 1:
                    errors.append("Neural Rendering manifest must declare exactly one valid version")
                elif expected_version is not None and "-".join(manifest_versions[0]) != expected_version:
                    errors.append(f"Neural Rendering manifest version differs from registry {expected_version}")
            if deployed_data is not None:
                deployed = deployed_data / target
                row["deployedPresent"] = deployed.is_file()
                try:
                    row["deployedSha256"] = hashlib.sha256(deployed.read_bytes()).hexdigest() if deployed.is_file() else None
                except OSError as error:
                    row["deployedSha256"] = None
                    errors.append(f"Unreadable deployed asset {target}: {error}")
                row["matches"] = row["deployedSha256"] == row["sourceSha256"]
                if not row["matches"]:
                    errors.append(f"Missing/stale deployed asset: {target}")
        rows.append(row)
    # Check each producer so unrelated paths cannot hide missing or untracked assets.
    actual: set[str] = set()
    for name, shaders in RUNTIME_SHADERS.items():
        expected = {"Data/" + (SHADERS / shader).as_posix() for shader in shaders}
        path = root / "src/Features/Upscaling/NeuralRendering" / name
        if not path.is_file():
            errors.append(f"Missing runtime producer {name}")
            continue
        try:
            # C++ line splicing precedes comment handling; quoted text must stay intact.
            source = re.sub(r'\\\r?\n', '', path.read_text(encoding="utf-8-sig"))
            references = {candidate.replace("\\\\", "/") for match in RUNTIME_TOKENS.finditer(source)
                          if (candidate := match.group("path")) is not None and candidate.endswith(".hlsl")}
        except (OSError, UnicodeError) as error:
            errors.append(f"Unreadable runtime producer {name}: {error}")
            continue
        references -= EXTERNAL_RUNTIME_SHADERS.get(name, set())
        actual.update(references)
        if references != expected:
            errors.append(f"Runtime shader inventory mismatch in {name}: missing={sorted(expected-references)}, extra={sorted(references-expected)}")
    return {"ok": not errors, "assets": rows, "errors": errors, "runtimePaths": sorted(actual),
            "deployedHashChecked": deployed_data is not None, "shaderCompilationChecked": False,
            "note": "Hash parity refers to the supplied Data root. MO2/VFS winner selection must also match the running test profile."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--deployed-data", type=Path)
    args = parser.parse_args()
    result = verify(args.repo_root.resolve(), args.deployed_data.resolve() if args.deployed_data else None)
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
