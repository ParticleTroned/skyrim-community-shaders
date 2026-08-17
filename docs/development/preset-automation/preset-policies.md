# Preset policies

Status: framework adopted; first AMD-informed provisional candidates recorded below

## Common contract

Performance, Balanced, and Quality are vendor-neutral policy tiers. They describe intended visual outcomes, frame-budget behavior, risk tolerance, and scene coverage. They do not encode AMD or NVIDIA implementations in their names or create parallel vendor preset trees.

The selection order is:

1. reject violations of hard correctness, stereo, stability, compatibility, or safety constraints;
2. reject evidence outside the lane, GPU vendor, scene, or setting scope actually measured;
3. retain the objective vector rather than collapsing it into one score;
4. remove Pareto-dominated candidates within the decision scope;
5. apply the tier priorities and budget to the remaining frontier;
6. record the choice, alternatives, confidence, and revalidation triggers.

A vendor-specific override is allowed only when a capability, driver behavior, correctness failure, or material performance-shape difference is demonstrated. It should be the narrowest setting-level constraint possible and must retain a link to its evidence.

## Objective vector

Each candidate comparison may contain:

| Objective | Direction | Notes |
| --- | --- | --- |
| GPU steady-state cost | Lower | Retain average and distribution, not only one sample |
| GPU tail latency | Lower | `p95`/`p99` and missed-frame implications |
| Settle or transition cost | Lower | Separate from steady state |
| Memory/resource cost | Lower | Include lifecycle and transient behavior where material |
| Perceptual contribution | Higher | Scene- and phenomenon-specific |
| Temporal stability | Higher | Flicker, ghosting, shimmer, settling |
| Stereo correctness | Hard gate, then higher | Eye parity, projection, disocclusion, double vision |
| Interaction risk | Lower | Breadth, ambiguity, and regression surface |
| Scene coverage | Higher | How consistently the benefit appears in required scenes |
| Evidence confidence | Higher | Directness, repeatability, sample size, and portability |

The vector is extensible. Its raw units and uncertainty remain visible.

## Performance

Intent: preserve coherent, stable VR rendering while recovering GPU headroom where perceptual loss is controlled.

- Correctness and stability remain hard gates.
- Prefer candidates with low steady-state and tail cost across common scenes.
- Accept reduced range, sampling, resolution, or secondary detail when the dossier predicts monotonic degradation and captures confirm it.
- Avoid features whose benefit is narrow but cost is broad unless they can be gated safely.
- Keep transition spikes and memory pressure inside the declared runtime budget.

## Balanced

Intent: retain the most visible and broadly applicable improvements while protecting frame-time consistency.

- Correctness and stability remain hard gates.
- Prefer strong perceptual contribution per unit cost without using a single aggregate score.
- Preserve features that improve common lighting, material, or visibility phenomena across the required scene suite.
- Select middle settings only when the control has demonstrated meaningful, stable intermediate behavior; “middle” is not automatically balanced.
- Spend budget on non-dominated candidates with high scene coverage before niche effects.

## Quality

Intent: maximize justified visual fidelity without accepting correctness regressions, uncontrolled tails, or redundant composition.

- Correctness and stability remain hard gates.
- Permit higher cost when a repeatable perceptual benefit exists and the frame/memory budget is still satisfied.
- Prefer fuller spatial, temporal, or material representation when it improves the declared phenomenon rather than merely increasing work.
- Do not enable experimental features solely because this is the highest tier.
- Redundant or physically inconsistent lighting remains disallowed even when performance is available.

## Hardware evidence state

AMD is the first measurement target. Until NVIDIA runs exist:

- AMD-qualified choices may populate a provisional vendor-neutral policy when their implementation contract is vendor-independent;
- portability confidence remains explicitly unknown for NVIDIA;
- a candidate is not disabled or forked for NVIDIA merely because it is unmeasured; and
- release behavior should retain existing safe defaults where portability has not been established.

When NVIDIA evidence arrives, compare performance shape, correctness, and capability behavior against the same tier intent. Create a narrow override only for a demonstrated divergence.

## Policy record requirements

