#!/usr/bin/env python3
"""Summarize aggregate and named-timer effects from a two-factor profiler run."""

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


def state_key(phase: dict[str, object]) -> str:
    state = phase["state"]
    return f"{int(state['A'])}{int(state['B'])}"


def state_means(values: dict[str, list[float]]) -> dict[str, float]:
    return {state: fmean(samples) for state, samples in values.items()}


def effects(means: dict[str, float]) -> dict[str, float]:
    return {
        "factorAWhenBOnMs": means["11"] - means["01"],
        "factorAWhenBOffMs": means["10"] - means["00"],
        "factorBWhenAOnMs": means["11"] - means["10"],
        "factorBWhenAOffMs": means["01"] - means["00"],
        "interactionMs": means["11"] - means["10"] - means["01"] + means["00"],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    run = json.loads(args.artifact.read_text(encoding="utf-8"))
    phase_groups: dict[str, list[dict[str, object]]] = {
        "00": [], "01": [], "10": [], "11": []
    }
    for phase in run["phases"]:
        phase_groups[state_key(phase)].append(phase)
    if any(not phases for phases in phase_groups.values()):
        raise ValueError("factorial artifact does not contain every state")

    aggregate_gpu: dict[str, list[float]] = {state: [] for state in phase_groups}
    aggregate_cpu: dict[str, list[float]] = {state: [] for state in phase_groups}
    for state, phases in phase_groups.items():
        for phase in phases:
            aggregate_gpu[state].extend(float(sample["resolvedTotalMs"]) for sample in phase["samples"])
            aggregate_cpu[state].extend(float(sample["resolvedCpuTotalMs"]) for sample in phase["samples"])

    result: dict[str, object] = {
        "schemaVersion": 1,
        "runId": run["runId"],
        "factors": run["factors"],
        "disableFirst": run["disableFirst"],
        "aggregate": {
            "gpu": {
                "states": {state: summarize(values) for state, values in aggregate_gpu.items()},
                "effects": effects(state_means(aggregate_gpu)),
            },
            "cpu": {
                "states": {state: summarize(values) for state, values in aggregate_cpu.items()},
                "effects": effects(state_means(aggregate_cpu)),
            },
        },
        "directTimers": {},
    }

    for factor_name in ("A", "B"):
        feature = run["factors"][factor_name]["feature"]
        prefix = f"{feature}::"
        timer_values: dict[str, list[float]] = {state: [] for state in phase_groups}
        timer_names: set[str] = set()
        for state, phases in phase_groups.items():
            for phase in phases:
                for sample in phase["samples"]:
                    matching = [
                        timer for timer in sample["timers"]
                        if timer["name"].startswith(prefix)
                    ]
                    timer_names.update(timer["name"] for timer in matching)
                    timer_values[state].append(
                        sum(float(timer.get("topLevelMs", 0.0)) for timer in matching)
                    )
        means = state_means(timer_values)
        result["directTimers"][factor_name] = {
            "feature": feature,
            "timerPrefix": prefix,
            "timers": sorted(timer_names),
            "states": {state: summarize(values) for state, values in timer_values.items()},
            "effects": effects(means),
        }

    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    if not args.quiet:
        print(rendered)


if __name__ == "__main__":
    main()
