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

### `PA-2026-008` — Keep Volumetric Shadows enabled across tiers

- Date: 2026-08-17
- Status: proposed for provisional preset generation
- Snapshot: live inner-control timing plus fog/dawn and clear/day lossless A/B/A captures through DLL commit `ab359c4d9`
- Question: Does disabling the shared Volumetric Shadows provider offer a defensible Performance-tier trade?
- Scope: AMD `SVR-OVR-NULL`, fixed Guardian Stones pose, Info logging; vendor-neutral intent with portability open
- Hard constraints: do not conflate this provider with the separate Volumetric Lighting feature; preserve lighting/particle/water/effect shadow semantics unless a saving dominates their loss; rejected backpressured captures are not evidence
- Options considered: disable in Performance; disable in Performance and Balanced; keep enabled in all tiers
- Evidence: [Volumetric Shadows live-control follow-up](./screening-20260817.md#volumetric-shadows-live-control-follow-up), including 120-frame timing phases and accepted separate-eye fog/day visual sequences
- Objective vector and uncertainty: six named passes cost a repeatable ~0.0384 ms; stable visual differences are subtle (~0.28 luma fog, ~0.41 luma clear day) and signed direction is not stable across anchors; moving-view and portability remain unmeasured
- Pareto result: the small saving does not dominate the provider's shared cross-material directional-shadow behavior, and there is no lower numeric quality state between enabled and disabled
- Decision: keep the inner `Enabled` value true in Performance, Balanced, and Quality
- Confidence: medium in this AMD null-HMD lane; low for physical-HMD and cross-runtime portability
- Consequences: preset generation retains one common Boolean and does not create a vendor fork; future tuning should seek a quality gradient before reconsidering wholesale disable
- Revalidation triggers: moving/physical HMD, SteamVR/OpenXR/VDXR/OCU lane, NVIDIA GPU, shadow-provider algorithm/resource change, or a new anchor with materially larger attributable response
- Supersedes: none

### `PA-2026-009` — Keep calibration evidence outside MO2 and SkyrimVR

- Date: 2026-08-17
- Status: adopted
- Snapshot: RootBuilder resource-exhaustion incident after the first autonomous screening session
- Question: Where may high-volume capture and profiler artifacts persist between MO2 launches?
- Scope: all preset-calibration screenshots, sequences, profiler payloads, and derived diagnostics
- Hard constraints: RootBuilder must never hash or deploy the evidence archive; SkyrimVR and MO2 must be stopped before staging is moved; evidence provenance and timestamps must survive relocation
- Options considered: retain evidence beneath `overwrite\Root`; manually clean it occasionally; write to an external archive by default and drain accidental staging at every session boundary
- Evidence: `overwrite\Root\CSX Baselines` contained 12,328 files and about 165.6 GB, while `Screenshots\PipelineDiagnostics` contained 530 files and about 0.829 GB. RootBuilder attempted per-file threaded hashing, exhausted open-file capacity, constructed an approximately 861,371-node virtual tree, and copied about 164.2 GB into the SkyrimVR directory before the derived installation was manually removed and SkyrimVR reinstalled.
- Objective vector and uncertainty: external storage removes RootBuilder deployment cost and D: duplication; direct game-process writes to `L:` still require a runtime proof after reinstall
- Pareto result: overwrite retention has no evidence-quality advantage and creates catastrophic resource and duplication risk
- Decision: resolve automation defaults through the repository-local external archive setting; treat MO2 overwrite as session-only staging; run the bounded finaliser before any later MO2 launch
- Confidence: high
- Consequences: this workstation uses `L:\CSX Preset Automation`; historical absolute evidence paths follow the archive move; campaign locators and hashes remain the stable identities
- Revalidation triggers: MO2/RootBuilder behavior change, archive-drive change, capture-path permission failure, or introduction of a session orchestrator that enforces an equivalent boundary
- Supersedes: none

### `PA-2026-010` — Stage capture on NVMe, then archive to HDD

- Date: 2026-08-17
- Status: adopted
- Snapshot: first direct-to-L lossless Terrain Blending visual curve after storage remediation
- Question: Should active lossless capture write directly to the persistent HDD archive?
- Scope: screenshot sequences, stereo pairs, preview videos, profiler payloads, and derived diagnostics produced by preset calibration
- Hard constraints: active staging must remain outside MO2 and SkyrimVR; exact-pair validity gates remain mandatory; the completed session must be drained to persistent storage
- Options considered: write directly to L:; temporarily use a game or overwrite path on D:; use an independent D: staging root and move it to L: at session end
- Evidence: direct-to-L PNG capture produced 41 backpressure drops at a 10-cycle interval and one drop at 45 cycles; a 60-cycle interval was valid, showing that HDD write/encode drain can constrain capture cadence even when file accumulation is safe
- Objective vector and uncertainty: independent NVMe staging improves capture headroom and remains invisible to RootBuilder; finalisation adds one bounded D:-to-L transfer; exact throughput varies by compression and scene
- Pareto result: an independent D: root keeps the speed benefit without the RootBuilder risk of game or overwrite staging
- Decision: default calibration tools to repository-local `csx.calibrationStagingRoot`, use `D:\CSX Preset Automation Staging` on this workstation, and move the dated session tree to `csx.calibrationArchiveRoot` during the existing stopped-runtime finaliser
- Confidence: high
- Consequences: L: remains the evidence archive; D: is temporary and must be empty of completed sessions; failed-run metadata is preserved until finalisation
- Revalidation triggers: staging-drive change, sustained zero-drop direct-to-archive capture at target cadence, finaliser verification failure, or a future capture API with bounded asynchronous spill management
- Supersedes: the direct-to-archive default in `PA-2026-009`; its prohibition on persistence beneath MO2 or SkyrimVR remains active

### `PA-2026-011` — Retain the inherited Terrain Blending distance tiers provisionally

- Date: 2026-08-17
- Status: adopted provisionally
- Snapshot: first live scalar-parameter timing and drift-resistant stereo campaign
- Question: Should `TerrainCullDistance` remain `725` for Performance and `1024` for Balanced/Quality?
- Scope: Terrain Blending distance only; `BlendStrength` and package enablement are unchanged
- Hard constraints: exact live readback and restoration; no stereo regression; no visual conclusion from unbalanced temporal drift
- Options considered: `1024` in all tiers; inherited `725/1024/1024`; `0` for Quality to disable distance culling
- Evidence: [Terrain Blending distance calibration](./terrain-blending-20260817.md), including 120-frame `0/725/1024` timing phases and three bracketing-baseline `725/1024` visual cycles
- Objective vector and uncertainty: Guardian feature timers were flat at about 0.34 ms render passes plus 0.02 ms depth blend; the Winterhold three-cycle mean visual residual was about 0.215 luma with p95 0.607 and no pixels above 3 luma; no repeatable spatial response was detected, but the expected performance saving remained below noise and scene coverage is narrow
- Pareto result: `725` has no demonstrated visual disadvantage and an architectural opportunity to cull more landscape bounds, but it is not yet a measured performance win
- Decision: retain `725` for Performance and `1024` for Balanced/Quality pending a distance-targeted moving-scene campaign
- Confidence: medium for tested-anchor visual equivalence; low for performance benefit and runtime/vendor portability
- Consequences: preset generation keeps one vendor-neutral tier path; the result must be labelled provisional rather than promoted as a qualified optimization
- Revalidation triggers: visible terrain/static seam spanning 10.35–14.62 m, moving or physical HMD, foliage-free exterior, OpenXR/VDXR/OCU lane, NVIDIA hardware, or Terrain Blending pass/culling changes
- Supersedes: none

### `PA-2026-012` — Use the preallocated Volumetric Lighting tier gradient provisionally

- Date: 2026-08-17
- Status: adopted provisionally
- Snapshot: first typed Low/Medium/High exterior timing and clear/fog lossless stereo curves
- Question: Should Volumetric Lighting remain High in every tier or use the engine's preallocated quality gradient?
- Scope: AMD `SVR-OVR-NULL`, Guardian Stones clear day and fog dawn, Info logging
- Hard constraints: retain the feature; do not conflate it with Volumetric Shadows; custom target sizes remain restart-bound; no perceptual claim beyond the tested fixed poses
- Options considered: High common; Low common; Low/Medium/High tier gradient
- Evidence: [SSGI and Volumetric Lighting calibration](./ssgi-volumetric-lighting-20260817.md), with 120-frame timing phases and two lossless visual anchors
- Objective vector and uncertainty: named-pass medians rise monotonically from about 0.12 to 0.14 to 0.20 ms; visual Low/High differences were not confidently separable from returning-baseline drift; strong volumetric shafts and moving-view stability remain open
- Pareto result: Low is cheaper with no detected loss in the tested compositions, but untested shaft/fog structure prevents declaring High dominated globally; the gradient preserves a conservative Quality ceiling
- Decision: use Low / Medium / High for Performance / Balanced / Quality as a provisional vendor-neutral candidate
- Confidence: high for AMD cost ordering and control mutability; low for perceptual ordering and portability
- Consequences: expose the same gradient to AMD and NVIDIA; never use custom target dimensions in unattended live sweeps
- Revalidation triggers: reproducible strong godrays, dense fog, physical/moving HMD, another runtime lane, NVIDIA, or volumetric resource implementation changes
- Supersedes: none

### `PA-2026-013` — Promote SSGI AO to a gated tier candidate

- Date: 2026-08-17
- Status: proposed; blocked from release generation by ambient-composition and moving-stereo gates
- Snapshot: first AO-only enable and Full/Half/Quarter curves with owned preconfiguration restoration
- Question: Should the inherited all-disabled SSGI state remain common, or should AO become a tiered preset component?
- Scope: AMD `SVR-OVR-NULL`, Guardian Stones timing and Dragonsreach visual/on-off timing, Info logging, AO-only resources
- Hard constraints: `ResourceProfile=AO-only`; no GI/IL/specular activation; no double ambient/double AO; exact restoration to the original disabled state; physical/moving-HMD stereo remains unqualified
- Options considered: remain disabled; enable Quality only; enable interiors in all tiers with Quarter/Half/Full resolution
- Evidence: [SSGI and Volumetric Lighting calibration](./ssgi-volumetric-lighting-20260817.md). Half-resolution AO costs about 0.15–0.18 ms whole-frame in Dragonsreach and produces a repeatable ~1.16–1.20 mean-luma contribution with ~0.79 residual correlation. Named SSGI work is about 0.35–0.37 ms Full, 0.14 ms Half, and 0.06 ms Quarter.
- Objective vector and uncertainty: AO contribution and cost are repeatable with close eye agreement; resolution visual ordering is subtler and NPC-contaminated; interaction with IBL/Skylighting/engine ambient and moving stereo is not yet qualified
- Pareto result: inexpensive Quarter/Half AO supplies a real perceptual contribution, so the inherited dormant values are not an adequate final policy; correctness gates still dominate the apparent benefit
- Decision: evaluate interiors-only AO at Quarter / Half / Full for Performance / Balanced / Quality; retain all-disabled as the release-safe fallback until the ambient and stereo gates pass
- Confidence: medium-high for fixed-pose AMD magnitude and cost; low for release correctness and portability
- Consequences: generated development candidates may carry the gradient behind an explicit experimental/gated status; release presets must not enable it silently yet
- Revalidation triggers: IBL × Skylighting × SSGI factorial, Vanilla SSAO state audit, moving/physical HMD, disocclusion sequence, alternate runtime, NVIDIA, or SSGI history/resource changes
- Supersedes: the unqualified SSGI inheritance row, but not the current release-safe disabled fallback
