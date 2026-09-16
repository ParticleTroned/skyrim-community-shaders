# Automated HMD assessment of NR colour

Colour-mode evaluation requires image-based assessment of the actual CSX
HMD submissions. The user does not judge the images. Private reconstruction
measurements from `tools/nr-color/assess.py` remain technical diagnostics;
their RGB-drift ranking cannot qualify image quality or select a default.

This protocol specializes the existing VR automation image-quality protocol
for NR colour. It does not change colour code, the release qualification
protocol, or the separate performance campaign.

The instrumented branch binds versioned rendered NR and camera evidence to
accepted screenshot acquisitions. Preflight those capabilities and the exact
producer before running the [executable workflow](../../tools/nr-color/hmd_workflow.md).
The [pre-build source audit](nr-colour-hmd-prebuild-audit.md) records the
implementation and remaining live checks. Status polling alone still cannot
replace the captured frame's immutable attribution.

The immediate objective is colour diagnosis and correction for later porting
to `main-VR`. Record the actual FOV or character-mask route and hold it fixed
within a comparison block. Broad coverage obtained with a FOV value such as
0.95 remains an approximation; a dedicated general-NR route is separate
future work. Each tested route needs its own baseline, and untested routes
remain unqualified.

## Existing integration and ownership

Use the installed marketplace skills and the maintained implementations in
`skyrim-vr-automation` on `dev`. Record the exact automation commit and hashes
of the scripts actually invoked, including any session-owned copies. Read
that repository's `AGENTS.md`, its `docs/fork-parity/image-quality-protocol.md`,
and the applicable DevBench, MO2, capture and visual-review contracts first.
Read the shader repository's `main-VR` agent, tooling and Ghidra integration
guidance without switching the checkout under test.

DevBench is a separate SKSE mod, maintained in the DevBench repository on
`main`. Preserve its physical DLL identity separately from the CSX producer,
enabled AIO DLL, adjacent build manifest and build receipt. An MO2 mod name
or package version alone does not establish which binaries are running.

Use exactly one live DevBench transport. Search the complete callable tool
catalog for `mcp__devbench_vr__` before selecting a lane. Callable direct tools
are mandatory when present. Otherwise use the established DevBench controller
with explicitly resolved runtime metadata and its identity checks. Discover
the live action schemas before use. Never fabricate an HTTP client, silently
change endpoints, or retry a mutation whose acknowledgement was lost.

MO2 access is shared even when source worktrees are independent. Inspect and
request access through the existing controller. Respect `access-busy`;
an overdue estimate is not expiry. Use a retained task workspace or create
one through the workspace controller after its fixture and ownership gates.
Keep its profile, saves, caches and evidence. Do not select another task's
profile or recover its lease merely to begin this campaign.

Use `tools/capture-interaction-control/Invoke-CaptureInteraction.ps1` for
correlated recording/capture sessions on the controller lane, with capability
preflight, an explicit task evidence directory, and bounded finalization.
The existing sequence descriptor already requests native left/right PNGs,
`hmd_submission`, `fallback: reject` and `sdr_srgb`. Use the exact callable
`communityshaders.screenshot` actions on the direct lane. Do not run the
controller alongside it. Real sequences use `sequence_start`, not a loop of
unrelated still requests. Inspect committed original images from both eyes.

Ghidra is the separately owned static-analysis service. Use the existing
Ghidra controller and a matching imported artifact if producer investigation
is needed. Neither Ghidra analysis nor image appearance proves NVIDIA's
colour contract. Do not start, stop or replace another task's Ghidra program.

## Freeze the experiment before candidates

Record scene/cell, save identity, player/camera and HMD poses, projection and
submitted eye bounds, render/display dimensions, weather/time, insertion
point, ROI/category policy, feature settings and upscaling state. Use the
existing prepared-scene barrier. Do not change unrelated graphics settings.
Never use `tfc 1` to manufacture a stationary scene.

Retain at least three unchanged NR-off baseline sequences before comparing
candidates. Each sequence contains at least twelve committed stereo pairs.
Use the same frozen cadence throughout; 500 ms is the existing capture
controller's default and provides a short exposure/animation observation
window. The executable plan accepts a preregistered 50..60000 ms interval.
Preserve actual acquisition frames and timestamps. Dropped frames are not duplicate
samples, and requested cadence is not observed cadence.

At the default 500 ms, only two frames per second are sampled. Such captures
can alias or miss HMD frame-rate flicker, and even the controller's 50 ms
minimum cannot establish full HMD frame-rate stability. Report measured
spacing and temporal findings at the capture cadence only. Neither reviewer
agreement nor low sampled variance closes this sampling limitation.

