# Feature interaction ledger

Status: initialized from static survey; relationships require progressive evidence

This ledger indexes relationships that can alter correctness, appearance, cost, or the interpretation of a measurement. Detailed claims remain in their linked reviews until promoted with an evidence grade.

Evidence grades follow the [shader and feature interaction survey](../shader-feature-interaction-survey.md):

- `D`: directly observed in registration, binding, dispatch, hook, shader call, or measurement;
- `S`: stated by code summary or documentation but not exhaustively verified;
- `I`: conservative inference;
- `?`: open.

## Relationship fields

Each promoted relationship should retain:

| Field | Meaning |
| --- | --- |
| ID | Stable identifier, for example `INT-ambient-001` |
| Producer or feature A | The source, upstream feature, or first factor |
| Consumer or feature B | The consumer, downstream feature, or second factor |
| Relationship | Produces, consumes, replaces, composes, competes, gates, aliases, or perturbs measurement |
| Composition contract | What should happen when both are active |
| Correctness risk | Double application, omission, stale history, eye mismatch, ordering, domain mismatch, or other hard risk |
| Performance interaction | Additive, overlapping, enabling, disabling, scene-dependent, lifecycle-only, or unknown |
| Context | Shader family, pass, scene, runtime lane, resolution, eye layout, and settings |
| Evidence | `D`/`S`/`I`/`?`, sources, measurements, and snapshot |
| Status | Open, reviewed, measured, constrained, or resolved |

## Seed clusters

| Cluster ID | Participants | Why it matters | Seed evidence | Status |
| --- | --- | --- | --- | --- |
| `CLU-ambient` | Engine ambient, DALC, Dynamic Cubemaps, IBL, Skylighting, SSGI, deferred composite | Ambient radiance has several producers and policies; replacement versus addition must be explicit | [General survey](../shader-feature-interaction-survey.md), [IBL review](../ibl-interaction-review.md) | Focused IBL review complete; wider ownership open |
| `CLU-material-lighting` | True PBR, Extended Materials, Extended Translucency, wetness, hair, subsurface scattering, shared lighting evaluation | Multiple BRDF/material extensions can share or reinterpret the same inputs | [General survey](../shader-feature-interaction-survey.md) | Static mapping only |
| `CLU-shadows` | Screen-space shadows, terrain shadows, cloud shadows, first-person shadows, light-limit/shadow policy, deferred composite | Shadow representations may overlap, gate one another, or differ by domain and eye | [General survey](../shader-feature-interaction-survey.md) | Static mapping only |
| `CLU-display-vr` | Upscaling, sharpening, foveation, render scale, VR eye layout, hidden-area mask, submission | Resolution and eye-domain changes affect both output and measurement comparability | [General survey](../shader-feature-interaction-survey.md), [capture contract](../capture-calibration.md) | Protocol initialized |
| `CLU-diagnostics` | Profiler, screenshot capture, performance overlay, RenderDoc | Observation can perturb timing or change available runtime paths | [General survey](../shader-feature-interaction-survey.md), [capture contract](../capture-calibration.md) | Timing and visual passes separated |

## Promotion rule

Add an individual interaction row only when its direction, context, and source can be stated. A shared shader define alone proves exposure, not execution or meaningful interaction. Absence from this ledger is not evidence of independence.

## Individual relationships

| ID | A | B | Relationship and composition contract | Correctness/performance concern | Context | Evidence | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `INT-ambient-001` | Dynamic Cubemaps | IBL | Supplies environment radiance used by IBL; ownership and refresh lifetime must remain attributable | Stale or mismatched radiance; capture/update cost may not be attributable to the consumer | Ambient-light cluster | `D/S`; [IBL review](../ibl-interaction-review.md) | Reviewed, runtime measurement open |
| `INT-ambient-002` | IBL | Engine ambient term | IBL states a diffuse ambient replacement contract rather than indiscriminate additive lighting | Double ambient contribution or missing ambient contribution | IBL-enabled lighting consumers | `S/D`; [IBL review](../ibl-interaction-review.md) | Hard semantic constraint; full consumer verification open |
| `INT-ambient-003` | Wetness | IBL | Wet material response and IBL compose non-additively; tier decisions must preserve their joint surface-lighting result | Judging Wetness without IBL substantially understates its visible darkening/material response | Guardian Stones storm, AMD `SVR-OVR-NULL`, fixed stereo pose | `D`; [first factorial measurements](./interactions-20260817.md) | Measured and constrained in one lane/anchor; material and runtime coverage open |
| `INT-shadow-001` | Skylighting | Screen Space Shadows | Broad ambient redistribution and local screen-space shadowing overlap spatially but remain separate controls | Whole-frame interaction direction is drift-sensitive; the SSS named pass ran more frequently with Skylighting active in this anchor | Guardian Stones clear day, AMD `SVR-OVR-NULL`, fixed stereo pose | `D`; [first factorial measurements](./interactions-20260817.md) | Measured; causal scheduling path and moving-view coverage open |
| `INT-diag-001` | Screenshot sequence capture | GPU timing | Timing and visual evidence are collected in separate passes | Readback/encoding and queue behavior could contaminate feature cost | VR submitted-eye capture | `D/S`; [capture contract](../capture-calibration.md) | Protocol constraint |

The first runtime work should expand relationships only when the proof-of-concept harness produces attributable observations.
