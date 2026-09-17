# Dark-scene NR colour assessment — 17 September 2026

The early matching captures support source-colour retention at 100%
Lighting preservation in this dark scene. No correction to the current
AIO is justified by this block. Useful added neural detail remains
unestablished. This is a partial assessment, not a complete dark-scene or
full colour-quality qualification: substantial later camera drift invalidates
the original fixed-region comparisons.

## Evidence and configuration

-   Source commit: 4ef15f932ae23a3e31b6630b5d9267d88c20ab44.
-   Runtime Build ID: f7d6c4f8be8279f1380d812125a4dd072346994f330eacdbccf07e653c45403c.
-   Enabled physical CommunityShaders.dll: 24,973,312 bytes, SHA-256
    19455d12e2c195da4255c72381c7ef234e6bf8307b6fa07e3d5a92070fd38aa6.
    The DLL, adjacent build manifest, AIO receipt, runtime producer and
    reconstruction shader were checked. Exact deployment evidence, enabled
    loose providers, Overwrite and unmanaged Data checks are retained in
    deployment-verification.json.
-   Screenshot session c0a1ee1d-4125-b038-364b-c69fd340cfeb, Skyrim PID 23844.
    User-prepared QASmoke interior, one visible dark-haired character.
    The inherited directory name says two characters; only one was assessed.
-   Restarted runtime was reset to FOV 0.95, periphery TAA off, Upscaled
    Centre, batched stereo/direct commit, character mask disabled.
    Preserve Source: detailStrength 1, appearanceMix 0, maximumDetailStops 1,
    identity colour profile and manual exposure multiplier 1.
-   18 completed sequences, 12 pairs each: 216 native stereo pairs / 432
    original PNGs, 2468 x 2740 pixels per eye, SDR sRGB, hmd_submission,
    fallback reject. All artifact hashes and exact-frame configuration/
    outcome attribution checks passed. No dropped or failed acquisitions.
-   Available exact-frame exposure companions: 216; scalar status
    measured_unit_ratio, gamma exponent 1. All 312 private per-eye
    measurements report zero nonfinite values. These engine observations do
    not establish the model vendor's input colour contract.
-   Actual acquisition spacing: 470.310–569.050 ms. This is sparse sampling,
    not an HMD-frame-rate or performance measurement.
-   The user waived blinded review and requested speed. Analysis is informed,
    using canonical hmd_assess metrics and attribution checks through local
    direct-capture adapters. No image registration, white balance, histogram
    matching, exposure normalization or artificial sharpening was applied.

## Which comparisons are usable

The numerical colour comparison uses off-a and off-c as separate
per-eye references, followed by p100-forward, p50-forward and p0-forward:
60 pairs in total. The direct source/100% subset contains 36 pairs.
Regional means, both source sequence means and their ranges, per-eye
effects, sampled temporal variation and camera evidence are retained.

Journal Menu was open at frames 118458–130357. All twelve off-b frames
fall inside that interval. The menu both occludes the wall and dims the
scene; the entire sequence is excluded, including its unobstructed face.
Its replacement is preserved but belongs to the later changed viewpoint,
so it is not pooled into the early source reference.

Camera drift grows markedly after p0-forward. The original face rectangle
progressively contains armour instead of the face. Therefore raw-forward
and every subsequent sequence are excluded from comparisons against the
original fixed regions and early baseline. Their pixels and diagnostic
measurements remain preserved, not silently discarded. Later full-frame
overviews show broadly similar source/100% appearance, but are not a
quantitative repeated pass.

Across the entire run, maximum camera-element changes relative to the first
capture are view 0.20472144, projection 0.00652943, and position-adjust
44.05518 game units. At p100-forward these are 0.00526252, zero, and
1.01123 respectively; within that sequence view change is 0.00041801
and position-adjust change is 0.04834. These are recorded matrix-component
differences, not a claim about physical headset translation or its cause.

Even early unchanged source frames exceed the frozen 0.15 pixel-gradient
motion screen. Broad regional distributions and gross appearance are useful;
pixel-exact error, tiny skin-patch rankings and subtle detail conclusions
remain confounded. The exclusions are based on actual image content and
camera evidence, not solely a protocol threshold.

## Colour, shadows and stereo

Each delta below subtracts the mean of the two early NR-off sequence means
for that same eye and region. Luma is encoded 0–255, not physical luminance.

| Condition            | Face-region luma delta L / R | Dark wall-shadow delta L / R | Shadowed floor delta L / R |
| -------------------- | ---------------------------: | ---------------------------: | -------------------------: |
| Preserve Source 100% |              -0.003 / +0.013 |              +0.101 / +0.099 |            -0.013 / -0.007 |
| Preserve Source 50%  |              +2.118 / +0.972 |              -0.735 / -0.707 |            -0.518 / -0.629 |
| Preserve Source 0%   |              +0.118 / -1.660 |              -1.397 / -1.282 |            -0.725 / -0.919 |

The 100% face-region mean RGB changes are at most 0.0478 in any channel.
Early NR-off face sequence-mean luma ranges are 12.016–12.385 on the left
and 12.556–12.900 on the right. Thus the 100% face mean closely matches
the source distribution; this does not demonstrate pixel equality or
zero residual colour error. The region includes hair and surrounding
material and is not an isolated-skin measurement.

At 100%, armour luma rises by 0.330/0.351, the ornamental garment region
by 0.049/0.057, and the relatively lit diffuse floor changes by
-0.321/-0.435. Shadow clipping is largely retained, with small residual
changes: the wall-shadow black-clipping fraction rises by about
0.91/0.88 percentage points and the shadowed floor by 0.62/0.60 points.
There is no broad new black crush or white clipping in the assessed early
regions at 100%. This does not assert exact preservation of near-black
texture, and there is no strong specular-highlight test here.