Observe baseline animation, exposure adaptation and camera drift. If the
scene cannot support a comparison, preserve those captures and select a
better fixture before fixing the regional policy. Do not choose crops after
seeing which candidate wins. Save and hash a per-eye region policy containing
explicit native-pixel rectangles and their semantic descriptions:

-   skin, including lit and shadowed portions where available;
-   neutral/material colours, identified by surface rather than an assumption
    that lighting or skin should be neutral;
-   deep shadows;
-   highlights and reflections;
-   unaffected background, outside the fixed neural ROI when available.

Match corresponding scene content across eyes with separately predetermined
rectangles. Reuse each eye's exact rectangles for every candidate and repeat.
Record absent or occluded region classes as untested; do not silently replace
them with convenient areas. Keep ROI selection and category strengths fixed.

## Distinct comparisons

Preserve each row independently in the technical candidate mapping:

| Condition             | Required evidence and interpretation                                                                                 |
| --------------------- | -------------------------------------------------------------------------------------------------------------------- |
| NR off                | NR master disabled; source reference, with no claim of retained neural detail.                                       |
| Raw NR                | Real inference; `legacy_raw`; compatibility output, not Managed identity.                                            |
| Managed identity      | Real inference; `managed`; identity transform, manual exposure multiplier one.                                       |
| Conversion candidates | Real inference; explicit Managed profile with a recorded producer-based rationale and domain hypothesis.             |
| Preserve Source       | Real inference; `preserve_source`; record all detail/appearance parameters.                                          |
| Display-only source   | `applyModelEdit: false` with inference continuing for the corresponding candidate; not NR off or a transport bypass. |

Identity, manual `linear_to_srgb`, and manual `reversible_proxy` are already
enumerated by the numeric assessment runner. A conversion is eligible only
with its stated interpretation and actual input range retained; do not infer
linear input from a floating-point texture or a pass name. Captured-exposure
profiles additionally require the declared exact producer-frame age and
valid, unambiguous exposure. Keep current-frame and previous-frame exposure
candidates separate. Preserve unsupported candidates with their reasons.

Read effective mode, revision, profile, processed frame and fresh applied
state after every change. Configuration acknowledgement alone is insufficient.
Require complete stereo batches where the API supplies them, plus outer
renderer evaluation/commit evidence. Keep fallback counters and dispositions
visible. Zero private-buffer error during a fallback is not valid inference.
Raw and NR-off observations must not be passed through a validator that
requires effective Managed mode.

Bracket each capture with identity/configuration observations and preserve
their relation to actual acquisition frames. Preserve frame-specific
telemetry where available. If a configuration change, fallback, unproven
application, or identity gap intersects a sequence, exclude the affected
comparison; do not label nearby status as exact-frame proof.

## Capture ordering and blinding

Generate opaque candidate IDs and a recorded random seed before dispatch.
Keep the seed, settings-to-ID map, build names, acquisition order and
technical diagnostics outside the visual-review input directory. Randomize
candidate order for each of three independent repetitions, then run its
reverse order. Include flanking returns to the unchanged NR-off baseline.
Keep both eyes and each complete sequence together.

For every inference condition, also capture shown/hidden/shown and
hidden/shown/hidden display-only comparisons. Only the display switch changes
inside these blocks: input profile, inference, ROI and history stay intact.
Record fresh applied-state evidence for each phase. Do not pool hidden-source
captures from different inference conditions or treat them as NR-off repeats.

Randomize visual presentation independently of acquisition order. Use two
presentation passes with displayed A/B order swapped. Use the existing
`tools/render-scale-qualification/AutomatedVisualReviewProvider.psm1` provider
for fresh, ephemeral image-review contexts and retained request/response
receipts. Its generic provider accepts a task-specific prompt and output
schema; its render-scale qualification categories are not an NR colour
scorecard. Preserve its two passes and three replicates per pass. A fresh
image-capable agent with no conversation fork is an alternative when available.

The reviewer receives anonymous full-resolution left/right images, identical
crops, sequence ordinals/timing, region descriptions and reference roles.
It receives no colour-mode mapping or technical ranking. Use
`tools/nr-color/hmd-review.prompt.md` for the review criteria. Record and hash
all assessments before revealing the mapping. If no fresh context is
available, record the blinding limitation; never call an informed review blind.

## Artifact acceptance

Preserve the original terminal receipts and final sequence manifests before
deriving images. Validate all of the following from the actual service output:

-   requested, effective and acquired source are `hmd_submission`, with
    fallback rejected and no source-fallback warning;
-   sequence completion and all scheduled ordinals are accounted for, including
    failed, cancelled and dropped captures;
