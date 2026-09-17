# NR colour: complete HMD investigation record

This is the consolidated record of the colour investigation through
17 September 2026, archived on 18 September. It connects the measurements,
native-image reviews, rejected attempts, implementation decisions and
remaining questions. The existing per-run reports retain their detailed
findings; the new [measurement archive](nr-colour-campaign-20260918/README.md)
also commits the underlying structured results and review records.

The supported current choice is Preserve Source with Lighting preservation
100%, detail contribution 1, Appearance Mix 0 and the existing one-stop
limit. Bright-scene results and the usable early dark-scene comparison
support regional colour retention. Useful additional neural detail is still
unestablished. No complete colour-quality, physical-display, moving-scene
or HMD-frame-rate qualification is claimed.

## What is now preserved in Git

The archive covers 23 retained evidence folders: admission attempts,
stationary-baseline retries, exploratory comparisons, the detail-strength
campaign, the Dragonsreach follow-up, the same-frame numerical investigation,
corrected packed-output testing, preliminary slider checks, and both final
lighting campaigns. It includes:

-   Complete per-frame/per-eye/per-region numerical tables, aggregates,
    baseline envelopes, temporal and stereo measurements, clipping and
    gradient diagnostics.
-   Complete available image-review records, anonymous mappings, selection
    policies, coverage limits, exclusions, interrupted and superseded analyses.
-   Frozen configuration/region/plan records, exact-frame private/exposure
    diagnostics, build/deployment evidence, acquisition and cleanup receipts.
-   Recording metadata and activity events, including explicit recording
    limits and missing tails; the continuous raw pose/tracking arrays stay local.
-   Task-local capture/analysis source text and the exact source-file
    byte lengths and SHA-256 hashes.

Native PNGs, binaries and full activity traces remain local. Raw acquisition
manifests are indexed by size/hash instead of copying raw evidence trees.
The archive preserves original numbers and qualifications; it does not
reclassify old failures or rerun analysis with new thresholds.

Earlier null-HMD live work is covered by the linked committed reports.
Its old raw directory was not available at the recorded C:/src path or
the equivalent current root/worktree locations. Those published results
are retained, but a complete new copy of those earlier raw measurements
is not claimed. No fresh game session or build was started for this archive.

## Investigation timeline and decisions

