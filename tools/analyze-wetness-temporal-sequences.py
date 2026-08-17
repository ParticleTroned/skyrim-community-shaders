#!/usr/bin/env python3
"""Summarize motion/event energy in Wetness factorial stereo sequences."""

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


def load_deltas(directory: Path, stride: int) -> np.ndarray:
    frames = sorted(directory.glob("frame_*_stereo.bmp"))
    if len(frames) < 2:
        raise ValueError(f"fewer than two combined stereo BMP frames in {directory}")
    previous: np.ndarray | None = None
    deltas: list[np.ndarray] = []
    for frame in frames:
        with Image.open(frame) as image:
            current = luma(
                np.asarray(image.convert("RGB"), dtype=np.float32)[::stride, ::stride]
            )
        if previous is not None:
            deltas.append(np.abs(current - previous))
        previous = current
    return np.stack(deltas)


def grid_means(values: np.ndarray) -> dict[str, float]:
    rows, columns = 3, 3
    height, width = values.shape
    result: dict[str, float] = {}
    for row in range(rows):
        y0, y1 = height * row // rows, height * (row + 1) // rows
        for column in range(columns):
            x0, x1 = width * column // columns, width * (column + 1) // columns
            result[f"row{row + 1}-column{column + 1}"] = float(
                np.mean(values[y0:y1, x0:x1])
            )
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    run = json.loads(args.manifest.read_text(encoding="utf-8"))
    phases = {phase["name"]: phase for phase in run["phases"]}
    required = ("state-11-before", "state-10", "state-00", "state-01", "state-11-return")
    if any(name not in phases for name in required):
        raise ValueError("Wetness factorial manifest lacks a required state")

    deltas = {
        name: load_deltas(Path(phase["outputDirectory"]), args.stride)
        for name, phase in phases.items()
    }
    width = deltas["state-11-before"].shape[2]
    if width % 2:
        raise ValueError("combined stereo image width is not even")
    midpoint = width // 2

    result: dict[str, object] = {
        "schemaVersion": 1,
        "runId": run["runId"],
        "factorSet": run["factorSet"],
        "disableFirst": run["disableFirst"],
        "acceptedCapture": bool(run["validity"]["accepted"]),
        "analysis": "absolute consecutive-frame luma delta at subsampled native pixels",
        "stride": args.stride,
        "eyes": {},
    }

    for eye, section in (("left", slice(0, midpoint)), ("right", slice(midpoint, width))):
        eye_result: dict[str, object] = {"phases": {}}
        phase_mean_energy: dict[str, float] = {}
        for name in required:
            values = deltas[name][:, :, section]
            per_pixel_median = np.median(values, axis=0)
            per_pixel_p95 = np.percentile(values, 95, axis=0)
            mean_energy = float(np.mean(values))
            phase_mean_energy[name] = mean_energy
            eye_result["phases"][name] = {
                "allFramePixelDeltas": stats(values),
                "perPixelMedianDelta": stats(per_pixel_median),
                "perPixelP95Delta": stats(per_pixel_p95),
                "fractionAbove1Luma": float(np.mean(values > 1.0)),
                "fractionAbove2Luma": float(np.mean(values > 2.0)),
                "fractionAbove5Luma": float(np.mean(values > 5.0)),
                "meanDeltaSpatialGrid": grid_means(np.mean(values, axis=0)),
            }

        baseline = 0.5 * (
            phase_mean_energy["state-11-before"]
            + phase_mean_energy["state-11-return"]
        )
        eye_result["meanDeltaRelativeToBothEnabledBaseline"] = {
            name: phase_mean_energy[name] / baseline
            for name in ("state-10", "state-00", "state-01")
        }
        result["eyes"][eye] = eye_result

    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    if not args.quiet:
        print(rendered)


if __name__ == "__main__":
    main()
