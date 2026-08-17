# Preset automation decision log

This is an append-only record. Supersede a decision with a new entry rather than editing its historical outcome. Evidence links may be repaired without changing the recorded decision.

## Entry template

### `PA-YYYY-NNN` — Short title

- Date:
- Status: proposed, adopted, rejected, superseded
- Snapshot:
- Question:
- Scope:
- Hard constraints:
- Options considered:
- Evidence:
- Objective vector and uncertainty:
- Pareto result:
- Decision:
- Confidence:
- Consequences:
- Revalidation triggers:
- Supersedes:

## Decisions

### `PA-2026-001` — Preserve a multi-objective evidence vector

- Date: 2026-08-17
- Status: adopted
- Snapshot: preset automation evidence-system initialization
- Question: How should performance, visual quality, stability, stereo correctness, interaction risk, coverage, and confidence be combined?
- Scope: all generated and reviewed preset choices
- Hard constraints: correctness, stability, safety, and compatibility are gates rather than tradeable score components
- Options considered: one weighted scalar; independent objectives with Pareto comparison
- Evidence: project requirements and the architectural ambiguity recorded in the [interaction survey](../shader-feature-interaction-survey.md)
- Objective vector and uncertainty: retained in the policy and measurement records
- Pareto result: not applicable to this process decision
- Decision: retain independent objectives, remove dominated candidates, then apply tier policy to the frontier
- Confidence: high
- Consequences: a preset choice cannot be justified by one opaque score
- Revalidation triggers: evidence that the process cannot make reproducible choices or that an objective is missing
- Supersedes: none

### `PA-2026-002` — Use one vendor-neutral preset policy

- Date: 2026-08-17
- Status: adopted
- Snapshot: preset automation evidence-system initialization
- Question: Should AMD and NVIDIA use separate preset paths?
- Scope: Performance, Balanced, and Quality policy structure
- Hard constraints: demonstrated vendor correctness or capability differences must still be handled safely
- Options considered: separate vendor trees; shared intent with narrow evidence-backed overrides
- Evidence: project requirement to make both vendors respond to one unified preset
- Objective vector and uncertainty: vendor portability remains an explicit confidence dimension
- Pareto result: not applicable to this process decision
- Decision: use one policy tree and introduce only narrow vendor-specific capability or safety overrides
- Confidence: high for structure; AMD-only for initial measurements
- Consequences: AMD-first evidence does not silently become NVIDIA evidence
- Revalidation triggers: measured vendor divergence that cannot be expressed as a narrow override
- Supersedes: none

### `PA-2026-003` — Keep synthetic and physical runtime lanes distinct

- Date: 2026-08-17
- Status: adopted
- Snapshot: before synthetic-HMD proof of concept
- Question: What can an unmanned SteamVR null-HMD run establish?
- Scope: capture, profiling, and cross-runtime comparisons
- Hard constraints: runtime, projection, eye geometry, occupancy, and compatibility-layer provenance must remain attributable
- Options considered: treat all VR captures as interchangeable; qualify each exact lane and measure cross-lane agreement
- Evidence: current CSX submitted-eye capture contract and planned SteamVR/OpenXR/VDXR comparison
- Objective vector and uncertainty: run variance and cross-lane bias are retained rather than normalized away
- Pareto result: not applicable to this process decision
- Decision: use synthetic SteamVR/OpenVR as an automation lane only; it is not OCU evidence and does not establish physical-headset or OpenXR parity
- Confidence: high
- Consequences: SteamVR native OpenVR, OCU to SteamVR OpenXR, and OCU to VDXR receive distinct lane identities
- Revalidation triggers: a calibrated harness demonstrates bounded cross-lane equivalence for a specific metric and scope
- Supersedes: none
