#!/usr/bin/env python3
"""Summarize temporal visual effects and interaction in a two-factor capture."""

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


def load_phase(directory: Path, stride: int) -> tuple[np.ndarray, np.ndarray]:
    frames = sorted(directory.glob("frame_*_stereo.bmp"))
    if not frames:
        raise ValueError(f"no combined stereo BMP frames in {directory}")
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
    return (
        np.median(np.stack(sampled), axis=0).astype(np.float32),
        np.median(np.stack(temporal), axis=0).astype(np.float32),
    )


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
        raise ValueError("visual factorial manifest lacks a required state")

    medians: dict[str, np.ndarray] = {}
    temporal: dict[str, np.ndarray] = {}
    for name, phase in phases.items():
        medians[name], temporal[name] = load_phase(Path(phase["outputDirectory"]), args.stride)

    width = medians["state-11-before"].shape[1]
    if width % 2:
        raise ValueError("combined stereo image width is not even")
    midpoint = width // 2
    mid_tokens = {
        name: (phase["firstCompositorCycleToken"] + phase["lastCompositorCycleToken"]) * 0.5
        for name, phase in phases.items()
    }
    baseline_start = mid_tokens["state-11-before"]
    baseline_end = mid_tokens["state-11-return"]
    if baseline_end <= baseline_start:
        raise ValueError("baseline compositor-token order is invalid")

    result: dict[str, object] = {
        "schemaVersion": 1,
        "runId": run["runId"],
        "factors": run["factors"],
        "disableFirst": run["disableFirst"],
        "acceptedCapture": bool(run["validity"]["accepted"]),
        "analysis": "temporal-median factorial with linear both-enabled drift interpolation",
        "stride": args.stride,
        "eyes": {},
    }

    for eye, section in (("left", slice(0, midpoint)), ("right", slice(midpoint, width))):
        before = luma(medians["state-11-before"][:, section])
        returned = luma(medians["state-11-return"][:, section])
        drift = np.abs(before - returned)
        max_temporal = np.maximum.reduce([temporal[name][:, section] for name in phases])
        stable = (drift <= 2.0) & (max_temporal <= 2.0)
        corrected: dict[str, np.ndarray] = {}
        state_result: dict[str, object] = {}
        for name in ("state-10", "state-00", "state-01"):
            position = float(np.clip(
                (mid_tokens[name] - baseline_start) / (baseline_end - baseline_start), 0.0, 1.0
            ))
            expected = before * (1.0 - position) + returned * position
            residual = luma(medians[name][:, section]) - expected
            state = name.removeprefix("state-")
            corrected[state] = residual
            absolute = np.abs(residual)
            state_result[state] = {
                "baselineInterpolationPosition": position,
                "absoluteDifferenceFromBothEnabled": stats(absolute),
                "meanSignedDifferenceFromBothEnabled": float(np.mean(residual)),
                "stableRegionAbsoluteDifferenceFromBothEnabled": stats(absolute[stable])
                if np.any(stable) else None,
                "temporalFrameDelta": stats(temporal[name][:, section]),
            }

        effects = {
            "factorAWhenBOn": -corrected["01"],
            "factorAWhenBOff": corrected["10"] - corrected["00"],
            "factorBWhenAOn": -corrected["10"],
            "factorBWhenAOff": corrected["01"] - corrected["00"],
            "interaction": -corrected["10"] - corrected["01"] + corrected["00"],
        }
        effect_result: dict[str, object] = {}
        for name, values in effects.items():
            absolute = np.abs(values)
            effect_result[name] = {
                "absoluteLuma": stats(absolute),
                "meanSignedLuma": float(np.mean(values)),
                "stableRegionAbsoluteLuma": stats(absolute[stable]) if np.any(stable) else None,
            }
        result["eyes"][eye] = {
            "enabledReturnDriftAbsolute": stats(drift),
            "stablePixelFraction": float(np.mean(stable)),
            "states": state_result,
            "effects": effect_result,
        }

    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    if not args.quiet:
        print(rendered)


if __name__ == "__main__":
    main()
