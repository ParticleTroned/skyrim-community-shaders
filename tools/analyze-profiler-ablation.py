#!/usr/bin/env python3
"""Extract direct feature-timer cadence from an A/B/A profiler artifact."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import fmean, median


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize(values: list[float]) -> dict[str, float | int]:
    active = [value for value in values if value > 0.0]
    return {
        "samples": len(values),
        "activeSamples": len(active),
        "activeFraction": len(active) / len(values) if values else 0.0,
        "meanAcrossAllMs": fmean(values) if values else 0.0,
        "meanWhenActiveMs": fmean(active) if active else 0.0,
        "medianWhenActiveMs": median(active) if active else 0.0,
        "p95WhenActiveMs": percentile(active, 0.95),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=Path)
    parser.add_argument(
        "--timer-prefix",
        help="Timer prefix; defaults to the artifact feature name followed by ::",
    )
    args = parser.parse_args()

    run = json.loads(args.artifact.read_text(encoding="utf-8"))
    prefix = args.timer_prefix or f"{run['feature']}::"
    names = sorted(
        {
            timer["name"]
            for phase in run["phases"]
            for sample in phase["samples"]
            for timer in sample["timers"]
            if timer["name"].startswith(prefix)
        }
    )

    phases: list[dict[str, object]] = []
    for phase in run["phases"]:
        gpu: list[float] = []
        cpu: list[float] = []
        for sample in phase["samples"]:
            matching = [
                timer
                for timer in sample["timers"]
                if timer["name"].startswith(prefix)
            ]
            gpu.append(sum(float(timer.get("topLevelMs", 0.0)) for timer in matching))
            cpu.append(sum(float(timer.get("cpuMs", 0.0)) for timer in matching))
        phases.append(
            {
                "name": phase["name"],
                "gpuTopLevel": summarize(gpu),
                "cpu": summarize(cpu),
            }
        )

    print(
        json.dumps(
            {
                "schemaVersion": 1,
                "runId": run["runId"],
                "feature": run["feature"],
                "timerPrefix": prefix,
                "timers": names,
                "phases": phases,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
