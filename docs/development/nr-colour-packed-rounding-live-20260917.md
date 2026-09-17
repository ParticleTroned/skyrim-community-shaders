# Corrected Preserve Source: stationary QASmoke HMD check

The corrected build supports retaining the packed-rounding fix. Its private
same-frame reconstruction residual was -0.027% to -0.064% mean luma, with
exact hidden controls and no invalid samples. The earlier uncorrected
scene measured about -0.6%; these are different scenes, so this is
corroboration of the numerical correction, not a controlled improvement
percentage.

Final face brightness did not shift consistently in both comparison
orders. This run does not establish a useful-detail improvement, complete
colour neutrality, or headset-rate temporal stability. No further
production correction is justified by these measurements.

## Build and scene

-   Compiled source: `d222831ad99e1dcd94c41cda9c6cd2fa160e8e3a`,
    clean Release, DevBench bridge enabled.
-   Build ID:
    `4e39bf89befed28f88214434a80a5dd45cfe9e61ad1426a18f125ee6f5ebc41c`.
-   Runtime: Skyrim VR, QASmoke, screenshot service session
    `606d0e9b-dbc2-8904-6393-0e4f7c531da8`.
-   Installed AIO:
    `CSX_AIO-CommunityShaders-NR-packed-rounding-DevBench-noFOMOD-d222831ad-20260917T002543Zfog-color`.
-   Physical DLL: 24,922,112 bytes, SHA-256
    `c5cd3269195ca8911b3c7cad7cd91e5056ca7e3513f79ef82e7750620741c324`.
    It matches the adjacent manifest, AIO receipt and runtime producer.
-   Corrected reconstruction shader: 3,931 bytes, SHA-256
    `952ee5f4e5cc279463f93f3f8ac89a1d78a0bf179a74d1968eb9adcd7625d92e`.
    Exact enabled loose-provider checks found the AIO as its only provider.
    Overwrite and unmanaged Data contained no competing DLL, manifest or
    reconstruction shader.
-   FOV 0.95, peripheral TAA disabled; existing DLSS configuration retained.
    Character face/skin selection enabled, hair disabled, single private
    region per eye, authored selection mask and visual isolation enabled.
-   Preserve Source: detail strength 1, appearance mix 0, maximum detail
    stops 1, identity transform, unknown domain, manual exposure multiplier 1.
    No inferred NVIDIA colour-domain contract or exposure correction.
-   The character was at the lower right of the mirror. Native eye framing
    contained the face. The user confirmed stationary HMD and explicitly
    waived awake controllers and blinded review.

The previous [implementation](nr-colour-packed-rounding-fix-20260917.md)
and [adversarial review](nr-colour-packed-rounding-review-20260917.md)
remain the numerical and scope evidence. This run changes no production
code, defaults, context protection or renderer ownership.

## Acquisitions and attribution

Six sequences saved 26 native stereo pairs, 52 PNGs, at 2468 x 2740 per
eye, `hmd_submission` with fallback rejected and identical
`sdr_srgb` encoding. Every requested pair was written; no acquisition
failure or drop was reported. Original PNG and manifest sizes/hashes
were verified.

| Phase                         | Pairs saved | Frames        | Assessment use                      |
| ----------------------------- | ----------: | ------------- | ----------------------------------- |
| NR off                        |           6 | 82809-83198   | Last 3 only                         |
| Preserve Source hidden        |           4 | 141978-142112 | All 4                               |
| Preserve Source shown         |           4 | 147454-147587 | All 4                               |
| Preserve Source shown return  |           4 | 149171-149305 | All 4                               |
| Preserve Source hidden return |           4 | 154355-154489 | All 4                               |
| NR off return                 |           4 | 159925-160188 | All 4, separate inactive-mode audit |

The Journal menu opened at frame 82030 and closed at 82971. Initial
reference ordinals 1-3 fall inside that interval and are excluded, with
their originals retained. Thus 23 pairs remain useful, including all 16
Preserve Source comparisons. The large apparent brightness difference
against those first three references is menu contamination, not NR.
Menu observations before and after the comparison block showed HUD only.

Frozen requested configuration and per-eye outcomes verified all four
Preserve Source phases: fresh same-frame evidence, successful inference
in both eyes, committed shown output, and hidden model output withheld.
All acquired camera view, projection and position-adjust arrays were
identical to the initial reference, with maximum element difference zero.
Controller sleep is not an exclusion.

The canonical importer accepted the initial reference and all Preserve
Source pairs. It rejected the four return references because its
`nr_off` condition expects the inactive mode label `legacy_raw`.
This targeted return retained `preserve_source` while disabling NR.
A separate audit verified exact configuration, revision 5, epoch 1,
same-frame stereo source, no inference, no output/pipeline commit,
empty physical regions, native source planes and committed PNG integrity.
The four canonical failures remain in the evidence; they were not
silently changed into importer passes.

Requested sequence spacing was 100 ms. Actual spacing was
592.252-765.904 ms, leaving 42-90 engine frames unobserved between
saved pairs. This is sampled image evidence, not a frame-rate flicker test.

## Private same-frame reconstruction

All 28 retained complete batches passed the existing immutable batch
validator: 56 eye measurements, 32 hidden and 24 shown. All have zero
invalid forward/inverse and nonfinite samples. Measurements compare
source and reconstructed result at the same sampled private pixels.

| Display condition | Left measurements |    Left mean-luma change | Right measurements |   Right mean-luma change |
| ----------------- | ----------------: | -----------------------: | -----------------: | -----------------------: |
| Hidden            |                16 |                       0% |                 16 |                       0% |
| Shown             |                12 | -0.038274% to -0.026917% |                 12 | -0.064162% to -0.042593% |

