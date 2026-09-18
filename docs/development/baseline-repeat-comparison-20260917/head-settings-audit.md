# Baseline versus tested-head settings verification

**The three earlier head repeats are not an accepted settings-matched comparison. Repeat that head cohort with SSGI enabled, GI disabled, and AO restricted to interiors.** The original September 15 run also remains historical reference only, because other feature settings differ. No measurements are deleted or relabeled as matched.

All three fresh baseline repeats agree with one another, and all three earlier head repeats agree with one another. Before the assays, all 813 commonly exposed settings match. After the assays, one shared setting differs: `ScreenSpaceGI.Enabled=true` in every baseline run and `false` in every head run. This is directly present in all twelve attributed snapshot receipts, not inferred from an archive name or a UI screenshot. Fourteen additional head-only fields remain recorded separately.

## What the SSGI evidence actually establishes

-   In all six runs, `EnableGI=false`, `AOInteriorsOnly=true` and `ILInteriorsOnly=true`. This is AO-only, not enabled indirect GI.
-   The feature is loaded in both builds. The Enabled difference is not merely an unloaded-feature preference.
-   Each after snapshot follows Save 13 completion by about 0.3 seconds. This is an end-state observation, not a substitute for every interior hold.
-   In seven of the nine baseline interior tail windows, the captured render stacks reach `ScreenSpaceGI::UpdateSB` or D3D dispatch beneath `ScreenSpaceGI::DrawSSGI`. In the exact baseline source, those calls follow the enabled/output-needed guard. This confirms actual AO-path work during measured interior windows.
-   None of the nine head interior tails contains those sampled active-path calls; its before/after master switch is false. Sampling absence alone is not proof of zero work, but these runs cannot be certified as matching the enabled baseline.
-   Mere `DrawSSGI` samples occur even when disabled: it also performs early-return/clear handling. They must not be misreported as enabled AO/GI execution.
-   In exterior scenes, AO-interiors-only plus GI-off selects the bypass path. The after-run Enabled difference therefore does not by itself prove expensive AO work in Save 13. No GPU cost is inferred from CPU samples.

## Recorded master switch

| Run               | Before | After | GI    | AO interiors only |
| ----------------- | ------ | ----- | ----- | ----------------- |
| Baseline today R1 | False  | True  | False | True              |
| Baseline today R2 | False  | True  | False | True              |
| Baseline today R3 | False  | True  | False | True              |
| Current R1        | False  | False | False | True              |
| Current R2        | False  | False | False | True              |
| Current R3        | False  | False | False | True              |

## Positive interior execution evidence

Values below are sampled CPU milliseconds in each saved ten-second tail for calls beyond the Enabled guard. They are **not GPU cost**, call counts, or an estimate of the complete AO cost. Zero means no matching sample.

| Run               | Save 08 | Save 09 | Save 10 |
| ----------------- | ------: | ------: | ------: |
| Baseline today R1 |  4.9976 |  1.0003 |  1.9992 |
| Baseline today R2 |  1.9993 |  3.0014 |  1.9994 |
| Baseline today R3 |  2.0003 |  0.0000 |  0.0000 |
| Current R1        |  0.0000 |  0.0000 |  0.0000 |
| Current R2        |  0.0000 |  0.0000 |  0.0000 |
| Current R3        |  0.0000 |  0.0000 |  0.0000 |

## Basis for the replacement comparison

Retain the three fresh baseline repeats. Repeat three complete tested-head assays with matching SSGI/AO settings, Info logging, active FOV+TAA centre 0.30 / outer 0.70, fixed save order 08, 11, 09, 12, 10, 13, and the saved gameft-sw timing rules. Preserve late DLAA/native versus DLSS/render-scale checks. Verify settings after loading as well as at the menu; a menu-only match did not establish in-game equivalence here. Do not adjust prior measurements or subtract an estimated SSGI cost.

This audit changes no runner, hold, metric calculation, build or game setting. The exact per-save feature-state verification needed for replacement runs must be resolved before treating them as a matched cohort.

[Complete snapshot fields, SHA-256 receipts, captured stacks and exact source guard](head-settings-audit.json).
