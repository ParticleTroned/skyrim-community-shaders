# gameft-sw: CSX campaign configuration

The maintained runner, recorder, RC166 compatibility, reporter and offline
checks live in [skyrim-vr-automation](https://github.com/ParticleTroned/skyrim-vr-automation/blob/dev/tools/gameft-sw/README.md).
Use that repository's `tools/gameft-sw` entry points and protocol. Supply
machine-specific fpsVR and archive paths explicitly. Do not install or
replace a toolchain during an active measurement.

When the user invokes `gameft-sw` without save numbers, retain this question:

> Which save numbers should I load? Give comma-separated numbers, for example 05 or 05, 07 or 05, 07, 12.

Existing CSX checkouts may retain ignored legacy scripts and saved runner
files so an active measurement can finish with its exact validated bytes.
They are local historical copies; future tool changes belong to automation.
The migration removes Git tracking without deleting or editing those files.
CSX retains its DevBench implementation and production-boundary tests.

## Fixed FOV+TAA settings for this performance campaign

The canonical comparison baseline is the complete six-save run
`gameft-sw-20260915T183826985Z`, in order 08, 11, 09, 12, 10, 13.
Its verified renderer base is `190c28a39a52c2bace475d1143a124c5893ac741`;
the exact compiled source with the gameft-sw diagnostics backport is
`a1a11fe0d722fd6dbc18315a023701e0ec4a84de`, Build ID
`ee97357e1005ab9fc04025938a1875e7af689c1c4409a0df74e756b6b2974ca6`.
Three additional same-day baseline repeats were completed on September 17;
see the [repeat comparison](baseline-repeat-comparison-20260917/README.md).
Keep the original run as the pinned historical reference. Use it for primary
CPU/GPU, tail/spike and WPR deltas; do not substitute the later 503fbfc
culling campaign, RC166 or the latest candidate repeats as the baseline.
Keep their comparisons secondary and labeled. Retain the baseline's older
vendor-library bundle as a cross-build limitation, not a proven cause.

Keep `periphery_taa_enable=true`, `periphery_taa_center_area=0.30` and
`periphery_taa_outer_scale=0.70` for comparisons against the original traced
baseline and the current material repeats. Verify these live, before invoking
the wrapper or loading any save, and retain the read-only receipt. A previous
run, archive name or current configuration file is not a substitute for the
running producer's values. Do not silently adjust the game to make it match.
On a mismatch or missing evidence, stop before loading and report the exact
fields. A deliberately different configuration starts a separate comparison
group only when the user requests it.

Do not substitute `foveatedCenterArea` for `periphery_taa_center_area`.
The former is the saved **FOV Only Visible Scale** and is inactive while
FOV+TAA is active. The latter selects the FOV+TAA centre. A saved FOV-only
value of 0.60 alongside an enabled FOV+TAA centre of 0.30 is not an active
0.60-centre mismatch. Preserve both raw values, but label their applicability.

Use the DevBench effective Upscaling settings, or the supported legacy
render-scale status `neuralRendering.requestedConfiguration.upscaling`
fields together with its active foveation evidence. Compare the existing
after-run snapshot as well; a changed setting disqualifies a matched-settings
claim. Before/after receipts do not establish unobserved in-hold values.
Do not add polling during the measured holds or change their calculations.
The wrapper currently records snapshots but does not itself enforce these
three values, so the operator must complete this pre-load check.

The September 16 culling campaign's sixteen before/after receipts explicitly
record `periphery_taa_enable=true` and `periphery_taa_center_area=0.60`;
this finding is not inferred from the saved FOV-only value. Its eight runs
remain internally comparable for that setting. Do not pool them with the
0.30 runs as an isolated build/material comparison. See the
[retained-settings audit](fov-material-audit-20260917/README.md).

## Campaign records

The [portable publication record](gameft-publication-20260918/README.md)
links complete baseline/matched-pose evidence, settings corrections and the
additive 10 ms scheduler reassessment. Historical measurements and their
original verdicts remain distinct from current policy assessments.
