# Bright-scene NR colour assessment — 17 September 2026

The 100% Lighting preservation setting substantially preserves source colour
and lighting in this scene. Reducing it consistently admits the model's
relighting. Useful added neural detail at 100% is not established. No
implementation defect is demonstrated by these captures.

## Evidence and configuration

-   Current AIO source: 4ef15f932ae23a3e31b6630b5d9267d88c20ab44.
-   Runtime Build ID: f7d6c4f8be8279f1380d812125a4dd072346994f330eacdbccf07e653c45403c.
-   Physical enabled DLL: 24,973,312 bytes, SHA-256
    19455d12e2c195da4255c72381c7ef234e6bf8307b6fa07e3d5a92070fd38aa6.
    DLL, adjacent manifest, AIO receipt, runtime producer and reconstruction
    shader agree. Enabled loose providers, Overwrite and unmanaged Data were
    checked; the separate DevBench DLL identity is retained.
-   User-prepared QASmoke scene, two stationary characters, broad FOV route,
    0.95 FOV, periphery TAA disabled, DLSS configured as before, Upscaled Centre,
    batched stereo/direct commit, character masking disabled.
-   Preserve Source: detailStrength 1, appearanceMix 0, maximumDetailStops 1;
    manual exposure multiplier 1, identity profile, no transport bypass.
-   17 actual native stereo sequences, 12 pairs each: 204 pairs / 408 PNGs.
    All scheduled acquisitions completed; zero failures or drops.
    PNG dimensions 2468 x 2740 per eye, SDR sRGB, source hmd_submission,
    fallback reject. Artifact hashes and frame attribution verified.
-   Four NR-off sequences, including three before candidates; 100/50/0%
    Preserve Source plus Raw and Managed identity in forward/reverse order;
    two display-only source controls with inference running; final 100% return.
-   Actual spacing was 699.333–824.180 ms, despite requested 500 ms.
    These are acquisition diagnostics, not performance measurements.
-   All 204 acquisitions retain available exact-frame engine exposure
    companions; their scalar status is measured_unit_ratio and exponent 1.
    All 312 private per-eye measurements have zero reported nonfinite values.
    Engine observations are not claims about NVIDIA's input colour contract.
-   Frozen regions include both full faces, small skin patches, armour, cloth,
    floor, stone recess and lit stone. Every eye uses its own original reference.
    No registration, white balancing, histogram matching or normalization.
-   The user waived blinded review and requested a fast comparison. This is
    an informed accelerated assessment, not the full randomized three-repeat,
    independently blinded protocol. Numerical analysis uses the maintained
    hmd_assess metrics and attribution checks, with a direct-capture adapter.
    NR-off retains its configured Preserve Source setting and is explicitly
    checked for disabled inference; it is not rewritten as legacy_raw.

## Separate findings

### Colour and lighting

In the full face crops, all three 100% runs differ from the mean of the
NR-off sequence means by at most 0.577 encoded-luma values and 0.604 in any
mean RGB channel, on a 0–255 scale. The unchanged baseline sequence-mean
luma spans are about 0.68–0.70 for the dark-haired character and 0.28 for the
blond character. Small differences are comparable with scene variation;
this does not establish exact pixel equality or zero colour error.

| Condition              | Dark-haired face luma change, both eyes/repeats | Blond face luma change, both eyes/repeats |
| ---------------------- | ----------------------------------------------: | ----------------------------------------: |
| Preserve Source 100%   |                                  +0.28 to +0.58 |                            -0.15 to +0.26 |
| Preserve Source 50%    |                                  -2.21 to -1.25 |                            -5.35 to -5.00 |
| Preserve Source 0%     |                                  -4.33 to -2.87 |                           -10.19 to -9.95 |
| Raw / Managed identity |                                  -4.25 to -2.93 |                           -10.24 to -9.94 |

The 50%/0% darkening is much larger than baseline variation and repeats in
both directions. Lower preservation visibly deepens facial/cloth shadows,
and Raw/Managed identity also change the facial appearance. For example,
near-black coverage in the left dark-haired full-face crop increases from
22.4% with NR off to 30.9% at 0%; 100% gives 21.7%. These are full crops
including hair and surroundings, not isolated-skin measurements.

At 100%, fixed material/stone mean-luma changes are small (generally under
0.45 values), with preserved colour and diffuse highlights. No white-channel
clipping was measured in the assessed full-face/material regions. Bright
specular reflections and strongly clipped illumination are not represented.