Hidden mean absolute differences were exactly zero. Shown mean absolute
differences were 0.000438-0.000483 left and 0.000562-0.000607 right.
These are private texture-domain values before final character masking;
they are not face-only measurements or HMD display code differences.

The earlier private residual was -0.571% to -0.611% left and -0.591%
to -0.621% right. Together with the controlled GPU regression, today's
smaller residual supports the rounding correction. Nearest quantization
does not require zero signed error for every image. Different scenes
prevent treating the two live ranges as a controlled before/after test.

No retained private batch exactly joined a screenshot's immutable
batch/revision/generation/frame/slot key. The private results therefore
remain independent evidence, not an explanation assigned to a particular
saved face image.

## Final face, material and stereo observations

The face and control rectangles were fixed from reference images before
viewing corrected output. Each eye used its own native crop. Direct
review inspected every saved face crop and representative floor, armour
and shadow crops. Blinded review was omitted at the user's instruction.

All whole-face pairs, including a blink in hidden ordinal 4, remain in the
primary shown/hidden means. Encoded luma is 0.2126R + 0.7152G + 0.0722B
on 8-bit output codes; it is not linear luminance.

| Comparison, shown minus hidden | Left face luma | Right face luma | Left edge RMS | Right edge RMS |
| ------------------------------ | -------------: | --------------: | ------------: | -------------: |
| Forward                        |      -0.696099 |       -0.682626 |     +0.130772 |      +0.148425 |
| Reverse                        |      +0.335219 |       +0.415627 |     +0.062283 |      +0.050358 |

The brightness direction reverses with acquisition order. Clear NR-off
face ranges were 47.354691-49.652756 left and 48.948057-51.179481 right,
wider than these differences. Returning to NR off changed face means by
-0.709388 left and -0.650536 right relative to the clear initial
reference. Hidden-return means also changed by -0.759517 and -0.894242.

Floor differences were small: forward +0.023957/+0.051476 codes,
reverse -0.025333/+0.086261, left/right respectively. Armour differences
changed sign between orders; sleeve-shadow differences remained small.
This does not establish a uniform global lighting cause for all facial
variation. The blink and residual character/shading variation matter
despite the exactly stationary camera.

Facial features, armour motifs and floor detail remain recognizable in
both eyes. No gross colour cast, missing eye or committed-output mismatch
was observed. A slight edge-RMS increase cannot establish useful neural
detail: clear NR-off edge variation was larger, and no repeatable new
facial feature was demonstrated. The gradient disagreement diagnostic
also exceeded the frozen 0.15 screen (face 0.343559-0.680219 against
clear reference ordinal 4); exact pixel-detail fidelity is not qualified.
This diagnostic does not discard valid same-frame private measurements
or prevent the limited brightness comparison.

## Evidence limitations and cleanup

Exact engine-exposure companion lookup returned 0 of 26 available.
It was requested after capture and restoration; pending frozen stamps
were not replaced with newer exposure readings. This collection missed
the required drain/retention window. Manual identity exposure was used
for reconstruction, so there is no verified captured-exposure binding
or exposure-recovery result.

The owned 50-ms activity recording reached its 600,000-ms cap. Stop
reported 959,328 ms elapsed and a 359,328-ms unrecorded tail. It contains
9,798 pose/tracking samples and preserves the initial menu event.
Later image camera evidence remains valid, but continuous activity/menu
coverage is incomplete. No continuous-pose pass is claimed.

Owned screenshots and recording were finalized and verified inactive.
NR master and character rendering were restored off; original Raw colour
settings and diagnostic flags were restored. The user-requested FOV
0.95 and peripheral-TAA-off settings remain. No scene navigation, game
restart or additional build was performed.

The next automation improvement is capture-owned retention of exact
private-batch and exposure companions, plus menu-aware acquisition and
adequate recording duration. Local feedback item
`AUTO-20260917-030553858-50A78A4D` contains the observed evidence.
The numerical correction can remain; further brightness compensation
should wait for final masked-output evidence. Useful-detail and temporal
checks need their own evidence, not another speculative shader change.

## Reproduction and retained artifacts

Local evidence is under
`build/validation/nr-packed-rounding-live-20260917T023044Z`.
It includes tool responses, original sequence manifests and PNGs, frozen
regions, recording bytes, native crops, private batches, signed metrics,
deployment verification and restoration receipts. Raw trees stay local.

The preserved offline scripts use the existing `hmd_assess` metrics,
artifact checks and `fresh_batch_groups` validator. Python dependencies
come from `build/validation/hmd-python-deps`.

-   `analyse_live.py <worktree> <evidence>`: 26 pair artifacts processed,
    four inactive-mode importer failures retained.
-   `audit_controls.py <worktree> <evidence>`: separate four-pair
    NR-off audit, fixed-crop metrics and native contact sheets.
-   `audit_menu_exclusions.py <worktree> <evidence>`: three explicit
    menu exclusions and clear-reference comparisons.
-   `verify_deployment.py <worktree> <evidence>`: physical DLL/shader,
    manifest, AIO receipt and exact provider checks.

`results/clear-reference-audit-v2.json` is the final clear-reference
summary; earlier raw summaries deliberately retain contaminated reference
rows and the initial diagnostic computation. `SHA256SUMS.json` seals
the final local evidence inventory. This documentation-only follow-up
does not rerun the already recorded standalone GPU suite or qualify
SE/AE gameplay, physical HMD optics, highlights or fast motion.

The verified evidence inventory contains 349 files, 654,368,376 bytes;
its SHA-256 is
`321d923a07496310a6eead7f93f8e3315de85529ee3cdec61ecdb6f0911d15e6`.
Focused pre-commit checks passed for the three report files (whitespace,
line endings and Prettier; unrelated language hooks skipped).