Lower preservation changes the face and deepens dark surfaces. At 0%,
black-clipped floor coverage increases by 17.54/20.03 percentage points,
and wall-shadow coverage by 12.87/12.26 points. Native crops support the
larger relighting change. Lower-preservation face brightness is not
monotonic between 50% and 0%; these independently inferred outputs must
not be treated as one fixed image being linearly blended.

The clipping metric counts pixels with any RGB channel equal to zero
or 255, respectively; it does not require all channels to be black or white.

After subtracting each eye's own reference, the left/right face luma
response difference is 0.016 at 100%, versus 1.146 at 50% and 1.779 at 0%.
The early 100% crops show a consistent response in both eyes without a
gross eye-specific facial break. The stronger lower-preservation
asymmetry is a follow-up observation, not a proven stereo implementation
defect: there is no usable reverse-order replication in the same viewpoint.
Raw/Managed identity and hidden-source comparisons cannot be ranked
reliably from the later original crops.

## Detail and sampled temporal behaviour

Native source/100% crops retain the visible face, hair, armour contours
and garment ornament. No useful new neural detail is established. The
garment-region edge RMS falls by about 3.6–4.2% at 100%, whereas the very
dark floor edge RMS rises by about 11.8%. Neither result proves blur,
reconstruction quality or denoising in motion-sensitive, near-black
regions. Do not reinterpret edge energy as an improvement score.

All twelve early source and 100% face crops were reviewed for both eyes,
along with native first/middle/last region sheets and the early
shadow/highlight comparisons. No coarse facial popping was identified in
those sequences. Temporal face-region luma standard deviation is
0.336–0.403 for NR off and 0.265–0.281 at 100%; the sampled evidence does
not show added slow brightness instability. Approximately two samples per
second can miss faster flicker. HMD-rate stability, moving-object ghosting,
and deliberate illumination-change recovery remain untested.

## Recording coverage and orderly completion

The activity recorder reached its configured 1,800,000 ms limit. The stop
receipt reports elapsed 2,507,102 ms and unrecorded tail 707,102 ms
(11 minutes 47.102 seconds). Its last tracking sample is frame 221688.
The five sequences p100-reverse, hidden-b, p100-return, off-return and
off-replacement are outside that retained trace. Their screenshot-side
frame/configuration/camera evidence is intact, but complete late menu and
input histories are unavailable. The source recording was copied with
matching size and SHA-256, and remains in recording.json.

Every owned screenshot sequence reached its completed terminal state.
The recording stop and restoration of enabled NR / Preserve Source 100%
were acknowledged before Skyrim closure was permitted. Two redundant final
status queries later failed transport; their exact errors are retained.
They do not turn already completed captures into active ones. No game
restart or live action was attempted after the user was told it could close.

## Decision and next action

Keep this AIO and Lighting preservation at 100%. Bright-scene evidence
and the usable early dark comparison support colour preservation within
their stated limits. No production correction, shader build or replacement
archive was made from this assessment.

The next focused test should be a short dark-scene NR-off / hidden-source /
shown-100% / NR-off bracket, captured consecutively before offline image
analysis. It should resolve retained versus added detail and confirm a
return reference without allowing long capture gaps. Lower-preservation
stereo relighting is a secondary follow-up. Fast motion, illumination
recovery, strong specular light and the character-mask route are still
outside the completed evidence.

Capture orchestration took too long for the requested fast run. Local
automation feedback AUTO-20260917-091725002-203872C0 records the concrete
improvement: bounded sequential capture, camera/recording-deadline checks,
and offline analysis after acquisition. This is an orchestration follow-up,
not a demonstrated shader defect. Nothing was published externally.

## Reproduction and result interpretation

Executed successfully:

-   analyse.py <worktree> <evidence-directory>: all 216 pairs decoded,
    hashed and attributed; zero attribution errors.
-   adjudicate.py <worktree> <evidence-directory>: evidence-based exclusions,
    early per-eye references/effects, recording coverage and native review
    grids generated.
-   Physical deployment verification reused the existing bounded verifier;
    identity agrees with the screenshot producer and AIO receipt.

results/means.json, baseline.json, regions.json, temporal.json and
summary.json are unfiltered acquisition diagnostics and include confounded
sequences. They are not the assessment verdict. Use
results/early-baseline.json, early-source-relative-effects.json,
early-source-relative-stereo.json, comparison-exclusions.json and
qualified-summary.json for the interpreted comparison. The obsolete
unexecuted all-source pooling summariser was removed.

The two baseline-regions preview PNGs were generated before the baseline-only
highlight rectangle correction. They are stale previews; regions.json and
the analysis crops use the corrected rectangle. Originals remain unchanged.

## Preserved local evidence

All evidence paths and filenames above are relative to this worktree's
build/validation/nr-dark-two-characters-20260917T081914Z/
directory unless an explicit path is given. Original PNGs, manifests,
receipts and derived metrics remain there; raw evidence is not versioned.

-   Original assessment SHA-256: e782b55a750c4d17c4158e813bde6cab601f5f2cbb86b467899e515108db69cb.
-   Campaign index SHA-256: ce00847b09b930305747bea110d288d7cc78477dfb0d2bf7d5fd2dc90756e96a.

The campaign index identifies the preserved inputs and producer provenance.
This published report retains the original findings and limitations.
