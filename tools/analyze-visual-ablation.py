#!/usr/bin/env python3
"""Summarize a preset-calibration visual A/B/A run without rewriting captures."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def load_phase(directory: Path, stride: int) -> tuple[np.ndarray, np.ndarray]:
    frames = sorted(directory.glob("frame_*_stereo.bmp"))
    if not frames:
        raise ValueError(f"no combined stereo BMP frames in {directory}")

    sampled: list[np.ndarray] = []
    previous_luma: np.ndarray | None = None
    temporal_deltas: list[np.ndarray] = []
    for frame in frames:
        with Image.open(frame) as image:
            rgb = np.asarray(image.convert("RGB"), dtype=np.uint8)[::stride, ::stride]
        sampled.append(rgb)
        luma = np.rint(
            rgb[..., 0].astype(np.float32) * 0.2126
            + rgb[..., 1].astype(np.float32) * 0.7152
            + rgb[..., 2].astype(np.float32) * 0.0722
        ).astype(np.int16)
        if previous_luma is not None:
            temporal_deltas.append(np.abs(luma - previous_luma))
        previous_luma = luma

    stack = np.stack(sampled)
    median_rgb = np.median(stack, axis=0).astype(np.float32)
    temporal = np.median(np.stack(temporal_deltas), axis=0).astype(np.float32)
    return median_rgb, temporal


def luma(rgb: np.ndarray) -> np.ndarray:
    return (
        rgb[..., 0] * 0.2126
        + rgb[..., 1] * 0.7152
        + rgb[..., 2] * 0.0722
    )


def stats(values: np.ndarray) -> dict[str, float]:
    return {
        "mean": float(np.mean(values)),
        "median": float(np.median(values)),
        "p95": float(np.percentile(values, 95)),
        "p99": float(np.percentile(values, 99)),
    }


def analyze_eye(
    before: np.ndarray,
    off: np.ndarray,
    returned: np.ndarray,
    temporal: tuple[np.ndarray, np.ndarray, np.ndarray],
) -> dict[str, object]:
    reference = (before + returned) * 0.5
    reference_luma = luma(reference)
    off_luma = luma(off)
    before_luma = luma(before)
    returned_luma = luma(returned)
    effect = np.abs(off_luma - reference_luma)
    drift = np.abs(before_luma - returned_luma)
    stable = (drift <= 2.0) & (np.maximum.reduce(temporal) <= 2.0)
    visible = effect >= 2.0
    effect_over_drift = effect > np.maximum(2.0, drift * 2.0)
    signed_rgb = np.mean(off - reference, axis=(0, 1))
    return {
        "lumaEffectAbsolute": stats(effect),
        "enabledReturnDriftAbsolute": stats(drift),
        "offMinusEnabledMeanLuma": float(np.mean(off_luma - reference_luma)),
        "offMinusEnabledMeanRgb": {
            "r": float(signed_rgb[0]),
            "g": float(signed_rgb[1]),
            "b": float(signed_rgb[2]),
        },
        "stablePixelFraction": float(np.mean(stable)),
        "visibleEffectPixelFraction": float(np.mean(visible)),
        "effectAboveTwiceDriftPixelFraction": float(np.mean(effect_over_drift)),
        "stableRegionLumaEffect": stats(effect[stable]) if np.any(stable) else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--stride", type=int, default=4)
    args = parser.parse_args()

    run = json.loads(args.manifest.read_text(encoding="utf-8"))
    phases = {phase["name"]: phase for phase in run["phases"]}
    required = ("baseline-before", "ablated", "baseline-return")
    if any(name not in phases for name in required):
        raise ValueError("manifest does not contain a complete baseline/off/return sequence")

    medians: dict[str, np.ndarray] = {}
    temporal: dict[str, np.ndarray] = {}
    for name in required:
        medians[name], temporal[name] = load_phase(
            Path(phases[name]["outputDirectory"]), args.stride
        )

    width = medians[required[0]].shape[1]
    if width % 2:
        raise ValueError("combined stereo image width is not even")

    result: dict[str, object] = {
        "schemaVersion": 1,
        "runId": run["runId"],
        "feature": run["feature"],
        "acceptedCapture": bool(run["validity"]["accepted"]),
        "analysis": "temporal-median A/B/A at subsampled native pixels",
        "stride": args.stride,
        "eyes": {},
    }
    midpoint = width // 2
    for eye, section in (("left", slice(0, midpoint)), ("right", slice(midpoint, width))):
        result["eyes"][eye] = analyze_eye(
            medians["baseline-before"][:, section],
            medians["ablated"][:, section],
            medians["baseline-return"][:, section],
            tuple(temporal[name][:, section] for name in required),
        )

    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
