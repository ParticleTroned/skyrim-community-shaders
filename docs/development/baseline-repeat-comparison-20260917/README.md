# Baseline repeats: 2026-09-17

**Comparison status:** the original first run is historical reference,
not an identical-settings control. The three fresh baseline repeats are
internally consistent. The three earlier tested-head repeats require
replacement with matching enabled SSGI/AO settings; see the
[verified settings and execution audit](head-settings-audit.md).

Three same-day repeats plus the historical reference. All numbers reuse the saved gameft-sw reporter; final [50,60) seconds only.

All four runs have verified identical CSX DLL/Build ID and exact save files. The three same-day runs have identical feature settings in corresponding before/after snapshots. Historical settings differ; same-binary comparisons across dates are not a matched-settings repeatability test.

FOV+TAA centre is **0.30**, outer **0.70**, enabled in all four runs. The changed `foveatedCenterArea` value is the inactive FOV-only profile while FOV+TAA is enabled; it is not evidence of an active 0.60 centre.

Complete: see wpr-comparison.md and wpr-comparison.json for original plus three fresh repeats, including scheduler coverage flags.

Run order: 08, 11, 09, 12, 10, 13. Tables group DLAA 08/09/10 and DLSS 11/12/13.

## CPU mean (ms)

| Save | Original | Today R1 | Today R2 | Today R3 | Today median | Median delta vs original |
| ---- | -------: | -------: | -------: | -------: | -----------: | -----------------------: |
| 08   |    4.664 |    5.655 |    5.726 |    5.980 |        5.726 |                   +1.062 |
| 09   |    3.602 |    5.755 |    5.252 |    5.408 |        5.408 |                   +1.806 |
| 10   |    2.937 |    5.916 |    5.879 |    5.824 |        5.879 |                   +2.942 |
| 11   |    7.784 |    8.297 |    7.861 |    7.819 |        7.861 |                   +0.077 |
| 12   |    6.854 |    8.279 |    8.060 |    8.022 |        8.060 |                   +1.206 |
| 13   |    8.675 |   12.483 |   11.409 |    8.841 |       11.409 |                   +2.734 |

## GPU mean (ms)

| Save | Original | Today R1 | Today R2 | Today R3 | Today median | Median delta vs original |
| ---- | -------: | -------: | -------: | -------: | -----------: | -----------------------: |
| 08   |   10.410 |   10.350 |   10.331 |   10.296 |       10.331 |                   -0.079 |
| 09   |    9.537 |    8.532 |    8.402 |    8.371 |        8.402 |                   -1.135 |
| 10   |   10.036 |    8.299 |    8.346 |    8.301 |        8.301 |                   -1.735 |
| 11   |    7.835 |    7.485 |    7.183 |    7.108 |        7.183 |                   -0.652 |
| 12   |    7.576 |    7.467 |    7.167 |    7.124 |        7.167 |                   -0.409 |
| 13   |   10.259 |   10.222 |    9.961 |   10.387 |       10.222 |                   -0.037 |

## CPU P95 (ms)

| Save | Original | Today R1 | Today R2 | Today R3 | Today median | Median delta vs original |
| ---- | -------: | -------: | -------: | -------: | -----------: | -----------------------: |
| 08   |    5.200 |    6.905 |    6.810 |    8.745 |        6.905 |                   +1.705 |
| 09   |    7.400 |    9.400 |    8.800 |    9.000 |        9.000 |                   +1.600 |
| 10   |    3.200 |    9.800 |    9.800 |    9.800 |        9.800 |                   +6.600 |
| 11   |   11.500 |   12.340 |   11.800 |   12.000 |       12.000 |                   +0.500 |
| 12   |   10.600 |   12.300 |   12.000 |   11.600 |       12.000 |                   +1.400 |
| 13   |   12.200 |   21.300 |   21.370 |   12.900 |       21.300 |                   +9.100 |

## CPU P99 (ms)

| Save | Original | Today R1 | Today R2 | Today R3 | Today median | Median delta vs original |
| ---- | -------: | -------: | -------: | -------: | -----------: | -----------------------: |
| 08   |    9.212 |    9.401 |   11.908 |   14.421 |       11.908 |                   +2.696 |
| 09   |    8.300 |   10.600 |    9.400 |    9.500 |        9.500 |                   +1.200 |
| 10   |    3.400 |   10.100 |   10.100 |   10.100 |       10.100 |                   +6.700 |
| 11   |   15.200 |   16.200 |   14.396 |   14.675 |       14.675 |                   -0.525 |
| 12   |   13.315 |   15.200 |   14.788 |   14.177 |       14.788 |                   +1.473 |
| 13   |   14.904 |   26.212 |   23.707 |   14.800 |       23.707 |                   +8.803 |

## GPU P95 (ms)

| Save | Original | Today R1 | Today R2 | Today R3 | Today median | Median delta vs original |
| ---- | -------: | -------: | -------: | -------: | -----------: | -----------------------: |
| 08   |   10.600 |   10.500 |   10.400 |   10.400 |       10.400 |                   -0.200 |
| 09   |   10.000 |    8.700 |    8.600 |    8.500 |        8.600 |                   -1.400 |
| 10   |   10.200 |    8.400 |    8.500 |    8.500 |        8.500 |                   -1.700 |
| 11   |    8.500 |    9.200 |    8.240 |    8.300 |        8.300 |                   -0.200 |
| 12   |    8.000 |    8.095 |    7.480 |    7.500 |        7.500 |                   -0.500 |
| 13   |   10.800 |   12.500 |   11.700 |   10.600 |       11.700 |                   +0.900 |

## GPU P99 (ms)

