#!/usr/bin/env python3
"""Validate NR colour source assets and optionally hash-match a deployed Data root."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

FEATURE = Path("features/Neural Rendering Colour")
SHADERS = Path("Shaders/Upscaling/NeuralRendering")
NAMES = ("ColorCommon.hlsli", "ColorPrepareCS.hlsl", "ColorReconstructCS.hlsl",
         "ColorMeasureCS.hlsl", "ColorExposureCS.hlsl")


def verify(root: Path, deployed_data: Path | None = None) -> dict:
    mappings = [(FEATURE / "Shaders/Features/NeuralColor.ini", Path("Shaders/Features/NeuralColor.ini"))]
    mappings += [(FEATURE / SHADERS / name, SHADERS / name) for name in NAMES]
    errors: list[str] = []
    rows: list[dict] = []
    if not (root / FEATURE / "CORE").is_file():
        errors.append("Missing colour CORE marker")
    for source, target in mappings:
        path = root / source
        row = {"source": str(source), "destination": str(target), "present": path.is_file()}
        if not path.is_file():
            errors.append(f"Missing source: {source}")
        else:
            raw = path.read_bytes()
            row["sourceSha256"] = hashlib.sha256(raw).hexdigest()
            text = raw.decode("utf-8-sig")
            if path.suffix in (".hlsl", ".hlsli"):
                for include in re.findall(r'^\s*#\s*include\s+"([^"]+)"', text, re.M):
                    if not (path.parent / include).is_file():
                        errors.append(f"Unresolved shader include {include} from {source}")
            if path.suffix == ".ini" and "Version = 1-1-0" not in text:
                errors.append("NeuralColor manifest version is not 1-1-0")
            if deployed_data is not None:
                deployed = deployed_data / target
                row["deployedPresent"] = deployed.is_file()
                row["deployedSha256"] = hashlib.sha256(deployed.read_bytes()).hexdigest() if deployed.is_file() else None
                row["matches"] = row["deployedSha256"] == row["sourceSha256"]
                if not row["matches"]:
                    errors.append(f"Missing/stale deployed asset: {target}")
        rows.append(row)
    # Verify all runtime compile paths have an asset in the known feature mapping.
    expected = {"Data/" + str(SHADERS / n).replace("\\", "/") for n in NAMES if n.endswith(".hlsl")}
    actual: set[str] = set()
    for name in ("ColorPipeline.cpp", "ExposureCapture.cpp"):
        path = root / "src/Features/Upscaling/NeuralRendering" / name
        if not path.is_file():
            errors.append(f"Missing runtime producer {name}")
            continue
        actual.update(re.findall(r'L"(Data/Shaders/[^"\n]+\.hlsl)"', path.read_text(encoding="utf-8-sig")))
    if actual != expected:
        errors.append(f"Runtime shader inventory mismatch: missing={sorted(expected-actual)}, extra={sorted(actual-expected)}")
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
