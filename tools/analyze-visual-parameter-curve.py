#!/usr/bin/env python3
"""Quantify a scalar visual-parameter curve from separate-eye capture phases."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def luma(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def stats(values: np.ndarray) -> dict[str, float]:
    return {
        "mean": float(np.mean(values)),
        "median": float(np.median(values)),
        "p95": float(np.percentile(values, 95)),
        "p99": float(np.percentile(values, 99)),
    }


def spatial_grid(values: np.ndarray, stable: np.ndarray) -> dict[str, object]:
    result: dict[str, object] = {}
    height, width = values.shape
    for row in range(3):
        y0, y1 = height * row // 3, height * (row + 1) // 3
        for column in range(3):
            x0, x1 = width * column // 3, width * (column + 1) // 3
            region = values[y0:y1, x0:x1]
            mask = stable[y0:y1, x0:x1]
            result[f"row{row + 1}-column{column + 1}"] = {
                "stablePixelFraction": float(np.mean(mask)),
                "stableAbsoluteLumaDifference": stats(region[mask])
                if np.any(mask)
                else None,
            }
    return result


def load_eye(directory: Path, eye: str, stride: int) -> tuple[np.ndarray, np.ndarray]:
    frames = sorted(directory.glob(f"frame_*_{eye}.bmp"))
    if not frames:
        raise ValueError(f"no separate-eye BMP frames for {eye} in {directory}")
    sampled: list[np.ndarray] = []
    temporal: list[np.ndarray] = []
    previous: np.ndarray | None = None
    for frame in frames:
        with Image.open(frame) as image:
            rgb = np.asarray(image.convert("RGB"), dtype=np.uint8)[::stride, ::stride]
        sampled.append(rgb)
        current = luma(rgb.astype(np.float32))
        if previous is not None:
            temporal.append(np.abs(current - previous))
        previous = current
    if not temporal:
        temporal.append(np.zeros(sampled[0].shape[:2], dtype=np.float32))
    return (
        np.median(np.stack(sampled), axis=0).astype(np.float32),
        np.median(np.stack(temporal), axis=0).astype(np.float32),
    )


def resolve_phase_directory(manifest: Path, recorded: str) -> Path:
    directory = Path(recorded)
    if directory.is_dir():
        return directory
    relocated = manifest.parent / directory.name
    if relocated.is_dir():
        return relocated
    raise ValueError(
        f"capture directory is missing at recorded or relocated path: {directory}"
    )


def image_occupancy(rgb: np.ndarray) -> dict[str, float]:
    image_luma = luma(rgb)
    chroma = np.max(rgb, axis=2) - np.min(rgb, axis=2)
    return {
        "nearBlackPixelFraction": float(np.mean(image_luma <= 5.0)),
        "nearWhitePixelFraction": float(np.mean(image_luma >= 250.0)),
        "anyChannelClippedPixelFraction": float(np.mean(np.max(rgb, axis=2) >= 254.0)),
        "meanChromaRange": float(np.mean(chroma)),
        "p95ChromaRange": float(np.percentile(chroma, 95)),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    if args.stride < 1:
        raise ValueError("stride must be positive")

    run = json.loads(args.manifest.read_text(encoding="utf-8"))
    if not run.get("validity", {}).get("accepted", False):
        raise ValueError("capture manifest is not accepted")
    phase_records = {phase["name"]: phase for phase in run["phases"]}
    for required in ("baseline-before", "baseline-return"):
        if required not in phase_records:
            raise ValueError(f"manifest lacks {required}")
    value_names = [name for name in phase_records if name.startswith("value-")]
    if not value_names:
        raise ValueError("manifest has no value phases")

    phase_midpoints = {
        name: (phase["firstCompositorCycleToken"] + phase["lastCompositorCycleToken"])
        * 0.5
        for name, phase in phase_records.items()
    }
    baseline_start = phase_midpoints["baseline-before"]
    baseline_end = phase_midpoints["baseline-return"]
    if baseline_end <= baseline_start:
        raise ValueError("baseline compositor-token order is invalid")

    result: dict[str, object] = {
        "schemaVersion": 1,
        "runId": run["runId"],
        "feature": run["feature"],
        "control": run["control"],
        "parameter": run["parameter"],
        "acceptedCapture": True,
        "analysis": (
            "temporal-median separate-eye scalar curve at subsampled native pixels "
            "with linear baseline-drift interpolation"
        ),
        "stride": args.stride,
        "eyes": {},
    }

    for eye in ("left", "right"):
        medians: dict[str, np.ndarray] = {}
        temporal: dict[str, np.ndarray] = {}
        for name, phase in phase_records.items():
            medians[name], temporal[name] = load_eye(
                resolve_phase_directory(args.manifest, phase["outputDirectory"]),
                eye,
                args.stride,
            )
        before = medians["baseline-before"]
        returned = medians["baseline-return"]
        before_luma = luma(before)
        returned_luma = luma(returned)
        drift = np.abs(before_luma - returned_luma)
        max_temporal = np.maximum.reduce(list(temporal.values()))
        stable = (drift <= 2.0) & (max_temporal <= 2.0)
        eye_result: dict[str, object] = {
            "baselineReturnDriftAbsoluteLuma": stats(drift),
            "stablePixelFraction": float(np.mean(stable)),
            "baselineBeforeOccupancy": image_occupancy(before),
            "baselineReturnOccupancy": image_occupancy(returned),
            "values": [],
        }
        for name in sorted(value_names):
            phase = phase_records[name]
            position = (phase_midpoints[name] - baseline_start) / (
                baseline_end - baseline_start
            )
            position = float(np.clip(position, 0.0, 1.0))
            interpolated_rgb = before * (1.0 - position) + returned * position
            delta_rgb = medians[name] - interpolated_rgb
            delta_luma = luma(delta_rgb)
            absolute = np.abs(delta_luma)
            above_drift = absolute > np.maximum(2.0, drift * 2.0)
            value_result = {
                "phase": name,
                "effectiveValue": phase["effectiveValue"],
                "baselineInterpolationPosition": position,
                "absoluteLumaDifference": stats(absolute),
                "meanSignedLumaDifference": float(np.mean(delta_luma)),
                "meanSignedRgbDifference": {
                    channel: float(np.mean(delta_rgb[..., index]))
                    for index, channel in enumerate(("red", "green", "blue"))
                },
                "differenceAboveTwiceDriftPixelFraction": float(
                    np.mean(above_drift)
                ),
                "stableRegionAbsoluteLumaDifference": stats(absolute[stable])
                if np.any(stable)
                else None,
                "stableSpatialGrid": spatial_grid(absolute, stable),
                "temporalFrameDelta": stats(temporal[name]),
                "occupancy": image_occupancy(medians[name]),
            }
            eye_result["values"].append(value_result)
        result["eyes"][eye] = eye_result

    rendered = json.dumps(result, indent=2)
    output = args.output or args.manifest.with_name(
        "visual-parameter-curve-analysis.json"
    )
    output.write_text(rendered + "\n", encoding="utf-8")
    if not args.quiet:
        print(rendered)


if __name__ == "__main__":
    main()