Every adopted setting needs:

- dossier and interaction references;
- tested values and safe bounds;
- hardware and runtime-lane scope;
- scenes and poses;
- timing and visual measurement records;
- hard-gate result;
- frontier alternatives;
- reason the tier selected this point;
- confidence and open questions; and
- dependency, driver, shader, runtime, or feature changes that require revalidation.

## Provisional candidate matrix — 2026-08-17

This is the first policy synthesis, not a release qualification. “Retain” means preserve the inherited setting while evidence is incomplete; it does not upgrade that setting to measured truth. The evidence snapshot is:

- [existing preset census](./preset-baseline-census.md);
- [first feature screen](./screening-20260817.md);
- [first quality-profile curves](./profile-curves-20260817.md); and
- [first pairwise interactions](./interactions-20260817.md).

The matrix is now materialized as the three reproducible development candidates in [`unified-preset-candidates-20260817.md`](./unified-preset-candidates-20260817.md). Generation does not promote them to release status or remove the legacy vendor-labelled sources.

### Hard composition and structure constraints

- Use one Performance/Balanced/Quality shader policy for AMD and NVIDIA.
- Resolve upscaler provider and supported implementation at a narrow capability boundary; do not fork the shader policy.
- Keep IBL's ambient-replacement semantics correct in every tier. Quality budget does not permit double ambient contribution.
- Keep Wetness review coupled to the intended IBL/ambient policy. IBL-off Wetness evidence is not representative of the production composition.
- Keep timing capture separate from screenshot/video capture.
- Do not promote null-HMD fixed-pose symmetry or controlled player translation to physical-headset, rotating-view, OpenXR, OCU, or VDXR qualification.

### Tiered feature candidates

| Family | Performance | Balanced | Quality | Current disposition | Confidence and gate |
| --- | --- | --- | --- | --- | --- |
| Skylighting | Typed Performance profile: smaller field/grid and 16/8 update cadence | Typed Balanced profile | Typed Quality profile | Provisional evidence-backed gradient; keep enabled in all tiers | Medium on AMD `SVR-OVR-NULL`; Quality cost is clear, signed brightness ordering is not. One 32-unit step converged in both eyes within about 167 ms without a gross persistent trail, but its tier residual was drift-limited; continuous/rotating motion and other anchors remain open |
| Screen Space Shadows | 16 reference samples, 20,480 cull | 30 samples, 20,480 cull | 44 samples, unlimited range | Provisional evidence-backed gradient; keep enabled | Medium-high on AMD `SVR-OVR-NULL`; with samples held at 44, unlimited versus 20,480 range produced a moving-scene tail about 1.48× return drift with close eye agreement and ~100 ms convergence. Known-distance attribution around the fade boundary plus continuous/rotating stereo remain open |
| Wetness | Typed Performance density/lifetime/range profile | Typed Balanced profile | Typed Quality profile | Keep enabled; preserve the now independently visible coverage/event gradient, but do not claim a measured tier cost | Medium-high for feature importance, IBL interaction, and perceptual tier separation on AMD `SVR-OVR-NULL`; low for tier GPU cost. One storm position-step converged in both eyes within about 67 ms but remained drift-limited; continuous precipitation/physical-HMD stability is open. Performance fade is effectively 7,002.801, not requested 5,000 |
| Grass Collision | Off | On | On | Retain inherited gradient | Low; static null pose did not exercise collision. Requires reproducible controller/player/grass movement |
| SSGI | Evaluation candidate: AO-only, interiors only, Quarter resolution | Evaluation candidate: AO-only, interiors only, Half resolution | Evaluation candidate: AO-only, interiors only, Full resolution | Strong AO contribution, monotonic cost curve, and accepted translational recovery; keep disabled in release-safe fallback until ambient/rotating-stereo gates pass | Medium-high for AMD AO magnitude, cost, and one 32-unit translation; low for ambient-cluster interaction, rotating/physical stereo, runtime, and vendor portability |
| Terrain Blending | 725 cull distance | 1,024 | 1,024 | Retain inherited gradient | Low-medium; continuous pass floor measured near 0.0205 ms, but distance response and cutoff visibility are open |
| Volumetric Lighting | Low exterior/interior quality | Medium exterior/interior quality | High exterior/interior quality | Provisional measured gradient; retain enabled | High for named-pass cost ordering and live mutability; low for perceptual ordering. The fog position-step needed about 467 ms to enter its tail and remained drift-limited; a strong-shaft target plus continuous/rotating motion is required |
| Volumetric Shadows | Enabled | Enabled | Enabled | Keep common; no on/off tier split | Medium on AMD `SVR-OVR-NULL`; six named passes cost ~0.0384 ms continuously, while fog/day visual effects are subtle and drift-limited. Shared cross-material directional-shadow semantics outweigh the small saving; moving/physical-HMD and runtime portability remain open |
| Upscaling quality | Intent corresponding to inherited mode 4 | Intent corresponding to inherited mode 3 | Intent corresponding to inherited mode 2 | Retain tier intent; resolve provider by capability | Low for current visual/performance quality because no provider curve was run; high that vendor selection belongs at this boundary |