| Save | Original | Today R1 | Today R2 | Today R3 | Today median | Median delta vs original |
| ---- | -------: | -------: | -------: | -------: | -----------: | -----------------------: |
| 08   |   10.900 |   10.500 |   10.606 |   11.100 |       10.606 |                   -0.294 |
| 09   |   10.200 |    9.000 |    8.700 |    8.700 |        8.700 |                   -1.500 |
| 10   |   10.300 |    8.600 |    8.600 |    8.600 |        8.600 |                   -1.700 |
| 11   |    9.906 |   12.628 |   11.300 |   11.075 |       11.300 |                   +1.394 |
| 12   |    8.200 |   10.200 |    9.100 |    9.377 |        9.377 |                   +1.177 |
| 13   |   12.204 |   14.536 |   15.635 |   12.200 |       14.536 |                   +2.332 |

## Health

All 24 final per-save lifecycle verdicts are successful, with stretch inactive at stop. This is the game-ft final-state assessment, not canonical render-scale qualification. Strict raw assay gates remain preserved in each receipt.

| Run      | Save | CPU settling (s)              | GPU settling (s)              |  Stretch (ms) | Final state |
| -------- | ---- | ----------------------------- | ----------------------------- | ------------: | ----------- |
| Original | 08   | within band from first window | 36                            | none observed | recovered   |
| Original | 11   | within band from first window | 5                             | none observed | recovered   |
| Original | 09   | not settled in 60 s           | 1                             |      5938.820 | recovered   |
| Original | 12   | 3                             | 4                             | none observed | recovered   |
| Original | 10   | within band from first window | 32                            |      5800.681 | recovered   |
| Original | 13   | 38                            | 49                            | none observed | recovered   |
| Today R1 | 08   | 13                            | within band from first window | none observed | recovered   |
| Today R1 | 11   | within band from first window | 31                            | none observed | recovered   |
| Today R1 | 09   | within band from first window | 1                             |      6768.413 | recovered   |
| Today R1 | 12   | within band from first window | within band from first window | none observed | recovered   |
| Today R1 | 10   | within band from first window | 1                             |      6293.802 | recovered   |
| Today R1 | 13   | not settled in 60 s           | not settled in 60 s           |       131.998 | recovered   |
| Today R2 | 08   | within band from first window | within band from first window | none observed | recovered   |
| Today R2 | 11   | 1                             | 2                             | none observed | recovered   |
| Today R2 | 09   | within band from first window | 1                             |      6445.845 | recovered   |
| Today R2 | 12   | within band from first window | 4                             | none observed | recovered   |
| Today R2 | 10   | within band from first window | 1                             |      6297.748 | recovered   |
| Today R2 | 13   | not settled in 60 s           | not settled in 60 s           |       130.662 | recovered   |
| Today R3 | 08   | within band from first window | within band from first window | none observed | recovered   |
| Today R3 | 11   | 2                             | 2                             | none observed | recovered   |
| Today R3 | 09   | within band from first window | 1                             |      6402.358 | recovered   |
| Today R3 | 12   | within band from first window | within band from first window | none observed | recovered   |
| Today R3 | 10   | within band from first window | 1                             |      6234.631 | recovered   |
| Today R3 | 13   | 6                             | 4                             |       132.710 | recovered   |

## Historical settings differences

All three fresh repeats match one another. The following values differ from the original in both before and after snapshots; their per-save performance effect is not established by these snapshots. They prevent treating the four runs as an identical-settings A/B. No settings were changed during this analysis.

| Feature field                                | Original | Three fresh repeats |
| -------------------------------------------- | -------- | ------------------- |
| `AdaptiveBrightness.enabled`                 | True     | False               |
| `AdaptiveBrightness.globalProfile.advanced`  | True     | False               |
| `CSUtility.fixUnderwaterFogDofBlur`          | False    | True                |
| `GrassLighting.BasicGrassBrightness`         | 0.75     | 1.0                 |
| `GrassLighting.OverrideComplexGrassSettings` | 1        | 0                   |
| `LODBlending.EnableWaterReflectionStrength`  | False    | True                |
| `ScreenSpaceGI.EnableStereoSync`             | False    | True                |
| `ScreenSpaceShadows.EnableFoveated`          | 1        | 0                   |
| `Skylighting.IncludeMarkedRoofOccluders`     | True     | False               |
| `Upscaling.fsr4RuntimeEnable`                | False    | True                |
| `VR.EnableLightingFoveationHardCutoff`       | True     | False               |
| `VR.EnableSSRFoveationHardCutoff`            | True     | False               |
| `VR.EnableWaterParallaxFoveationHardCutoff`  | True     | False               |
| `VR.EnableWetternessFoveationHardCutoff`     | True     | False               |
| `Wetterness.SkinWetness`                     | 1.0      | 0.949999988079071   |

The inactive FOV-only saved centre also differs (0.30 to 0.60); it does not change the active FOV+TAA centre. `ScreenSpaceGI.Enabled` and the main-menu `Upscaling.qualityMode` differ only in the before snapshot. Per-save mode evidence takes precedence over menu quality settings. The complete raw values and phase distinctions are retained in the JSON.

## Evidence

-   Complete metrics, settings differences and provenance: [comparison.json](comparison.json).
-   Four-run stack, ready-delay and wait comparison: [wpr-comparison.md](wpr-comparison.md).
-   Original: `gameft-sw-20260915T183826985Z`.
-   Today R1: `gameft-sw-20260917T091738372Z-574152f4`.
-   Today R2: `gameft-sw-20260917T093110225Z-6abc5f75`.
-   Today R3: `gameft-sw-20260917T094258533Z-3bc4de9e`.

Publication note: machine-specific paths use portable root labels. Historical
scheduler flags retain their original policy; see the additive
[10 ms reassessment and complete ledger record](../gameft-publication-20260918/README.md).
