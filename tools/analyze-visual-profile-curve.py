#!/usr/bin/env python3
"""Summarize temporal visual differences in a typed quality-profile curve."""

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
    rows, columns = 3, 3
    height, width = values.shape
    result: dict[str, object] = {}
    for row in range(rows):
        y0, y1 = height * row // rows, height * (row + 1) // rows
        for column in range(columns):
            x0, x1 = width * column // columns, width * (column + 1) // columns
            region_values = values[y0:y1, x0:x1]
            region_stable = stable[y0:y1, x0:x1]
            key = f"row{row + 1}-column{column + 1}"
            result[key] = {
                "stablePixelFraction": float(np.mean(region_stable)),
                "stableAbsoluteLumaDifference": stats(region_values[region_stable])
                if np.any(region_stable)
                else None,
            }
    return result


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
    phase_records = {phase["name"]: phase for phase in run["phases"]}
    for required in ("baseline-before", "baseline-return"):
        if required not in phase_records:
            raise ValueError(f"manifest lacks {required}")

    medians: dict[str, np.ndarray] = {}
    temporal: dict[str, np.ndarray] = {}
    for name, phase in phase_records.items():
        medians[name], temporal[name] = load_phase(
            Path(phase["outputDirectory"]), args.stride
        )

    width = medians["baseline-before"].shape[1]
    if width % 2:
        raise ValueError("combined stereo image width is not even")
    midpoint = width // 2

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
        "schemaVersion": 2,
        "runId": run["runId"],
        "feature": run["feature"],
        "acceptedCapture": bool(run["validity"]["accepted"]),
        "analysis": (
            "temporal-median profile curve at subsampled native pixels with "
            "linear baseline-drift interpolation"
        ),
        "stride": args.stride,
        "eyes": {},
    }

    profile_names = [name for name in phase_records if name.startswith("profile-")]
    for eye, section in (("left", slice(0, midpoint)), ("right", slice(midpoint, width))):
        before = medians["baseline-before"][:, section]
        returned = medians["baseline-return"][:, section]
        reference = (before + returned) * 0.5
        reference_luma = luma(reference)
        before_luma = luma(before)
        returned_luma = luma(returned)
        drift = np.abs(luma(before) - luma(returned))
        max_temporal = np.maximum.reduce(
            [temporal[name][:, section] for name in phase_records]
        )
        stable = (drift <= 2.0) & (max_temporal <= 2.0)
        eye_result: dict[str, object] = {
            "enabledReturnDriftAbsolute": stats(drift),
            "stablePixelFraction": float(np.mean(stable)),
            "profiles": {},
            "correctedPairwiseProfileDifferences": {},
        }
        corrected_profiles: dict[str, np.ndarray] = {}
        for name in profile_names:
            profile = phase_records[name]["effectiveProfile"]
            profile_luma = luma(medians[name][:, section])
            effect = np.abs(profile_luma - reference_luma)
            effect_over_drift = effect > np.maximum(2.0, drift * 2.0)
            phase_position = (phase_midpoints[name] - baseline_start) / (
                baseline_end - baseline_start
            )
            phase_position = float(np.clip(phase_position, 0.0, 1.0))
            interpolated_baseline_luma = (
                before_luma * (1.0 - phase_position)
                + returned_luma * phase_position
            )
            corrected = profile_luma - interpolated_baseline_luma
            corrected_profiles[profile] = corrected
            corrected_absolute = np.abs(corrected)
            eye_result["profiles"][profile] = {
                "baselineInterpolationPosition": phase_position,
                "lumaDifferenceFromBaselineProfile": stats(effect),
                "profileMinusBaselineMeanLuma": float(
                    np.mean(profile_luma - reference_luma)
                ),
                "differenceAboveTwiceDriftPixelFraction": float(
                    np.mean(effect_over_drift)
                ),
                "stableRegionLumaDifference": stats(effect[stable])
                if np.any(stable)
                else None,
                "driftCorrectedLumaDifference": stats(corrected_absolute),
                "driftCorrectedMeanLuma": float(np.mean(corrected)),
                "stableRegionDriftCorrectedLumaDifference": stats(
                    corrected_absolute[stable]
                )
                if np.any(stable)
                else None,
                "temporalFrameDelta": stats(temporal[name][:, section]),
            }
        profile_order = list(corrected_profiles)
        for left_index, left_profile in enumerate(profile_order):
            for right_profile in profile_order[left_index + 1 :]:
                delta = corrected_profiles[left_profile] - corrected_profiles[right_profile]
                absolute_delta = np.abs(delta)
                eye_result["correctedPairwiseProfileDifferences"][
                    f"{left_profile}-{right_profile}"
                ] = {
                    "absoluteLumaDifference": stats(absolute_delta),
                    "meanSignedLumaDifference": float(np.mean(delta)),
                    "stableRegionAbsoluteLumaDifference": stats(
                        absolute_delta[stable]
                    )
                    if np.any(stable)
                    else None,
                    "stableSpatialGrid": spatial_grid(absolute_delta, stable),
                }
        result["eyes"][eye] = eye_result

    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    if not args.quiet:
        print(rendered)


if __name__ == "__main__":
    main()
