#!/usr/bin/env python3
"""Align and summarize an on/off/on stereo player-translation sequence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


PHASE_NAMES = ("baseline-before", "ablated", "baseline-return")
EYES = ("left", "right")


def luma(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def summarize(values: np.ndarray) -> dict[str, float]:
    return {
        "mean": float(np.mean(values)),
        "median": float(np.median(values)),
        "p95": float(np.percentile(values, 95)),
        "p99": float(np.percentile(values, 99)),
        "maximum": float(np.max(values)),
    }


def resolve_phase_directory(run_path: Path, raw_path: str) -> Path:
    directory = Path(raw_path)
    if directory.is_dir():
        return directory
    candidate = run_path.parent / directory.name
    if candidate.is_dir():
        return candidate
    raise FileNotFoundError(f"phase directory is unavailable: {raw_path}")


def load_eye_frames(directory: Path, eye: str, stride: int) -> np.ndarray:
    frames = sorted(directory.glob(f"frame_*_{eye}.*"))
    if len(frames) < 3:
        raise ValueError(f"fewer than three {eye}-eye frames in {directory}")
    loaded: list[np.ndarray] = []
    for frame in frames:
        with Image.open(frame) as image:
            rgb = np.asarray(image.convert("RGB"), dtype=np.float32)[::stride, ::stride]
        loaded.append(luma(rgb))
    return np.stack(loaded)


def detect_motion_event(
    left: np.ndarray,
    right: np.ndarray,
    queued_before: int,
    queued_after: int,
) -> tuple[int, list[float], tuple[int, int]]:
    left_energy = np.mean(np.abs(np.diff(left, axis=0)), axis=(1, 2))
    right_energy = np.mean(np.abs(np.diff(right, axis=0)), axis=(1, 2))
    energy = 0.5 * (left_energy + right_energy)

    # Delta index k describes frame k -> k+1. The command is issued after at
    # least queued_before frames. Readback may lag until queued_after frames.
    search_start = max(0, queued_before - 2)
    search_stop = min(len(energy), max(queued_before + 3, queued_after + 2))
    if search_start >= search_stop:
        search_start, search_stop = 0, len(energy)
    peak_delta = search_start + int(np.argmax(energy[search_start:search_stop]))
    event_frame = peak_delta + 1
    return event_frame, [float(value) for value in energy], (search_start, search_stop)


def correlation(a: np.ndarray, b: np.ndarray) -> float | None:
    a_flat = a.reshape(-1).astype(np.float64)
    b_flat = b.reshape(-1).astype(np.float64)
    a_std = float(np.std(a_flat))
    b_std = float(np.std(b_flat))
    if a_std <= 1.0e-9 or b_std <= 1.0e-9:
        return None
    return float(np.corrcoef(a_flat, b_flat)[0, 1])


def first_sustained_recovery(
    series: list[float],
    relative_frames: list[int],
    tail_value: float,
    tolerance_fraction: float,
    consecutive: int = 3,
) -> int | None:
    tolerance = max(0.05, abs(tail_value) * tolerance_fraction)
    for start in range(len(series) - consecutive + 1):
        if relative_frames[start] < 0:
            continue
        window = series[start : start + consecutive]
        if all(abs(value - tail_value) <= tolerance for value in window):
            return relative_frames[start]
    return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--tail-frames", type=int, default=5)
    parser.add_argument("--recovery-tolerance", type=float, default=0.10)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    run_path = args.run.resolve()
    run = json.loads(run_path.read_text(encoding="utf-8"))
    phases = {phase["name"]: phase for phase in run["phases"]}
    missing = [name for name in PHASE_NAMES if name not in phases]
    if missing:
        raise ValueError(f"motion manifest lacks required phases: {', '.join(missing)}")

    frames: dict[str, dict[str, np.ndarray]] = {}
    alignment: dict[str, Any] = {}
    for name in PHASE_NAMES:
        phase = phases[name]
        directory = resolve_phase_directory(run_path, phase["outputDirectory"])
        eye_frames = {
            eye: load_eye_frames(directory, eye, args.stride) for eye in EYES
        }
        if "scheduledPreStepFrames" in phase["motion"]:
            queued_before = int(phase["motion"]["scheduledPreStepFrames"])
            queued_after = queued_before + 4
        else:
            queued_before = int(phase["motion"]["queuedFramesBeforeCommand"])
            queued_after = int(phase["motion"]["queuedFramesAfterObservedMove"])
        event_frame, energy, search_window = detect_motion_event(
            eye_frames["left"],
            eye_frames["right"],
            queued_before,
            queued_after,
        )
        frames[name] = eye_frames
        alignment[name] = {
            "eventFrameIndexZeroBased": event_frame,
            "eventFrameNumberOneBased": event_frame + 1,
            "searchDeltaIndexRange": list(search_window),
            "meanConsecutiveFrameDelta": energy,
            "peakMeanConsecutiveFrameDelta": energy[event_frame - 1],
        }

    before_count = min(alignment[name]["eventFrameIndexZeroBased"] for name in PHASE_NAMES)
    after_count = min(
        frames[name]["left"].shape[0]
        - alignment[name]["eventFrameIndexZeroBased"]
        for name in PHASE_NAMES
    )
    relative_frames = list(range(-before_count, after_count))
    if after_count < args.tail_frames:
        raise ValueError("not enough aligned post-step frames for the requested tail")

    aligned: dict[str, dict[str, np.ndarray]] = {}
    for name in PHASE_NAMES:
        event = alignment[name]["eventFrameIndexZeroBased"]
        aligned[name] = {
            eye: frames[name][eye][event - before_count : event + after_count]
            for eye in EYES
        }

    result: dict[str, Any] = {
        "schemaVersion": 1,
        "runId": run["runId"],
        "feature": run["feature"],
        "parameter": run["parameter"],
        "acceptedCapture": bool(run["validity"]["accepted"]),
        "analysis": (
            "motion-aligned native-eye luma comparison; on reference is the mean "
            "of baseline-before and baseline-return, and drift is their absolute difference"
        ),
        "stride": args.stride,
        "alignment": alignment,
        "commonRelativeFrameRange": [relative_frames[0], relative_frames[-1]],
        "eyes": {},
    }

    eye_effect_maps: dict[str, list[np.ndarray]] = {}
    for eye in EYES:
        a1 = aligned["baseline-before"][eye]
        off = aligned["ablated"][eye]
        a2 = aligned["baseline-return"][eye]
        on_reference = 0.5 * (a1 + a2)
        effect = on_reference - off
        drift = np.abs(a1 - a2)
        eye_effect_maps[eye] = [effect[index] for index in range(effect.shape[0])]

        per_frame: list[dict[str, Any]] = []
        effect_mae_series: list[float] = []
        for index, relative in enumerate(relative_frames):
            effect_map = effect[index]
            drift_map = drift[index]
            effect_mae = float(np.mean(np.abs(effect_map)))
            drift_mae = float(np.mean(drift_map))
            effect_mae_series.append(effect_mae)
            per_frame.append(
                {
                    "relativeFrame": relative,
                    "effectSignedLuma": float(np.mean(effect_map)),
                    "effectAbsoluteLuma": summarize(np.abs(effect_map)),
                    "baselineReturnDriftAbsoluteLuma": summarize(drift_map),
                    "effectToDriftMaeRatio": (
                        effect_mae / drift_mae if drift_mae > 1.0e-9 else None
                    ),
                    "fractionEffectAbove1Luma": float(np.mean(np.abs(effect_map) > 1.0)),
                    "fractionEffectAbove2Luma": float(np.mean(np.abs(effect_map) > 2.0)),
                }
            )

        tail_effect = effect[-args.tail_frames :]
        tail_drift = drift[-args.tail_frames :]
        tail_effect_mae = float(np.mean(np.abs(tail_effect)))
        tail_drift_mae = float(np.mean(tail_drift))
        recovery = first_sustained_recovery(
            effect_mae_series,
            relative_frames,
            tail_effect_mae,
            args.recovery_tolerance,
        )
        result["eyes"][eye] = {
            "perFrame": per_frame,
            "tail": {
                "frameCount": args.tail_frames,
                "effectSignedLuma": float(np.mean(tail_effect)),
                "effectAbsoluteLuma": summarize(np.abs(tail_effect)),
                "baselineReturnDriftAbsoluteLuma": summarize(tail_drift),
                "effectToDriftMaeRatio": (
                    tail_effect_mae / tail_drift_mae
                    if tail_drift_mae > 1.0e-9
                    else None
                ),
            },
            "firstThreeFrameRecoveryWithinTailFraction": recovery,
            "recoveryToleranceFraction": args.recovery_tolerance,
        }

    stereo_correlations: list[dict[str, Any]] = []
    for index, relative in enumerate(relative_frames):
        stereo_correlations.append(
            {
                "relativeFrame": relative,
                "samePixelEffectCorrelation": correlation(
                    eye_effect_maps["left"][index], eye_effect_maps["right"][index]
                ),
            }
        )
    result["stereo"] = {
        "note": (
            "Same-pixel correlation is diagnostic only because physical stereo parallax means "
            "corresponding scene points do not occupy identical pixels."
        ),
        "perFrame": stereo_correlations,
    }

    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    if not args.quiet:
        print(rendered)


if __name__ == "__main__":
    main()
