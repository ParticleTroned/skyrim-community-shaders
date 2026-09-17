# Corrected Preserve Source HMD detail comparison

The corrected reconstruction shader ran successfully in Skyrim VR.
Preserve Source strengths 0, 1 and 2, plus inference-running display-hidden
controls, produced 156 attributed stereo pairs. Increased edge contrast
does not establish useful new detail: the strength-zero repeat and ground
control also changed. This is a bounded regional comparison, not release,
whole-image or frame-rate temporal qualification.

Three independent blinded reviewers found regional colour and stereo ties
against the source controls. Useful-detail judgments were ties or
indeterminate, with no repeatable advantage for strength two. The evidence
does not justify changing the existing detail default.

## Build and fixture

The tested source is `7f3f6279c6c43f8998a6ff43bfe51d98ae70d57d` on
`work/face-of-gogh-colour-managed-20260914`. The
[shader investigation](nr-colour-detail-investigation-20260916.md) records
the adjacent-tap correction and its WARP regression.

-   Build ID:
    `24edcea53076fd092f82f2559cdff2a299e797960b2637a2b018db775a906f83`.
-   Physical enabled AIO DLL: 24,922,112 bytes, SHA-256
    `d07965268d065d13946de7251e55cf92475e80f3288ccb5920fd9e77d4eeb144`.
    It matches its adjacent manifest, AIO receipt and runtime producer.
-   Installed reconstruction shader: 3,103 bytes, SHA-256
    `65af3a3e6da7c4cffad4a5e889e2bdca9beb9942f96335aceffd08ec90657edc`.
-   Separate enabled DevBench DLL: 2,417,152 bytes, SHA-256
    `9e4455b501f0a5c6286df36fb7e8c3f1067b8542ce024e49822a1d7cbb7154f8`.
    The retained admission identified PID 2696 and runtime version
    `1.18.1+pt.1.16.1.dev.3.7f60c2b`. Its loaded virtual module path is
    not claimed as a physical backing-file measurement.
-   Exact enabled loose providers, Overwrite and unmanaged Data were checked.
    No competing DLL, manifest or reconstruction-shader provider was found.
-   Automation integration: `7b42b764456247dc5ab0d2aa723f35659da7408a` on
    automation `dev`; installed plugin `0.9.0+codex.20260916195731`.
    The refreshed direct MCP catalogue exposed the required typed NR controls.

The user explicitly authorized testing the manually launched game.
The selected legacy MO2 task profile was observed and retained; this was
not a newly prepared or isolated automation session. No profile, package,
AI, tracking or game-lifecycle changes were made.

The QASmoke interior contains a reclining character. Both native eyes are
2468 by 2740 pixels. Fixed regions cover the face, a supporting cheek
patch, armour, dark sleeve and stone floor. No suitable highlight region
was available. The first centre-only scout crop missed the character near
the bottom of the image; the full native capture did contain her.
Subsequent lower-eye inspection corrected that framing mistake.

Foveated dispatch stayed enabled with FOV 0.95, peripheral TAA off and
existing native DLSS. Character face/skin isolation was enabled, hair
disabled, appearance mix zero and maximum detail one stop. Three NR-off
baseline repeats preceded the strength comparisons; shown strengths ran
0/1/2 then 2/1/0, with a hidden control for each strength and a final
NR-off baseline. No Raw/Managed retest or model-domain conversion occurred.

## Captures and measurements

All 13 sequences contain 12 native PNG stereo pairs with explicit
`hmd_submission`, rejected fallback and `sdr_srgb` encoding.
Canonical `hmd_assess.py` checks accepted all 156 pairs: committed file
hashes/sizes, eye extents, producer/session, configuration, revision/epoch,
fresh frame attribution, inference and visible/hidden output outcomes.
The frozen plan and anonymous mapping preceded candidate capture.

These are face-region sequence means, shown minus the matching hidden
control. Luma uses encoded 0–255 values; edge RMS is a descriptive metric,
not a detail-quality verdict.

| Strength/order | Luma delta left | Luma delta right | Edge change left | Edge change right |
| -------------- | --------------: | ---------------: | ---------------: | ----------------: |
| 0 forward      |          -2.667 |           -2.573 |           +0.35% |            +0.10% |
| 1 forward      |          +0.027 |           -0.060 |           +1.84% |            +1.98% |
| 2 forward      |          +0.600 |           +0.537 |           +3.06% |            +2.44% |
| 2 reverse      |          -0.270 |           -0.151 |           +4.56% |            +3.69% |
| 1 reverse      |          -0.820 |           -0.540 |           +0.81% |            +0.88% |
| 0 reverse      |          -0.198 |           +0.184 |           +3.98% |            +3.78% |

