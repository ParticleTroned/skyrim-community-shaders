# Preset automation evidence system

Status: initialized

Initial hardware focus: AMD

## Purpose

This directory is the evidence and decision layer for LLM-assisted and human-reviewed preset generation. It records what a rendering feature is intended to do, how it composes with other features, what it costs under controlled conditions, and why a setting belongs in a Performance, Balanced, or Quality policy.

It deliberately does not reduce a feature to one score. A candidate retains a multi-objective evidence vector:

- GPU cost, including tail latency;
- perceptual contribution;
- temporal stability;
- VR stereo correctness;
- interaction and regression risk;
- scene coverage;
- memory and resource cost; and
- evidence maturity and confidence.

Hard correctness and stability constraints are applied first. Surviving candidates are compared using Pareto dominance: a candidate is dominated only when another is no worse in every relevant objective and better in at least one. A tier policy then chooses among the non-dominated candidates and records the rationale.

## Record topology

| Record | Role | Canonical location |
| --- | --- | --- |
| Feature dossier | The maintained “essence” of one rendering phenomenon or feature contract | [`dossiers/`](./dossiers/README.md) |
| Interaction ledger | Producer, consumer, composition, correctness, and performance relationships | [`interaction-ledger.md`](./interaction-ledger.md) |
| Control contract | Live/reload/restart mutability, UI disclosure, and automation API requirements | [`control-contract.md`](./control-contract.md) |
| Benchmark protocol | Controlled scenes, capture procedure, runtime lanes, and validity gates | [`benchmark-protocol.md`](./benchmark-protocol.md) |
| Artifact storage policy | Archive resolution, safe staging, and mandatory session finalisation | [`storage-policy.md`](./storage-policy.md) |
| Sequence capture calibration | Measured lossless throughput envelopes and temporal/stereo capture profiles | [`sequence-capture-calibration.md`](./sequence-capture-calibration.md) |
| Anchor scenes | Validated scene recipes, time/weather variants, expected pose, and retained proof captures | [`anchor-scenes.md`](./anchor-scenes.md) |
| Measurement run | Machine-readable provenance and results for one controlled run | [`measurements/`](./measurements/README.md) |
| Preset policy | Vendor-neutral intent, constraints, priorities, and selection rules for each tier | [`preset-policies.md`](./preset-policies.md) |
| Existing preset census | Structural comparison of the six inherited MGO vendor/tier files and the resulting screening priorities | [`preset-baseline-census.md`](./preset-baseline-census.md) |
| First-pass screening | AMD native-OpenVR timing and visual A/B/A results, artifact hashes, interpretation, and follow-up routing | [`screening-20260817.md`](./screening-20260817.md) |
| First quality-profile curves | Forward/reverse AMD timing and visual curves for Skylighting, Screen Space Shadows, and Wetness | [`profile-curves-20260817.md`](./profile-curves-20260817.md) |
| First pairwise interactions | Forward/reverse AMD timing and visual factorials for Skylighting × Screen Space Shadows and Wetness × IBL | [`interactions-20260817.md`](./interactions-20260817.md) |
| Terrain Blending distance calibration | Live-control validation, AMD timing, drift-resistant stereo comparison, and provisional tier implication | [`terrain-blending-20260817.md`](./terrain-blending-20260817.md) |
| SSGI and Volumetric Lighting calibration | Typed live/recompile/restart boundaries, AMD timing, fixed-pose and controlled-translation stereo response, and provisional tier implications | [`ssgi-volumetric-lighting-20260817.md`](./ssgi-volumetric-lighting-20260817.md) |
| Unified provisional preset candidates | Machine-readable three-tier synthesis, capability-selected upscaler boundary, generator, and release gates | [`unified-preset-candidates-20260817.md`](./unified-preset-candidates-20260817.md) |
| Unified Quality smoke record | Machine-readable AMD/null-HMD provider-resolution and control-readback proof for the generated Quality file | [`unified-quality-smoke-20260817.json`](./unified-quality-smoke-20260817.json) |
| Decision log | Append-only rationale connecting evidence to adopted or rejected choices | [`decision-log.md`](./decision-log.md) |

The canonical schemas are:

- [`feature-dossier.schema.json`](./schemas/feature-dossier.schema.json);
- [`measurement-run.schema.json`](./schemas/measurement-run.schema.json).

Schema version changes are required when a field changes meaning or units, not merely when an optional field is added.

## Seed evidence

The following existing documents remain authoritative source evidence and are not duplicated here:

- the [shader and feature interaction survey](../shader-feature-interaction-survey.md) supplies the first architectural map and the `D`/`S`/`I`/`?` evidence notation;
- the [IBL interaction review](../ibl-interaction-review.md) is the first focused feature essence, including the ambient-replacement contract and unresolved ownership boundaries; and
- [capture and calibration evidence](../capture-calibration.md) defines the current CSX capture and profiler contract.

The survey and IBL review predate the machine-readable dossier schema. They are valid seed reviews, not silently “qualified” dossiers. Their claims should be promoted into dossier records only with source links and unchanged evidence grades.

## Evidence flow

1. Select a rendering phenomenon rather than an arbitrary HLSL file.
2. Create a dossier from [`dossiers/feature-dossier.template.json`](./dossiers/feature-dossier.template.json).
3. Register material producer/consumer or composition relationships in the interaction ledger.
4. Define the controlled scene, runtime lane, pose, settings snapshot, and comparison before running it.
5. Record timing and visual passes separately using a measurement-run record.
6. Reject runs that fail correctness, provenance, capture, or comparability gates.
7. Compare valid candidates as an evidence vector and retain the Pareto frontier.
8. Apply the appropriate preset policy and append the decision, confidence, and revalidation triggers.

## Scope boundaries

- AMD is the first measurement target. The schema is vendor-neutral and does not convert AMD observations into NVIDIA conclusions.
- Performance, Balanced, and Quality express visual intent and budget. They are not AMD and NVIDIA preset forks.
- Vendor-specific behavior belongs in a narrow capability or safety override backed by evidence. It must not create an independent policy tree by default.
- Synthetic-HMD runs are an automation lane. They do not establish parity with a physical, occupied headset or with another VR runtime.
- “SteamVR” and “OpenXR” are not mutually exclusive labels. Records keep the application VR API, compatibility layer, target runtime API, runtime implementation, compositor/transport, HMD mode, and HMD profile separate.
- The initial SteamVR null-HMD proof of concept is native OpenVR through SteamVR. It cannot be used as Open Composite Unleashed evidence.

## Terms

- **Feature essence:** the maintained intent, representation, consumers, controls, constraints, artifacts, and evidence for a rendering phenomenon.
- **Hard constraint:** a correctness, stability, safety, or compatibility condition that a candidate must satisfy before performance or preference trade-offs are considered.
- **Evidence vector:** the set of independently retained objectives and confidence values for a candidate.
- **Pareto frontier:** the candidates not dominated across the objectives relevant to the decision.
- **Runtime lane:** one exact application-API, translation-layer, target-API, runtime, compositor/transport, HMD-mode, and HMD-profile combination.
- **Qualified:** supported by repeatable measurements in its stated lane and scope; it does not mean portable to other lanes or GPU vendors.
