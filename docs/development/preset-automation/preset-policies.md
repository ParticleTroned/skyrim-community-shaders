# Preset policies

Status: policy framework initialized; no feature settings qualified

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