-   each accepted pair belongs to one child request/acquisition and compositor
    cycle, with exactly one left-eye and one right-eye artifact;
-   acquisition frame, per-eye dimensions, bounds, orientation, publication
    generation, device identity, DXGI format and colour-space metadata exist
    and agree with the pinned capture conditions;
-   each artifact is committed, exists, decodes as PNG and matches its recorded
    byte length, SHA-256 and actual width/height;
-   both outputs declare identical `sdr_srgb` encoding and match their native
    staged eye extents, with no capture crop or resize;
-   the intended CSX/DevBench runtime identities and unchanged configuration
    bracket the acquisition, with the required fresh applied-state evidence.

Do not trust a file extension, latest-frame pointer, screenshot request
acceptance or thumbnail as proof. An SBS overview is supplementary. Preserve
every failed capture and exclusion reason; qualify a replacement separately.

## Regional and temporal measurements

Analyse the original decoded PNG pixels in each eye against that eye's own
reference. Record formulas, colour domain, units, sample counts, thresholds,
tool versions, source hashes and region-policy hash with machine-readable
results. Use existing ablation/temporal analyzers where applicable, preserving
their definitions. Extend task-local analysis only for missing colour metrics.

Retain signed mean/median R, G and B changes in 8-bit sRGB code values,
absolute RGB error, and brightness percentile changes (at least p05, p50 and
p95). Keep encoded luma distinct from linear-light relative luminance. Any
sRGB decoding for measurement uses one fixed transfer function for all inputs;
it is not an image correction and does not identify the model input domain.

Record near-black and near-white pixel fractions with explicit fixed
thresholds, shadow/highlight percentile shifts, and clipping/saturation
fractions. Measure local contrast and edge/high-frequency changes in fixed
regions. Higher edge energy can be noise or ringing: score useful neural
detail separately from colour fidelity, and support it with original-pixel
inspection. Do not average away opposite signed channel or eye shifts.

Compute the same measures between unchanged baseline repeats, including
flanking returns. Report their range/dispersion alongside every candidate
effect. Preserve per-repetition results and forward/reverse agreement; frames
within one sequence are not independent experimental repetitions. Differences
inside baseline variability are unresolved or neutral, not improvements.

Keep per-frame regional measurements for flicker and exposure recovery.
Report actual time/frame spacing, consecutive brightness/colour/edge changes,
and the time course after a mode/display change. A deliberate exposure
challenge requires the same preserved scene/pose event for all candidates.
If no such challenge ran, mark exposure recovery under changing illumination
untested. Ambient animation must not be counted automatically as flicker.

Inspect stereo geometry and the difference between each eye's signed changes.
Per-eye reference subtraction precedes any stereo summary; natural disparity,
occlusion and view-dependent reflections are not colour faults by definition.

Do not white-balance, histogram-match, independently normalize, relight,
sharpen, denoise, warp or optimize registration of candidate images. Reject
motion/occlusion-confounded regions or comparisons and retain their originals
and reasons. Temporal medians and difference maps may be labeled diagnostics;
they never replace review of the original sequence. Annotate only derived
copies, using the same crops, scales and difference-map legend for all modes.

## Decision and retained output

Report four separate conclusions: colour fidelity, useful neural detail,
temporal stability and stereo consistency. Require repeatability above the
baseline variation, consistent reversed ordering, and medium/high-confidence
blinded evidence for a directional claim. Disagreement remains indeterminate.
Do not collapse these objectives into one RGB or sharpness score.

NR off and display-only source establish references; zero RGB difference
cannot make either the best neural result. Keep successful inference,
fallback and no-inference outcomes distinct after unblinding. Even a
repeatable visual preference cannot prove NVIDIA's correct colour contract.
Do not change colour code or choose a production default to optimize a
selected screenshot. State scene coverage and uncertainties explicitly.

Retain:

1. Unmodified per-eye PNGs, terminal receipts, capture manifests and hashes.
2. Anonymous original-image copies and fixed identical review crops.
3. Machine-readable regional, per-frame, baseline-repeat and stereo results.
4. Annotated derived comparisons, prompts, structured review responses and
   the written image-based assessment.
5. The separately retained candidate map, randomization/acquisition schedule,
   effective-state diagnostics, runtime/build identities and ownership receipts.
6. Every failed/excluded capture, reason, untested condition and evidence gap.

Screenshots and recording remain outside the separate performance campaign.
Confirm owned screenshot/recording work is inactive before handing the runtime
back for timing. Capture timings are acquisition diagnostics, not performance
measurements. Missing live evidence is reported as blocked or untested; a
protocol document, numeric-buffer result or synthetic fixture is not a
completed HMD image assessment.