NR-off face-luma variation spans 1.829 codes left and 1.071 right.
Strengths 1 and 2 remain within those spans relative to their controls;
their brightness shifts do not retain one sign across acquisition orders.
The first strength-zero comparison darkens more than that baseline span,
while its repeat does not. Facial pose, framing and animation remain
confounds; this discrepancy is retained, not attributed to the correction.

Strength-zero edge contrast rises almost 4% in the reverse comparison.
Ground edge contrast also changes by approximately 1.4–3.9% in the
strength-1/2 comparisons. Therefore the measured edge increases are not
specific evidence of useful neural reconstruction. The small cheek patch
is supporting evidence only, and the moving sleeve/shadow region cannot
establish shadow fidelity.

All 156 engine exposure observations resolved to a ratio of one. The 312
per-eye inference-exposure observations remain unavailable; an engine
ratio does not establish the model's exposure or colour-domain contract.

## Blinded regional assessment

Three fresh contexts reviewed anonymous originals' fixed native crops,
without settings mappings or numerical rankings. Each inspected all 26
face sheets: 13 sequences, both eyes and all 12 frames, totaling 312
eye/frame crops per reviewer. Two inspected all 78 representative
armour/shadow/ground crops; the third inspected 66 and recorded that limit.
Both acquisition-order requests were assessed, with shown/hidden
presentation order reversed. JSON assessments and input hashes were sealed
in `analysis/sealed-reviews.json` before this combined interpretation.

Across the 18 reviewer/comparison scorecards:

-   Colour fidelity: 18 ties, generally medium confidence. Warm skin,
    brown lips, dark hair and olive/gold materials remained within reference
    variation; no repeatable new colour cast was identified.
-   Useful detail: eight ties and ten indeterminate judgments. Reviewers
    saw stronger cheek stippling, hair texture or granular ground in some
    sequences, but no reliably recovered new structure. Similar enhancement
    in the strength-zero repeat is a negative control against interpreting
    this as a strength-two benefit.
-   Sampled temporal behavior: 18 ties at low confidence, with ordinary
    blinks/head movement and no clear gross sampled tone jump or persistent
    facial trail. Unobserved intervening frames remain unqualified.
-   Regional stereo consistency: 18 ties, generally medium confidence.
    The observed texture changes appeared in both eyes without an obvious
    one-eye tint or broken contour. Physical binocular viewing was not tested.

These counts summarize judgments of the same captures, not independent
scene replications or a statistical significance test. Source similarity
does not establish useful neural reconstruction. The shader's synthetic
alternating-detail correction remains proven by WARP; this run establishes
live execution and a limited colour-retention observation, not improved
HMD detail relative to the old shader.

## Scope limits and restoration

All 156 strict scout-camera checks failed; maximum absolute view-matrix
element drift was 0.016618. The gradient diagnostic flagged 1,440 of
1,560 regional rows, including unchanged controls. Those diagnostics were
preserved while continuing the user-authorized regional comparison.

The frozen scene metadata also used the display name
`Editor Smoke Test Cell` where the recorder reports editor ID
`QASmoke`, and retained a stale time pin of 2.1 versus the observed
recording-start hour 3.773441. These are plan metadata errors, not passed
protocol checks. Cell form ID 207591, interior status and weather form ID
1090112 match, with zero cell/lifecycle events. The same-cell interior and
repeated image controls support the separate exploratory analysis; the
strict scene gate remains unqualified.

Actual capture spacing was 658.909–1268.109 ms, median 703.514 ms.
This cannot qualify HMD frame-rate flicker. Complete original images are
retained, but full-image viewing exceeded the image tool's size limit;
visual findings cover fixed native crops, without resizing or colour
normalization.

The correlated recording lasted 819,454 ms, with no limit reached and no
unrecorded tail. Its four menu events were closing
`WSActivateRollover` events, not a captured journal opening.
After capture, recording and screenshot workers were verified inactive.
Original NR master/character state and colour/diagnostic settings were
restored. The user's FOV 0.95 and TAA-off fixture remains applied.

## Retained evidence and validation

The task checkout retains
`build/validation/nr-detail-character-20260916T201940Z`, including the
immutable plan/seal, every request/receipt, original PNGs/manifests,
recording, physical-provider verification, canonical metrics, anonymous
native crops, source/analysis hashes and dependency versions.

Executed from the task checkout with
`build/validation/hmd-python-deps` on `PYTHONPATH`:

```powershell
python build/validation/nr-detail-character-20260916T201940Z/analyse.py build/validation/nr-detail-character-20260916T201940Z .
python build/validation/nr-detail-character-20260916T201940Z/summarize.py build/validation/nr-detail-character-20260916T201940Z
```

Analysis used NumPy 2.4.3, Pillow 12.1.1 and jsonschema 4.26.0.
The exact commands used absolute task paths; the equivalents above are
relative to that checkout. No new production code, defaults, performance
claim or before/after old-shader HMD comparison is part of this report.