### Useful detail

Native crops retain hair strands, embroidery and armour structure at 100%;
no gross new halo, broken facial feature or invented repeated material was
identified in the reviewed crops. Clear useful _added_ facial detail is not
established. Raw/lower-preservation results introduce stronger facial and
shadow changes, which cannot be counted automatically as improved detail.

Some stone/floor fine texture is attenuated: at 100%, floor edge RMS is
about 2.6–3.9% below the mean NR-off reference; the nearby hidden-source
comparisons likewise show a small decrease. Cloth edge energy rises slightly.
Neither measurement alone proves useful reconstruction, blur severity or
denoising. Visual differences are small at native size.

Camera micro-motion remains: maximum view-element change 0.00169745,
position-adjust change 0.246216 game units, projection unchanged.
Fixed-pixel gradient mismatches exceed the preregistered 0.15 screen even
between unchanged references. Pixel-exact detail/error and tiny-skin-crop
rankings therefore remain motion-limited. Regional colour distributions,
large repeatable relighting and gross structure remain informative. No
candidate was warped to improve its comparison.

### Sampled temporal behaviour

No added slow brightness instability is demonstrated at 100%.
Across both eyes, full-face temporal luma standard deviations are:

| Region           |      NR off | Preserve Source 100% |
| ---------------- | ----------: | -------------------: |
| Dark-haired face | 0.088–0.188 |          0.082–0.134 |
| Blond face       | 0.150–0.194 |          0.166–0.186 |

All frames were measured. Visual review covered native first/middle/last
crops across conditions and complete twelve-frame face grids for the source
and final 100% return. No coarse facial popping was identified there.
This does not qualify HMD-rate flicker, moving-object ghosting or exposure
recovery under a deliberate illumination change. Captures sample roughly
1.2–1.4 frames/second and can miss faster changes.

### Stereo

After subtracting each eye's own mean NR-off reference, the largest
difference between left/right full-face mean-luma changes at 100% is
0.0596 code values. The visible face response is consistent in both eyes,
and no gross eye-specific facial structural break was identified in the
reviewed crops. Lower preservation can expose unequal per-eye relighting
(for example roughly one extra luma value of darkening in the right
dark-haired face at 0%). Natural disparity and view-dependent materials
prevent treating arbitrary cross-eye pixel differences as defects.
This is not a physical headset/lens or exhaustive disparity qualification.

## OBody and exclusions

The activity recording places CustomMenu opening at engine frame 341124
and closing at 347978. The overlay covers the fixed wall-shadow crop during
managed-reverse, raw-reverse, p0-reverse and p50-reverse: 96 per-eye regional
rows are explicitly excluded in results/regional-exclusions.json. Their raw
measurements and originals are retained. The character and other selected
material regions remain unobstructed; inference and attribution stayed valid.
Forward wall-shadow observations and all 100% wall-shadow observations remain
usable. The later reopening occurred after all capture work ended and was
closed again; only HUD Menu remained.

## Next action and remaining coverage

Keep this AIO and the 100% setting for the dim-scene check. There is no
evidence-driven correction to implement from this bright block alone.
Use a dim scene with a visible face, partly lit skin, dark cloth/armour and
stone. Repeat the source/100% comparison, add 50%/0% where needed, and
inspect shadow retention, residual tint, texture attenuation and both eyes.

Bright-scene fidelity and sampled stereo/temporal behaviour are supported
within the stated limits. Useful added detail remains unresolved. Dim
lighting, a deliberate exposure-recovery event, fast motion, HMD-frame-rate
flicker, strong specular highlights and the separate character-mask route
remain outside this block. Capture and recording ownership are inactive;
runtime NR was restored to Preserve Source 100%, FOV 0.95, TAA off.

## Preserved local evidence

All evidence paths and filenames above are relative to this worktree's
build/validation/nr-bright-two-characters-20260917T0723Z/
directory unless an explicit path is given. Original PNGs, manifests,
receipts and derived metrics remain there; raw evidence is not versioned.

-   Original assessment SHA-256: 20a5ee83dfa4a42f024206727dba04a480e3687b03837e29828ce7e9e8778924.
-   Campaign index SHA-256: aa38dbb42baab2adde7321057e830cfd856b3a5f8879a19cdd361c1bdf8b3e05.

The campaign index identifies the preserved inputs and producer provenance.
This published report retains the original findings and limitations.
