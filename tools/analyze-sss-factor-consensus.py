#!/usr/bin/env python3
"""Build order-balanced consensus from two SSS factor factorial runs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


ONE_CODE = 1.0 / 255.0
COMPARISONS = {
    "samplesAtUnlimited": ("01", "11"),
    "samplesAtCapped": ("00", "10"),
    "unlimitedAtHighSamples": ("10", "11"),
    "unlimitedAtLowSamples": ("00", "01"),
}


def load_run(path: Path) -> tuple[str, dict[str, dict[str, np.ndarray]]]:
    record = json.loads(path.read_text(encoding="utf-8"))
    frames: dict[str, dict[str, list[np.ndarray]]] = {}
    for phase in record["phases"]:
        key = f"{int(bool(phase['state']['A']))}{int(bool(phase['state']['B']))}"
        state = frames.setdefault(key, {"left": [], "right": []})
        for measurement in phase["factorMeasurements"]:
            packed = np.asarray(Image.open(measurement["outputPath"]).convert("L"), dtype=np.float32) / 255.0
            if packed.shape[1] % 2:
                raise ValueError(f"packed-stereo width must be even: {measurement['outputPath']}")
            midpoint = packed.shape[1] // 2
            state["left"].append(packed[:, :midpoint])
            state["right"].append(packed[:, midpoint:])
    missing = sorted({"00", "01", "10", "11"} - frames.keys())
    if missing:
        raise ValueError(f"{path}: missing states {', '.join(missing)}")
    medians = {
        key: {eye: np.median(np.stack(values), axis=0) for eye, values in eyes.items()}
        for key, eyes in frames.items()
    }
    return record["runId"], medians


def grid_mean(values: np.ndarray) -> list[list[float]]:
    result: list[list[float]] = []
    for y in range(3):
        y0, y1 = values.shape[0] * y // 3, values.shape[0] * (y + 1) // 3
        row: list[float] = []
        for x in range(3):
            x0, x1 = values.shape[1] * x // 3, values.shape[1] * (x + 1) // 3
            row.append(float(np.mean(values[y0:y1, x0:x1])))
        result.append(row)
    return result


def write_heatmap(path: Path, delta: np.ndarray) -> None:
    scale = max(float(np.percentile(np.abs(delta), 99)), ONE_CODE)
    normalized = np.clip(delta / scale, -1.0, 1.0)
    rgb = np.zeros((*delta.shape, 3), dtype=np.uint8)
    rgb[..., 0] = np.rint(np.clip(-normalized, 0.0, 1.0) * 255.0).astype(np.uint8)
    rgb[..., 1] = np.rint(np.clip(normalized, 0.0, 1.0) * 255.0).astype(np.uint8)
    rgb[..., 2] = rgb[..., 1]
    Image.fromarray(rgb, mode="RGB").save(path)


def consensus_metrics(first: np.ndarray, second: np.ndarray) -> tuple[dict[str, object], np.ndarray]:
    consensus = (first + second) * 0.5
    either_changed = (np.abs(first) > ONE_CODE) | (np.abs(second) > ONE_CODE)
    sign_agree = (
        (np.abs(first) > ONE_CODE)
        & (np.abs(second) > ONE_CODE)
        & (np.signbit(first) == np.signbit(second))
    )
    robust = np.where(sign_agree, consensus, 0.0)
    if np.count_nonzero(either_changed) > 1:
        correlation = float(np.corrcoef(first[either_changed], second[either_changed])[0, 1])
    else:
        correlation = None
    return {
        "firstSignedMean": float(np.mean(first)),
        "secondSignedMean": float(np.mean(second)),
        "consensusSignedMean": float(np.mean(consensus)),
        "consensusMeanAbsolute": float(np.mean(np.abs(consensus))),
        "orderDifferenceMeanAbsolute": float(np.mean(np.abs(first - second))),
        "correlationWhereEitherChanged": correlation,
        "signAgreeingFractionOfEitherChanged": float(
            np.count_nonzero(sign_agree) / max(np.count_nonzero(either_changed), 1)
        ),
        "signAgreeingCoverageFullFrame": float(np.mean(sign_agree)),
        "robustSignedMeanFullFrame": float(np.mean(robust)),
        "robustMeanAbsoluteFullFrame": float(np.mean(np.abs(robust))),
        "robustSignedMeanGrid3x3": grid_mean(robust),
        "robustMeanAbsoluteGrid3x3": grid_mean(np.abs(robust)),
    }, robust


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    first_id, first = load_run(args.first)
    second_id, second = load_run(args.second)
    args.output.mkdir(parents=True, exist_ok=True)
    result: dict[str, object] = {
        "schemaVersion": 1,
        "runIds": [first_id, second_id],
        "factorMeaning": "1.0 is unshadowed; negative candidate-minus-reference adds shadow",
        "threshold": ONE_CODE,
        "comparisons": {},
    }
    output_comparisons = result["comparisons"]
    assert isinstance(output_comparisons, dict)
    for name, (reference, candidate) in COMPARISONS.items():
        output_comparisons[name] = {}
        for eye in ("left", "right"):
            first_delta = first[candidate][eye] - first[reference][eye]
            second_delta = second[candidate][eye] - second[reference][eye]
            stats, robust = consensus_metrics(first_delta, second_delta)
            output_comparisons[name][eye] = stats
            write_heatmap(args.output / f"robust-{name}-{eye}.png", robust)

    result_path = args.output / "factor-consensus.json"
    result_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
