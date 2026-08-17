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

### `PA-2026-004` — Preserve the common inherited baseline during first synthesis

- Date: 2026-08-17
- Status: proposed for provisional preset generation
- Snapshot: census plus first screen, profile curves, and factorial interactions through commit `37f17abe3`
- Question: What should happen to settings that are identical across all six inherited presets and have not yet produced attributable tier evidence?
- Scope: the first AMD-informed Performance, Balanced, and Quality candidates
- Hard constraints: do not silently change correctness, stability, lifecycle, or composition behavior; do not equate inheritance with qualification
- Options considered: retune every serialized leaf; preserve common settings while testing semantic factors
- Evidence: [preset census](./preset-baseline-census.md) reduced the inherited differences to seven tiered families; the [first screen](./screening-20260817.md) shows why scene and direct-timer attribution matter
- Objective vector and uncertainty: preserves known behavior and limits regression surface, but leaves optimality and missed tier opportunities explicitly unknown
- Pareto result: unmeasured alternatives cannot displace the known common baseline yet
- Decision: retain common inherited settings in the first generated candidates; change them only through a correctness constraint or attributable factor decision
- Confidence: high as a safe synthesis rule; low that every inherited common value is optimal
- Consequences: provisional files remain reviewable and differences stay semantic rather than becoming a 600-leaf rewrite
- Revalidation triggers: cluster review, new attributable ablation/curve, shader architecture change, or correctness finding
- Supersedes: none

### `PA-2026-005` — Adopt first Skylighting and SSS tier gradients

- Date: 2026-08-17
- Status: proposed for provisional preset generation
- Snapshot: forward/reverse AMD timing and visual curves plus Skylighting × SSS factorials
- Question: Which inherited Skylighting and SSS settings should represent the first three policy tiers?
- Scope: AMD `SVR-OVR-NULL`, fixed Guardian Stones clear-day anchor; vendor-neutral intent with portability open
- Hard constraints: features remain enabled; exact live/recompile settle and restoration contracts remain disclosed; no stereo or temporal regression may be accepted later
- Options considered: disable features in Performance; flatten tiers; retain the typed inherited gradients
- Evidence: [profile curves](./profile-curves-20260817.md) and [pairwise measurements](./interactions-20260817.md)
- Objective vector and uncertainty: Skylighting Quality has a clear cadence/cost step and broad ambient value; SSS Performance saves named-pass cost, while Balanced and Quality are close near-field; distance and moving-view evidence remain open
- Pareto result: disabling either feature loses a repeatable visual contribution; cheaper typed profiles preserve the phenomenon and are non-dominated for Performance/Balanced at this evidence stage
- Decision: use typed Performance/Balanced/Quality profiles for both families in the provisional candidates
- Confidence: medium for Skylighting; medium-high for SSS near-field; low for SSS Quality's unlimited-distance value
- Consequences: Quality is not treated as free, and Performance is not reduced to wholesale feature removal
- Revalidation triggers: SSS distance/disocclusion test, Skylighting moving-view stability, new anchor, physical HMD, runtime lane, GPU vendor, or shader/resource change
- Supersedes: none

### `PA-2026-006` — Keep Wetness enabled and evaluate it with IBL

- Date: 2026-08-17
- Status: proposed for provisional preset generation
- Snapshot: Wetness feature ablation, profile curves, and Wetness × IBL forward/reverse factorials
- Question: Can Wetness be tiered or judged independently from the ambient/IBL policy?
- Scope: AMD `SVR-OVR-NULL`, Guardian Stones storm anchor
- Hard constraints: IBL ambient replacement remains correct; Wetness Performance's requested 5,000-unit fade must not be misreported because runtime clamps it to 7,002.801
- Options considered: disable Wetness in Performance; select tiers from IBL-off tests; keep the feature and inherited density/range gradient coupled to IBL
- Evidence: [first screen](./screening-20260817.md), [profile curves](./profile-curves-20260817.md), and [factorial interactions](./interactions-20260817.md)
- Objective vector and uncertainty: Wetness has a large conditional visual contribution and strong non-additive IBL interaction; current tier GPU ordering and range cutoff are unresolved
- Pareto result: disabling Wetness loses a major storm material response, while cheaper parameter profiles may reduce secondary density/range without removing the phenomenon
- Decision: keep Wetness enabled in all provisional tiers, retain its typed gradient, and require IBL-active evidence for future selection
- Confidence: high for the coupling constraint; medium for keeping the feature; low for exact tier performance/range optimality
- Consequences: Wetness and IBL become one visual review cluster, though they retain separate controls
- Revalidation triggers: material/range anchor, precipitation-motion sequence, dedicated timing instrumentation, physical HMD, runtime lane, GPU vendor, or material/lighting code change
- Supersedes: none

### `PA-2026-007` — Treat Wetness tiers as perceptual coverage/event gradients

- Date: 2026-08-17
- Status: proposed for provisional preset generation
- Snapshot: four independent-parameter Wetness factorials through DLL commit `ab359c4d9`
- Question: Do range, event density, and persistence provide distinct enough responses to retain a three-tier Wetness gradient?
- Scope: AMD `SVR-OVR-NULL`, fixed Guardian Stones storm anchor, IBL active
- Hard constraints: Wetness remains enabled; no aggregate-frame timing is relabelled as Wetness cost; fixed-pose null-HMD motion is not physical-HMD stability evidence
- Options considered: flatten Wetness across tiers; disable it in Performance; retain the inherited gradient as a perceptual policy; claim a performance gradient
- Evidence: [independent Wetness parameter follow-up](./profile-curves-20260817.md#wetness-independent-parameter-follow-up), with 30-frame paired range orders and 60-frame paired temporal orders
- Objective vector and uncertainty: material fade, raindrop coverage, opportunity density, and persistence all produce repeatable absolute separation with close left/right agreement; signed temporal energy reverses with order and no named GPU timer exists
- Pareto result: the lower settings preserve the dominant wet-material phenomenon while reducing secondary coverage/event representation, so they remain a plausible non-dominated perceptual tier; their cost benefit is unknown
- Decision: retain the typed Performance/Balanced/Quality Wetness gradient provisionally, describe it as coverage and event richness, and attach no measured GPU-saving or temporal-stability claim
- Confidence: medium-high for perceptual separation in this lane; low for GPU ordering and moving-headset stability
- Consequences: generated preset documentation must distinguish requested 5,000-unit Performance fade from the 7,002.801-unit runtime minimum until the preset definition is normalized
- Revalidation triggers: named Wetness timing, known-depth material target, moving/physical HMD, runtime lane, GPU vendor, weather/material code change, or normalization of the Performance fade request
- Supersedes: narrows the uncertainty in `PA-2026-006` without replacing its IBL-coupling rule