### Common inherited baseline

All feature settings that are identical across the six inherited files remain common across the three provisional candidates. This is deliberate factor control: the first preset changes only settings with either an inherited tier gradient or a demonstrated hard composition requirement. It is not a conclusion that every common setting is optimal.

The common baseline should be reviewed cluster-by-cluster. A common setting may change only when one of these occurs:

1. a correctness, stereo, stability, compatibility, or lifecycle issue requires it;
2. an attributable screen finds a meaningful dominated alternative;
3. architecture shows that the current setting violates a composition contract; or
4. a new tier opportunity has a monotonic, repeatable response and enough scene coverage.

### Provisional tier reading

- **Performance:** preserve IBL, Wetness, Skylighting, SSS, Volumetric Shadows, and the common correctness baseline; buy headroom through cheaper Skylighting/SSS/Wetness parameters, inherited upscaling intent, shorter terrain range, and disabled Grass Collision. This is not an “effects off” tier.
- **Balanced:** use the measured centre profiles for Skylighting and SSS, the inherited Wetness centre profile, enabled Grass Collision, the middle upscaling intent, and otherwise the common baseline. This is the current recommended development reference.
- **Quality:** use the high profiles where they represent fuller range/coverage, but retain hard constraints. Skylighting's higher cadence is a real cost; SSS unlimited range now has direct factor evidence above the interior noise floor but still needs known-distance and moving-view qualification; experimental/inactive features do not become enabled merely because budget is higher.

### Revalidation block before generated presets become release candidates

The minimum remaining gates are:

1. SSS known-distance target around the 19,280-20,480-unit fade band, plus a continuous or rotating disocclusion sequence; the first +64-unit translation now qualifies separate-eye response and ~100 ms step convergence;
2. Wetness known-depth near/far material target plus physical/moving-HMD precipitation sequence with IBL active; fixed-pose paired sequences qualify independent magnitude and the first position step found no gross persistent trail, but signed continuous-motion stability remains open;
3. Terrain Blending 725/1,024 boundary scene;
4. Grass Collision controlled movement scene;
5. a reproducible strong volumetric-shaft/dense-fog scene with continuous/rotating motion; Guardian clear/fog establishes cost and a low-confidence visual floor, while its first step sequence took about 467 ms to reach the settled tail;
6. at least one interior/day, interior/night, exterior/day, exterior/night, and adverse-weather coverage pass;
7. occupied physical-HMD comparison in native SteamVR/OpenVR;
8. OCU/OpenXR via SteamVR OpenXR and VDXR as distinct lanes; and
9. NVIDIA validation against the same policy, introducing only evidence-backed provider/capability overrides.

Before enabling the SSGI candidate in generated release presets, add a paired IBL × Skylighting × SSGI ambient-composition screen and a rotating/physical-HMD stereo-disocclusion sequence. The accepted Dragonsreach step-translation result establishes recovery by roughly 100 ms in this null-HMD lane; it does not prove that the combined ambient stack is correctly balanced or that rotation/reprojection is stable.
