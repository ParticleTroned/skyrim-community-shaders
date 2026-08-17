#!/usr/bin/env python3
"""Analyze packed-stereo Screen Space Shadows factor measurements."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def load_factor(path: Path) -> tuple[np.ndarray, np.ndarray]:
    image = np.asarray(Image.open(path).convert("L"), dtype=np.float32) / 255.0
    if image.shape[1] % 2:
        raise ValueError(f"packed-stereo factor width must be even: {path}")
    midpoint = image.shape[1] // 2
    return image[:, :midpoint], image[:, midpoint:]


def grid_means(values: np.ndarray) -> list[list[float]]:
    rows: list[list[float]] = []
    for y in range(3):
        y0, y1 = values.shape[0] * y // 3, values.shape[0] * (y + 1) // 3
        row: list[float] = []
        for x in range(3):
            x0, x1 = values.shape[1] * x // 3, values.shape[1] * (x + 1) // 3
            row.append(float(np.mean(values[y0:y1, x0:x1])))
        rows.append(row)
    return rows


def metrics(delta: np.ndarray) -> dict[str, object]:
    absolute = np.abs(delta)
    return {
        "signedMean": float(np.mean(delta)),
        "meanAbsolute": float(np.mean(absolute)),
        "p95Absolute": float(np.percentile(absolute, 95)),
        "p99Absolute": float(np.percentile(absolute, 99)),
        "fractionChangedAboveOneCode": float(np.mean(absolute > (1.0 / 255.0))),
        "signedMeanGrid3x3": grid_means(delta),
        "meanAbsoluteGrid3x3": grid_means(absolute),
    }


def write_heatmap(path: Path, delta: np.ndarray) -> None:
    scale = max(float(np.percentile(np.abs(delta), 99)), 1.0 / 255.0)
    normalized = np.clip(delta / scale, -1.0, 1.0)
    rgb = np.zeros((*delta.shape, 3), dtype=np.uint8)
    rgb[..., 0] = np.rint(np.clip(-normalized, 0.0, 1.0) * 255.0).astype(np.uint8)
    rgb[..., 1] = np.rint(np.clip(normalized, 0.0, 1.0) * 255.0).astype(np.uint8)
    rgb[..., 2] = rgb[..., 1]
    Image.fromarray(rgb, mode="RGB").save(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path, help="visual-factorial-run.json")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    record = json.loads(args.run.read_text(encoding="utf-8"))
    output = args.output or args.run.parent / "factor-analysis"
    output.mkdir(parents=True, exist_ok=True)

    states: dict[str, dict[str, list[np.ndarray]]] = {}
    for phase in record["phases"]:
        key = f"{int(bool(phase['state']['A']))}{int(bool(phase['state']['B']))}"
        state = states.setdefault(key, {"left": [], "right": []})
        for measurement in phase.get("factorMeasurements", []):
            left, right = load_factor(Path(measurement["outputPath"]))
            state["left"].append(left)
            state["right"].append(right)

    missing = sorted({"00", "01", "10", "11"} - states.keys())
    if missing:
        raise ValueError(f"missing factor states: {', '.join(missing)}")

    medians: dict[str, dict[str, np.ndarray]] = {}
    for key, eyes in states.items():
        medians[key] = {}
        for eye, frames in eyes.items():
            if not frames:
                raise ValueError(f"state {key}/{eye} has no factor frames")
            median = np.median(np.stack(frames), axis=0)
            medians[key][eye] = median
            Image.fromarray(np.rint(median * 255.0).astype(np.uint8), mode="L").save(
                output / f"median-state-{key}-{eye}.png"
            )

    comparisons = {
        "samplesAtUnlimited": ("01", "11"),
        "samplesAtCapped": ("00", "10"),
        "unlimitedAtHighSamples": ("10", "11"),
        "unlimitedAtLowSamples": ("00", "01"),
    }
    result: dict[str, object] = {
        "schemaVersion": 1,
        "runId": record["runId"],
        "factorMeaning": "1.0 is unshadowed; a negative candidate-minus-reference delta adds shadow",
        "stateFrameCounts": {
            key: {eye: len(frames) for eye, frames in eyes.items()} for key, eyes in states.items()
        },
        "comparisons": {},
    }
    result_comparisons = result["comparisons"]
    assert isinstance(result_comparisons, dict)
    for name, (reference, candidate) in comparisons.items():
        result_comparisons[name] = {}
        for eye in ("left", "right"):
            delta = medians[candidate][eye] - medians[reference][eye]
            result_comparisons[name][eye] = metrics(delta)
            write_heatmap(output / f"difference-{name}-{eye}.png", delta)

    result["interaction"] = {}
    for eye in ("left", "right"):
        interaction = (
            medians["11"][eye]
            - medians["10"][eye]
            - medians["01"][eye]
            + medians["00"][eye]
        )
        result["interaction"][eye] = metrics(interaction)
        write_heatmap(output / f"difference-interaction-{eye}.png", interaction)

    result_path = output / "factor-analysis.json"
    result_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