| Stage                                      | Measurements and image findings                                                                                                                                                                         | Decision and detailed record                                                                                                                                                                                              |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Early live exposure/transport work, 15 Sep | Null-HMD numeric transport and inference at both insertion points; multi-ROI, exposure and transition/readback failures recorded separately                                                             | Instrumentation and bounded recovery corrections; no physical-headset colour verdict. See [earlier live context](#earlier-live-context).                                                                                  |
| First physical-HMD attempt, 16 Sep         | Scout and baseline wrote 24 stereo pairs; only one had fresh camera provenance, with the other 23 retaining an old camera frame                                                                         | Stop comparative attribution; repair the camera upload observer. [Attempt](nr-colour-hmd-attempt-20260916.md), [repair](nr-camera-upload-fix-20260916.md).                                                                |
| Indoor/outdoor baseline retries            | Menu contamination, actor/blink changes, sunlight variation, stale scout pins and an overly small coordinate tolerance were separately recorded; repeated NR-off images also failed the gradient screen | Keep failures as evidence, avoid blaming NR or requiring controller wake solely from those numbers. [Retests](nr-colour-hmd-retest-20260916.md).                                                                          |
| Lower-sunlight and QASmoke retries         | 36 pairs each in the lower-sun block, initial QASmoke block and fresh-reference block; the fresh QASmoke block passed attribution but failed 255/288 gradient comparisons                               | A fresh camera pin corrected the admission ordering; a visible blink and right-wall black-border crop remained confounds. [QASmoke](nr-colour-qasmoke-retest-20260916.md).                                                |
| Brighter exploratory comparison            | 204 pairs, Raw/Managed/Preserve Source shown and hidden in both orders; Raw/Managed faces looked paler, while Preserve Source retained source appearance                                                | Prefer Preserve Source colour retention; useful new detail not established. [Comparison](nr-colour-brighter-comparison-20260916.md).                                                                                      |
| Alternating-detail investigation           | Native-image metrics did not establish added detail; controlled shader inputs proved that every-second-pixel taps cancelled alternating residuals                                                       | Use adjacent taps. Synthetic proof is separate from face-image improvement. [Investigation](nr-colour-detail-investigation-20260916.md).                                                                                  |
| Corrected detail-strength campaign         | 156 pairs at strengths 0/1/2 with hidden controls; 18 review scorecards gave colour/stereo ties; useful detail had eight ties and ten indeterminate judgments                                           | Keep the existing detail default; edge-energy increases also occurred in zero-strength/background controls. [HMD comparison](nr-colour-detail-hmd-comparison-20260916.md).                                                |
| New Dragonsreach follow-up                 | 40 new pairs; source hue was better retained by Preserve Source, but faces darkened 2.37–4.41 luma codes; zero-strength private output was exact                                                        | Investigate the retained darkening without calling sequential image changes a proven shader defect. Full report and metrics are in the follow-up archive shard.                                                           |
| Same-frame private investigation           | 64 complete batches / 128 eye measurements; shown Preserve Source private luma residual about -0.6%, all hidden/zero controls exact                                                                     | Packed-output bias reproduced on WARP; explicit nearest rounding corrected the synthetic bias. [Correction](nr-colour-packed-rounding-fix-20260917.md), [review](nr-colour-packed-rounding-review-20260917.md).           |
| Corrected packed-output HMD test, 17 Sep   | 26 pairs; three menu-contaminated initial pairs excluded; 28 complete private batches showed roughly -0.027% to -0.064% residual, exact hidden controls                                                 | Retain correction; different scenes prevent a controlled before/after improvement percentage. [Live report](nr-colour-packed-rounding-live-20260917.md).                                                                  |
| Capture evidence retention                 | Deferred diagnostics had expired: 0/26 engine companions in the preceding test, plus private batches without exact screenshot joins                                                                     | Retain exact capture-owned companions. Later bright/dark runs retain all 420 engine companions and 624 private eye measurements, with no reported nonfinite samples. [Retention implementation](nr-capture-retention.md). |
| Lighting preservation slider               | Default 100% retains compatibility; 50% and 0% admit more of the bounded smooth neural brightness residual                                                                                              | Shared CSX control/shader change with typed DevBench access; no separate DevBench rebuild. [Implementation and validation](nr-lighting-preservation-20260917.md).                                                         |
| Bright slider campaign                     | 204 pairs, two characters; small 100% colour changes and larger repeatable relighting at lower preservation                                                                                             | Keep 100%; mask-off broad-FOV route, limited detail and sparse temporal coverage. [Bright report](nr-lighting-bright-assessment-20260917.md).                                                                             |
| Dark slider campaign                       | 216 pairs acquired; one character; 60 early pairs support regional comparisons, including a 36-pair source/100% subset                                                                                  | Exclude the journal sequence and later comparisons whose original face crop becomes armour. Keep 100%; complete the focused detail test later. [Dark report](nr-lighting-dark-assessment-20260917.md).                    |

Counts above refer to the stated comparison blocks; scouts, repeated imports
and later reanalysis of the same images must not be added as independent
replications. The archive also retains the interrupted brighter-scene
attempt, detail admission/scout work and preliminary slider observations.

The original [live camera-hook inspection](nr-colour-campaign-20260918/supporting/live-map-hook-inspection.json)
is also preserved with its source hash in the archive index. It records the
read-only process inspection behind the attribution repair, separately from
the image-comparison payloads.

## Key colour measurements

Encoded HMD luma is 0.2126R + 0.7152G + 0.0722B on 0–255 output values.
It is not physical luminance. Each eye uses its own source reference.
Private GPU statistics use native texture units and a different sampled
domain; the two kinds of result must remain separate.

### Brighter scene before the detail/rounding corrections

Shown minus nearby inference-running hidden control:

| Mode             |   Forward L / R |   Reverse L / R |
| ---------------- | --------------: | --------------: |
| Raw              | +4.311 / +4.173 | +6.037 / +6.633 |
| Managed identity | +4.361 / +4.455 | +6.047 / +6.933 |
| Preserve Source  | -0.161 / -0.347 | -0.248 / -0.297 |

NR-off face sequence means spanned 4.243 codes left and 3.017 right.
Independent native-crop reviewers preferred source colour and retained
facial detail to Raw/Managed in both orders. Preserve Source tied source
variation; no useful extra detail was established. Forehead drift was
much larger and that tiny crop was not an independent quantitative proof.

### Dragonsreach follow-up on the adjacent-tap correction

| Mode             |   Forward L / R |   Reverse L / R |
| ---------------- | --------------: | --------------: |
| Raw              | +1.402 / -0.750 | -1.214 / -3.556 |
| Managed identity | -1.828 / -3.991 | -1.387 / -3.581 |
| Preserve Source  | -4.258 / -4.412 | -2.496 / -2.369 |

Initial source face spans were 1.193 / 1.219 codes. Preserve Source
retained warmer hue more closely but did not pass unchanged-brightness
comparison. Changed pose, lighting and camera remain relevant. The separate
private same-frame analysis established a real numerical bias without
claiming it explained the entire face difference.

All sixteen shown Preserve Source private eye measurements had negative
mean RGB changes, with luma -0.571% to -0.611% left and -0.591% to
-0.621% right, zero invalid/nonfinite samples, and exact hidden controls.
No private batch exactly joined a saved screenshot's immutable key.
The corrected-build private residual was -0.038274% to -0.026917% left
and -0.064162% to -0.042593% right in a different scene.

### Current slider build: bright scene

| Setting                | Dark-haired face delta, both eyes/repeats | Blond face delta, both eyes/repeats |
| ---------------------- | ----------------------------------------: | ----------------------------------: |
| Preserve Source 100%   |                            +0.28 to +0.58 |                      -0.15 to +0.26 |
| Preserve Source 50%    |                            -2.21 to -1.25 |                      -5.35 to -5.00 |
| Preserve Source 0%     |                            -4.33 to -2.87 |                     -10.19 to -9.95 |
| Raw / Managed identity |                            -4.25 to -2.93 |                     -10.24 to -9.94 |

At 100%, maximum full-face mean-luma change was 0.577 codes and maximum
mean-RGB-channel change 0.604. The separately referenced left/right
face-response difference was at most 0.0596. OBody overlap excluded
96 wall-shadow regional rows; unobstructed character regions remained useful.
Fine floor/stone edge RMS attenuation around 2.6–3.9% was descriptive,
not proof of harmful blur or beneficial denoising.

### Current slider build: usable early dark block

| Setting | Face-region delta L / R | Dark wall-shadow delta L / R |
| ------- | ----------------------: | ---------------------------: |
| 100%    |         -0.003 / +0.013 |              +0.101 / +0.099 |
| 50%     |         +2.118 / +0.972 |              -0.735 / -0.707 |
| 0%      |         +0.118 / -1.660 |              -1.397 / -1.282 |

At 100%, the largest face mean-RGB-channel change was 0.0478, and the
separately referenced stereo response difference was 0.016 luma codes.
The face region includes hair and surroundings, not isolated skin.
Lower preservation changed shadows more strongly; at 0%, shadowed-floor
pixels with any zero RGB channel increased by 17.54/20.03 percentage
points. Neither face brightness nor neural output is a fixed monotonic
linear blend across independent sequential inference frames.

Later camera drift reached view-element change 0.20472144 and
position-adjust change 44.05518 game units; the old face crop no longer
represented the face. Those rows remain in raw diagnostics, but are not
pooled with the early reference. The recorder's 30-minute cap left
707,102 ms unrecorded; five late sequences lack full activity coverage.
Exact screenshot-side attribution survives that separate recording gap.

## Build attribution and scope

| Live stage                              | Compiled source                          | Runtime Build ID                                                 |
| --------------------------------------- | ---------------------------------------- | ---------------------------------------------------------------- |
| Initial stale-camera attempt            | 2eef86720e5c6a48e6fa9ad93506a1f2093d0f2f | 80fb139bfbeec23b32ebea94b98cbc5053f6c7f914e31aecdf1fd22cc6fe5003 |
| Camera repair / brighter comparison     | e3e731a83ff72387bdeebdae5b038cf87d826656 | 4385d3607500f6dd1006d3c90bd9a15b89eb193ab3795b4300b9155ec37c38e9 |
| Adjacent-tap detail correction          | 7f3f6279c6c43f8998a6ff43bfe51d98ae70d57d | 24edcea53076fd092f82f2559cdff2a299e797960b2637a2b018db775a906f83 |
| Packed-output corrected AIO             | d222831ad99e1dcd94c41cda9c6cd2fa160e8e3a | 4e39bf89befed28f88214434a80a5dd45cfe9e61ad1426a18f125ee6f5ebc41c |
| Capture retention / lighting slider AIO | 4ef15f932ae23a3e31b6630b5d9267d88c20ab44 | f7d6c4f8be8279f1380d812125a4dd072346994f330eacdbccf07e653c45403c |

The detailed reports/archive retain physical DLL sizes/hashes, matching AIO
receipts, shader identity, producer/session identity and deployment checks.
Report commits are not substituted for compiled source identities.
The same source build appearing in different scenes is not a matched A/B.

The requested FOV 0.95 and peripheral TAA off were retained in the
subsequent campaigns; native DLSS is itself temporal. Character-mask and
broad-FOV comparisons have distinct coverage. Early reviewers were blinded;
the user later waived blinding for speed. Interrupted reviews and generated
tiles are not counted as completed visual inspection.

The archived numerical shader proofs concern adjacent-tap response and
packed storage, not proof of useful facial reconstruction. These focused
corrections did not reinstate permanent D3D11 context protection or change
the ownership intent of 5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5.
The linked implementation/review records describe their exact scope.

## What remains open

1. Establish useful added detail at the current 100% setting and determine
   whether small texture attenuation is visually material.
2. Finish a short stable dark NR-off / hidden-source / shown-100% / NR-off
   bracket, with a valid nearby return reference.
3. Test HMD-rate flicker, moving-object ghosting and deliberate
   illumination-change recovery.
4. Replicate lower-preservation left/right relighting differences before
   calling them a stereo implementation defect.
5. Cover strong specular highlights and the current character-mask route
   where relevant. Earlier character-mask runs do not qualify the later
   lighting slider across that route.
6. Keep acquisition consecutive, monitor camera/recording deadlines, and
   defer image analysis until capture ends. Existing controls support this;
   an AIO rebuild is not required. Local workflow feedback is
   AUTO-20260917-091725002-203872C0.

No newer shader fix, production colour-domain selection, additional AIO
or performance gain is established by this archival commit.

## Earlier live context

These existing reports preserve the earlier numerical/runtime work and its
limits; null-HMD success is not physical-headset image-quality validation.

-   [Initial null-driver retest](nr-colour-null-driver-retest-20260915.md)
    and [c45eb42 retest](nr-colour-null-driver-c45eb42-20260915.md).
-   [Exposure follow-up](nr-colour-exposure-followup-20260915.md).
-   [Multi-ROI retest](nr-colour-multi-roi-retest-20260915.md) and
    [four-region/readback retest](nr-colour-readback-retest-20260915.md).
-   [Insertion-transition reproduction](nr-colour-transition-reproduction-20260915.md),
    [history/exposure correction](nr-colour-history-exposure-fix-20260915.md),
    and [draw-observation correction](nr-colour-draw-observation-fix-20260915.md).

## Archive verification

The original archive verifier compared every exported payload with a fresh
read of its preserved source file, including byte length, SHA-256 and complete
JSON field values or full text. False, zero, null, empty arrays and empty
objects remain distinct. Recording summaries explicitly identify the raw
arrays kept local. An incomplete preliminary JSON sequence index is retained
as text with its parse error, not dropped or interpreted as a complete index.

Local NR DLL paths are removed from the published payloads, including
embedded serialized JSON. The privacy audit verifies that all other payload
bytes remain unchanged. Source hashes identify the private originals;
archive hashes identify the sanitized exports. The index lists the affected
records, and the validation receipt distinguishes the two audits.

The [archive index](nr-colour-campaign-20260918/archive-index.json),
[source inventory](nr-colour-campaign-20260918/source-inventory.jsonl)
and [validation receipt](nr-colour-campaign-20260918/validation.json)
record exact coverage. These checks establish archival fidelity, not a
new visual pass. The original image/measurement evidence is unchanged.
