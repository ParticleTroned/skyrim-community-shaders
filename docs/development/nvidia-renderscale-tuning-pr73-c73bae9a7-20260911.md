# NVIDIA render-scale tuning: PR73 readiness change, September 11

Displayed millisecond values are rounded to two decimal places. Full
precision is retained in the comparison ledger and raw evidence.

Run `renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z` completed all 33 + 33 transitions in game PID
`30108`. Terminal render counts are 66 PASS / 0 FAIL;
Task 2 counts are 66 PASS / 0 FAIL / 0 INCONCLUSIVE. Both passes meet the
applicable full-history health standard, with zero device-loss, OOM,
producer-terminal, lifecycle, fidelity, vendor-fallback, bounds-fallback,
memory-trim, or retirement-fence failure observations. Reporting is COMPLETE.

The physical 28,173,824-byte DLL, its adjacent manifest and
AIO receipt match runtime Build ID `d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f` and clean
Release source `c73bae9a776e67614bba84ee0cecf3d076de259f`. Git establishes main-VR base
`ef7c366dd73989b2b87751c0ef975db7c6fd310f`. The active profile has exactly one enabled
loose DLL provider; Overwrite and unmanaged Data have no competing DLL.
DLL SHA-256: `9a777935e69bbd3c4d7b6400c7291727d6bec15c65708495fbed6719d0b19581`. Full compile identity and package
verification remain in the ledger and [deployment receipt](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/raw/offline/physical-aio-verification.json).

The reference is the previous relevant measured main-VR run
`nvidia-20260910T124329625Z`, source `7c8e3e656`. Later PR66/PR73/PR75
measurements are separate candidate builds. Mean strict completion is
779.95 / 800.46 ms, changing -9.938% / -3.916%.
These exclude each transition's five-second server wait. Reference fidelity
and vendor failures on routes 26 and 28 are absent in this candidate.
Per-route regressions remain visible in the full comparison; lower means
do not establish that every switch improved.

Formal improvement-or-neutral assessment is INCONCLUSIVE: retained scene
conditions differ, a matching complete fixture fingerprint is unavailable,
and no explicit versioned tolerance policy was supplied. Memory classification
is separately inconclusive. The six boundaries and exact predicates below
remain available; neither classification establishes leak freedom.

Pass retries are 9 / 10. All 31 selected
stretch transitions recovered, with no unrecovered selected transition.
Full-pass stretch totals are 79 / 93 frames and 4675.40 / 5600.77 ms,
17 episodes per pass, with no active tail. The raw fixed two-frame cutoff
is DIAGNOSTIC_ONLY under imposed settling. The raw scaled-presentation gate
at the proven native terminal target is a CONTRACT_MISMATCH. Both raw failed
gates, observations and limits remain visible below and in the ledger.

Owned captures are verified inactive and the entire journal is flushed.
The maximum extra client dispatch gap was 34.25 ms
against a 250.00 ms diagnostic budget. No transition was replayed.

## Evidence and validation

The [canonical ledger](vr-render-scale-ledger.md) contains all
finalized fields, both passes, all transitions and the complete comparison.
Field-for-field reconstruction of both summary and comparison passed;
all historical cells are preserved, and all 1,056 paired numeric timing
cells were audited. The append added one result column and retained all
1,151 metric rows.

-   [Complete ledger validation](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/complete-ledger-validation.json)
-   [Finalized summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/summary.json), [transition CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/transitions.csv), [receipt index](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/receipt-index.json)
-   [Full scalar evidence export](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/evidence-values.csv)
-   [Comparison JSON](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z-comparison/comparison.json) and [CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z-comparison/comparison.csv)
-   [Exact finalizer command and result](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/finalization-command.json)
-   [Exact comparison command and result](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z/comparison-command.json)

Finalization took 17.791 s. The single maintained
comparison-wrapper invocation took 4.096 s;
complete ledger preparation, reconstruction and append took
12.637 s. Stage timings are retained in
[reporting-performance.json](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z-comparison/reporting-performance.json).
`pwsh ./tools/git.ps1 diff --check -- docs/development/vr-render-scale-comparison-ledger.csv docs/development/vr-render-scale-iteration.md`
is recorded separately by the final task check. The user requested PR73 publication after measurement. Raw evidence remains local.

## Pass 1 and pass 2 by transition family

Provider crossings and native routes overlap destination-family tables. Retry counts remain adjacent to route identity; complete causes and wait intervals follow in the retained finalizer report. Each B/C-style slash here means pass 1 / pass 2.

### DLSS and DLAA destinations

| Row | Route                             | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |   Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | --------------------------------- | ------------------- | ------------- | ------------ | ------------ | ----------------: | --------------------: | ---------------------: | -------------------------------- |
| 3   | TAA -> DLAA                       | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   279.70 / 257.49 |           0.00 / 0.00 |                 -22.21 | False/False / False/False        |
| 4   | DLAA -> DLSS Hoshipa              | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   865.77 / 990.08 |       118.54 / 146.14 |                +124.32 | True/True / True/True            |
| 5   | DLSS Hoshipa -> DLSS UQ           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |  1120.62 / 987.03 |        82.60 / 135.98 |                -133.59 | True/True / True/True            |
| 6   | DLSS UQ -> DLSS Quality           | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1091.19 / 1125.32 |       132.81 / 132.56 |                 +34.13 | True/True / True/True            |
| 7   | DLSS Quality -> DLSS Balanced     | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1271.04 / 1148.39 |       126.12 / 141.23 |                -122.65 | True/True / True/True            |
| 8   | DLSS Balanced -> DLSS Performance | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1301.08 / 1195.79 |       125.83 / 136.88 |                -105.29 | True/True / True/True            |
| 9   | DLSS Performance -> DLSS UP       | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1209.84 / 1128.61 |       123.62 / 126.96 |                 -81.23 | True/True / True/True            |
| 10  | DLSS UP -> DLAA                   | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   734.51 / 760.69 |       176.29 / 169.11 |                 +26.18 | False/False / False/False        |
| 23  | NONE -> DLAA                      | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   246.58 / 258.24 |           0.00 / 0.00 |                 +11.66 | False/False / False/False        |
| 25  | FSR Native AA -> DLSS Hoshipa     | complete / complete | 1 / 0         | PASS / PASS  | PASS / PASS  |  1469.69 / 957.41 |       126.36 / 131.44 |                -512.28 | True/True / True/True            |
| 29  | FSR UP -> DLSS UP                 | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1468.51 / 1520.56 |       141.94 / 156.04 |                 +52.04 | True/True / True/True            |
| 33  | NONE -> DLAA                      | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   244.05 / 250.27 |           0.00 / 0.00 |                  +6.22 | False/False / False/False        |

### FSR3 destinations

| Row | Route                           | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |   Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | ------------------------------- | ------------------- | ------------- | ------------ | ------------ | ----------------: | --------------------: | ---------------------: | -------------------------------- |
| 13  | NONE -> FSR Native AA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   815.51 / 582.54 |           0.00 / 0.00 |                -232.96 | False/False / False/False        |
| 14  | FSR Native AA -> FSR Hoshipa    | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1279.58 / 1370.68 |         123.92 / 0.00 |                 +91.10 | True/True / True/True            |
| 15  | FSR Hoshipa -> FSR UQ           | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  727.24 / 1256.79 |           0.00 / 0.00 |                +529.55 | True/True / True/True            |
| 16  | FSR UQ -> FSR Quality           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   923.93 / 795.10 |           0.00 / 0.00 |                -128.84 | True/True / True/True            |
| 17  | FSR Quality -> FSR Balanced     | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   689.07 / 844.20 |           0.00 / 0.00 |                +155.13 | True/True / True/True            |
| 18  | FSR Balanced -> FSR Performance | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  735.19 / 1493.80 |           0.00 / 0.00 |                +758.60 | True/True / True/True            |
| 19  | FSR Performance -> FSR UP       | complete / complete | 1 / 0         | PASS / PASS  | PASS / PASS  |  1220.77 / 771.31 |           0.00 / 0.00 |                -449.46 | True/True / True/True            |
| 20  | FSR UP -> FSR Native AA         | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   713.69 / 763.53 |       238.50 / 203.03 |                 +49.84 | False/False / False/False        |
| 24  | DLAA -> FSR Native AA           | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  |   871.39 / 830.24 |       171.36 / 173.51 |                 -41.15 | False/False / False/False        |
| 26  | DLSS Hoshipa -> FSR Hoshipa     | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  954.92 / 1468.30 |         90.98 / 87.52 |                +513.38 | True/True / True/True            |
| 28  | NONE -> FSR UP                  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   881.77 / 905.80 |         81.92 / 43.52 |                 +24.03 | False/False / False/False        |
| 31  | TAA -> FSR Native AA            | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   685.02 / 594.05 |           0.00 / 0.00 |                 -90.97 | False/False / False/False        |

### Vendor provider crossings

| Row | Route                         | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |   Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | ----------------------------- | ------------------- | ------------- | ------------ | ------------ | ----------------: | --------------------: | ---------------------: | -------------------------------- |
| 24  | DLAA -> FSR Native AA         | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  |   871.39 / 830.24 |       171.36 / 173.51 |                 -41.15 | False/False / False/False        |
| 25  | FSR Native AA -> DLSS Hoshipa | complete / complete | 1 / 0         | PASS / PASS  | PASS / PASS  |  1469.69 / 957.41 |       126.36 / 131.44 |                -512.28 | True/True / True/True            |
| 26  | DLSS Hoshipa -> FSR Hoshipa   | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  954.92 / 1468.30 |         90.98 / 87.52 |                +513.38 | True/True / True/True            |
| 29  | FSR UP -> DLSS UP             | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1468.51 / 1520.56 |       141.94 / 156.04 |                 +52.04 | True/True / True/True            |

### TAA and None routes

| Row | Route                 | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 | Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | --------------------- | ------------------- | ------------- | ------------ | ------------ | --------------: | --------------------: | ---------------------: | -------------------------------- |
| 1   | DLSS Hoshipa -> NONE  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 656.59 / 697.75 |       208.77 / 225.23 |                 +41.16 | True/True / False/False          |
| 2   | NONE -> TAA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 153.42 / 177.48 |           0.00 / 0.00 |                 +24.07 | False/False / False/False        |
| 3   | TAA -> DLAA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 279.70 / 257.49 |           0.00 / 0.00 |                 -22.21 | False/False / False/False        |
| 11  | DLAA -> TAA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 457.95 / 425.13 |       289.95 / 259.79 |                 -32.82 | False/False / False/False        |
| 12  | TAA -> NONE           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 173.23 / 165.31 |           0.00 / 0.00 |                  -7.92 | False/False / False/False        |
| 13  | NONE -> FSR Native AA | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 815.51 / 582.54 |           0.00 / 0.00 |                -232.96 | False/False / False/False        |
| 21  | FSR Native AA -> TAA  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 481.17 / 519.99 |       275.10 / 297.07 |                 +38.82 | False/False / False/False        |
| 22  | TAA -> NONE           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 167.50 / 182.08 |           0.00 / 0.00 |                 +14.58 | False/False / False/False        |
| 23  | NONE -> DLAA          | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 246.58 / 258.24 |           0.00 / 0.00 |                 +11.66 | False/False / False/False        |
| 27  | FSR Hoshipa -> NONE   | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 702.90 / 781.30 |       169.37 / 230.76 |                 +78.40 | False/False / False/False        |
| 28  | NONE -> FSR UP        | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 881.77 / 905.80 |         81.92 / 43.52 |                 +24.03 | False/False / False/False        |
| 30  | DLSS UP -> TAA        | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 702.75 / 730.67 |       233.01 / 226.81 |                 +27.93 | False/False / False/False        |
| 31  | TAA -> FSR Native AA  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 685.02 / 594.05 |           0.00 / 0.00 |                 -90.97 | False/False / False/False        |
| 32  | FSR Native AA -> NONE | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 442.08 / 479.11 |       260.95 / 286.76 |                 +37.04 | False/False / False/False        |
| 33  | NONE -> DLAA          | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 244.05 / 250.27 |           0.00 / 0.00 |                  +6.22 | False/False / False/False        |

## Retained finalizer report

-   Assay execution: **COMPLETE**
-   Transitions dispatched: **66/66**
-   Terminal render verdict: **PASS** (terminal condition only)
-   Lane qualification: **NOT_APPLICABLE**
-   Full-history switch health: **NO_COUNTED_FAILURES**
-   Change assessment: **INCONCLUSIVE**
-   Non-stable terminal notes: **0**
-   Task 2/evidence: **per transition** (66 PASS, 0 FAIL, 0 INCONCLUSIVE)
-   Reporting completeness: **COMPLETE**
-   Deployment verification: **COMPLETE**
-   Memory confirmation: **inconclusive**
-   Presentation stretch: **31 selected, 31 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

## Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms  | Max ms  | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | ------- | ------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 779.95         | 1368.05 | 1469.69 | 15.030             | 724.72          | 13.360              | 17               | 79             | 4675.40    | 9       | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 800.46         | 1478.50 | 1520.56 | 15.545             | 734.77          | 13.880              | 17               | 93             | 5600.77    | 10      | 0        | 0                   | MET             | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":16675,"leftPath":"NativeOriginal","referenceFrame":17084,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":21403,"leftPath":"NativeOriginal","referenceFrame":21805,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000.00 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16733.715 |  16496.359 |     -237.355 |      16835.258 |    16829.637 |         -5.621 |    16822.871 |  16603.328 |     -219.543 |                   n.d. |
| System commit MiB                  |    55395.531 |   55048.34 |     -347.191 |      55399.238 |    55491.793 |         92.555 |    55520.379 |  55103.395 |     -416.984 |                   n.d. |
| DXGI process usage MiB             |     4285.164 |   3534.477 |     -750.688 |       3792.039 |     3763.914 |        -28.125 |     3822.215 |   3647.902 |     -174.313 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        263 |          263 |            263 |          263 |              0 |            0 |        236 |          236 |                  0.897 |
| Estimated live tracked texture MiB |            0 |   2402.766 |     2402.766 |       2402.766 |     2402.766 |              0 |            0 |   2358.765 |     2358.765 |                  0.982 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -237.35546875,
        "systemCommitMiB": -347.19140625,
        "dxgiUsageMiB": -750.6875,
        "liveTextures": 263,
        "liveTextureMiB": 2402.765727996826
    },
    "pass2": {
        "processPrivateMiB": -219.54296875,
        "systemCommitMiB": -416.984375,
        "dxgiUsageMiB": -174.3125,
        "liveTextures": 236,
        "liveTextureMiB": 2358.7650413513184
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

## Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                          | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | ------------------------------------------------------ | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ---------------- | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 1              | 12638/447.82 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                          | 99.24 ms                       | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 13060/667.77 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                          | 92.11 ms                       | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 13206/955.62 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 48.50 ms; SubmitStageFoveatedCenter: 48.35 ms | 225.59 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 13350/875.19 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.95 ms; SubmitStageFoveatedCenter: 51.90 ms | 222.42 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 13492/1060.67 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 59.33 ms; SubmitStageFoveatedCenter: 59.30 ms | 221.09 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 6              | 13634/1095.13 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.54 ms; SubmitStageFoveatedCenter: 52.48 ms | 208.04 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 13778/1006.71 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 14463/1109.70 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 14603/727.24 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 14744/923.93 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 14879/689.07 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 15015/735.19 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 15163/1220.77 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 273.66 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 15972/1260.39 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 16113/819.04 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 282.46 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 16540/1233.19 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                          | 118.12 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 17888/758.80 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                          | 123.09 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 18024/762.04 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.09 ms; SubmitStageFoveatedCenter: 51.07 ms | 216.65 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 18166/908.79 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 55.30 ms; SubmitStageFoveatedCenter: 55.22 ms | 214.36 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 18307/924.31 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.10 ms; SubmitStageFoveatedCenter: 0.99 ms   | 285.13 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 18449/963.67 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.89 ms; SubmitStageFoveatedCenter: 53.86 ms | 207.78 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 18591/921.52 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 19263/1370.68 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 19407/1256.79 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 19541/795.10 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 19673/844.20 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 19814/1493.80 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 19946/771.31 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 0       | complete                       | none observed                                          | 108.67 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 20706/742.12 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 20853/1333.26 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 282.32 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 21268/1282.28 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |

## Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                          | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | --------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   1 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"method":"none","qualityMode":0,"renderScaleMode":false}                   |                  1 | yes            |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  6 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                 13 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  2 | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  6 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true} |                 13 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                 13 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |        not_exposed | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |

## Complete comparison with the pinned main-VR reference

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T124329625Z                                                              | renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z                                                              |
| Renderer base           | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | c73bae9a776e67614bba84ee0cecf3d076de259f                                                                        |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        |
| Compiled source         | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | c73bae9a776e67614bba84ee0cecf3d076de259f                                                                        |
| Build ID                | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        | d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f                                                |
| DLL SHA-256             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        | 9a777935e69bbd3c4d7b6400c7291727d6bec15c65708495fbed6719d0b19581                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C   | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | ------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 866.01/779.95 | -9.938       | 10/9        | 4/0          | 2/0                 | none             | NOT_MET/MET         |
| nvidia | 2    | 33/33    | 833.08/800.46 | -3.916       | 10/10       | 4/0          | 2/0                 | none             | NOT_MET/MET         |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | -------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 807.34   | 724.72    | -82.61   | -10.233 |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920   | 13.360    | -0.560   | -4.023  |
| nvidia | 1    | Relatch proof total        | ms          | 20183.42 | 18118.06  | -2065.36 | -10.233 |
| nvidia | 1    | Relatch proof total        | frames      | 348      | 334       | -14      | -4.023  |
| nvidia | 1    | Relatch proof samples      | transitions | 25       | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 866.01   | 779.95    | -86.06   | -9.938  |
| nvidia | 1    | Strict completion mean     | frames      | 15.576   | 15.030    | -0.545   | -3.502  |
| nvidia | 1    | Strict completion total    | ms          | 28578.28 | 25738.25  | -2840.03 | -9.938  |
| nvidia | 1    | Strict completion total    | frames      | 514      | 496       | -18      | -3.502  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19       | 17        | -2       | -10.526 |
| nvidia | 1    | Stretch completed total    | frames      | 89       | 79        | -10      | -11.236 |
| nvidia | 1    | Stretch completed total    | ms          | 5719.96  | 4675.40   | -1044.56 | -18.262 |
| nvidia | 1    | Stretch longest episode    | ms          | 646.28   | 620.31    | -25.97   | -4.018  |
| nvidia | 2    | Relatch proof mean         | ms          | 763.77   | 734.77    | -28.99   | -3.796  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.840   | 13.880    | 0.040    | 0.289   |
| nvidia | 2    | Relatch proof total        | ms          | 19094.14 | 18369.35  | -724.79  | -3.796  |
| nvidia | 2    | Relatch proof total        | frames      | 346      | 347       | 1        | 0.289   |
| nvidia | 2    | Relatch proof samples      | transitions | 25       | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 833.08   | 800.46    | -32.62   | -3.916  |
| nvidia | 2    | Strict completion mean     | frames      | 15.455   | 15.545    | 0.091    | 0.588   |
| nvidia | 2    | Strict completion total    | ms          | 27491.58 | 26415.05  | -1076.53 | -3.916  |
| nvidia | 2    | Strict completion total    | frames      | 510      | 513       | 3        | 0.588   |
| nvidia | 2    | Stretch completed episodes | episodes    | 19       | 17        | -2       | -10.526 |
| nvidia | 2    | Stretch completed total    | frames      | 83       | 93        | 10       | 12.048  |
| nvidia | 2    | Stretch completed total    | ms          | 5383.11  | 5600.77   | 217.67   | 4.044   |
| nvidia | 2    | Stretch longest episode    | ms          | 441.44   | 648.80    | 207.36   | 46.973  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 688.55 / 656.59   | -31.96   | -4.641  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.43 / 153.42   | -16.01   | -9.448  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 249.13 / 279.70   | 30.58    | 12.273  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.37 / 865.77  | -138.61  | -13.800 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.23 / 1120.62 | -145.62  | -11.500 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.70 / 1091.19 | -84.51   | -7.188  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.40 / 1271.04 | -122.36  | -8.782  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.17 / 1301.08 | -51.09   | -3.778  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.51 / 1209.84 | -176.67  | -12.742 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 770.29 / 734.51   | -35.79   | -4.646  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 450.12 / 457.95   | 7.83     | 1.740   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 190.63 / 173.23   | -17.40   | -9.126  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 822.94 / 815.51   | -7.44    | -0.904  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.14 / 1279.58 | -97.56   | -7.084  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.79 / 727.24   | -22.55   | -3.007  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 929.14 / 923.93   | -5.20    | -0.560  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 762.92 / 689.07   | -73.86   | -9.681  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.52 / 735.19  | -665.33  | -47.506 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 778.50 / 1220.77  | 442.28   | 56.812  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 731.80 / 713.69   | -18.10   | -2.474  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 572.50 / 481.17   | -91.33   | -15.952 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 172.73 / 167.50   | -5.24    | -3.031  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 261.86 / 246.58   | -15.27   | -5.832  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1233.57 / 871.39  | -362.18  | -29.360 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.70 / 1469.69  | 493.00   | 50.476  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.01 / 954.92  | -581.09  | -37.831 | 1/0         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.03 / 702.90   | -230.13  | -24.665 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 924.79 / 881.77   | -43.02   | -4.652  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.56 / 1468.51 | -716.05  | -32.778 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 744.49 / 702.75   | -41.74   | -5.607  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 610.70 / 685.02   | 74.32    | 12.170  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 481.57 / 442.08   | -39.49   | -8.201  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 296.51 / 244.05   | -52.46   | -17.691 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 466.27 / 447.82   | 688.55 / 656.59   | 222.28 / 208.77  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6563,"dispatchToBlockedOrPreparationMs":335.0722,"firstNewGenerationToCleanupDrainedMs":209.0736,"firstPhysicalMutationToFirstNewGenerationMs":109.7841,"presentationToStrictCompletionMs":208.7666}  |
| 2   | 169.43 / 153.42   | 169.43 / 153.42   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":153.4173,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 249.13 / 279.70   | 249.13 / 279.70   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":279.7022,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 790.94 / 667.77   | 920.93 / 786.30   | 129.99 / 118.54  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0033,"dispatchToBlockedOrPreparationMs":320.4408,"firstNewGenerationToCleanupDrainedMs":158.6505,"firstPhysicalMutationToFirstNewGenerationMs":304.2086,"presentationToStrictCompletionMs":198.0014}  |
| 5   | 1014.09 / 955.62  | 1168.71 / 1038.21 | 154.62 / 82.60   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8194,"dispatchToBlockedOrPreparationMs":329.09,"firstNewGenerationToCleanupDrainedMs":164.8594,"firstPhysicalMutationToFirstNewGenerationMs":541.4455,"presentationToStrictCompletionMs":165.0008}    |
| 6   | 947.76 / 875.19   | 1089.40 / 1008.00 | 141.64 / 132.81  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2817,"dispatchToBlockedOrPreparationMs":347.7403,"firstNewGenerationToCleanupDrainedMs":174.1092,"firstPhysicalMutationToFirstNewGenerationMs":482.8658,"presentationToStrictCompletionMs":216.0018}  |
| 7   | 1160.29 / 1060.67 | 1307.70 / 1186.78 | 147.41 / 126.12  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1464,"dispatchToBlockedOrPreparationMs":341.1366,"firstNewGenerationToCleanupDrainedMs":168.5915,"firstPhysicalMutationToFirstNewGenerationMs":673.9075,"presentationToStrictCompletionMs":210.3715}  |
| 8   | 1122.70 / 1095.13 | 1271.88 / 1220.96 | 149.18 / 125.83  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2782,"dispatchToBlockedOrPreparationMs":350.8879,"firstNewGenerationToCleanupDrainedMs":166.2255,"firstPhysicalMutationToFirstNewGenerationMs":700.5718,"presentationToStrictCompletionMs":205.9462}  |
| 9   | 1153.44 / 1006.71 | 1299.33 / 1130.32 | 145.89 / 123.62  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2557,"dispatchToBlockedOrPreparationMs":335.1845,"firstNewGenerationToCleanupDrainedMs":162.6084,"firstPhysicalMutationToFirstNewGenerationMs":629.2742,"presentationToStrictCompletionMs":203.1347}  |
| 10  | 592.37 / 558.21   | 770.29 / 734.51   | 177.93 / 176.29  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5139,"dispatchToBlockedOrPreparationMs":333.0484,"firstNewGenerationToCleanupDrainedMs":221.0865,"firstPhysicalMutationToFirstNewGenerationMs":176.8596,"presentationToStrictCompletionMs":176.2943}  |
| 11  | 171.42 / 168.00   | 450.12 / 457.95   | 278.70 / 289.95  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.388,"dispatchToBlockedOrPreparationMs":126.0806,"firstNewGenerationToCleanupDrainedMs":290.7759,"firstPhysicalMutationToFirstNewGenerationMs":37.7057,"presentationToStrictCompletionMs":289.9458}    |
| 12  | 190.63 / 173.23   | 190.63 / 173.23   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":173.229,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 822.94 / 815.51   | 822.94 / 815.51   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":210.5794,"dispatchToBlockedOrPreparationMs":443.6748,"firstNewGenerationToCleanupDrainedMs":39.6922,"firstPhysicalMutationToFirstNewGenerationMs":121.5584,"presentationToStrictCompletionMs":0}        |
| 14  | 1240.73 / 1109.70 | 1329.44 / 1233.62 | 88.71 / 123.92   | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | {"blockedOrPreparationToFirstPhysicalMutationMs":256.5249,"dispatchToBlockedOrPreparationMs":387.052,"firstNewGenerationToCleanupDrainedMs":166.5031,"firstPhysicalMutationToFirstNewGenerationMs":423.5352,"presentationToStrictCompletionMs":169.8841} |
| 15  | 749.79 / 727.24   | 570.72 / 591.69   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0031,"dispatchToBlockedOrPreparationMs":352.8401,"firstNewGenerationToCleanupDrainedMs":39.6052,"firstPhysicalMutationToFirstNewGenerationMs":195.2417,"presentationToStrictCompletionMs":0}          |
| 16  | 929.14 / 923.93   | 662.60 / 600.73   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0463,"dispatchToBlockedOrPreparationMs":335.5841,"firstNewGenerationToCleanupDrainedMs":41.8456,"firstPhysicalMutationToFirstNewGenerationMs":219.2555,"presentationToStrictCompletionMs":0}          |
| 17  | 762.92 / 689.07   | 616.50 / 603.65   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5866,"dispatchToBlockedOrPreparationMs":368.0976,"firstNewGenerationToCleanupDrainedMs":42.1154,"firstPhysicalMutationToFirstNewGenerationMs":188.8525,"presentationToStrictCompletionMs":0}          |
| 18  | 1400.52 / 735.19  | 1120.85 / 607.98  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":3.943,"dispatchToBlockedOrPreparationMs":363.053,"firstNewGenerationToCleanupDrainedMs":39.7992,"firstPhysicalMutationToFirstNewGenerationMs":201.1864,"presentationToStrictCompletionMs":0}            |
| 19  | 778.50 / 1220.77  | 635.13 / 1055.20  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":262.079,"dispatchToBlockedOrPreparationMs":395.007,"firstNewGenerationToCleanupDrainedMs":40.3851,"firstPhysicalMutationToFirstNewGenerationMs":357.732,"presentationToStrictCompletionMs":0}           |
| 20  | 546.69 / 475.20   | 731.80 / 713.69   | 185.11 / 238.50  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0452,"dispatchToBlockedOrPreparationMs":340.1828,"firstNewGenerationToCleanupDrainedMs":239.0053,"firstPhysicalMutationToFirstNewGenerationMs":128.4599,"presentationToStrictCompletionMs":238.4978}  |
| 21  | 254.90 / 206.07   | 572.50 / 481.17   | 317.60 / 275.10  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":137.6313,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":275.1007}            |
| 22  | 172.73 / 167.50   | 172.73 / 167.50   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.4957,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 261.86 / 246.58   | 261.86 / 246.58   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":246.5831,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 1042.20 / 700.03  | 1233.57 / 871.39  | 191.38 / 171.36  | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5963,"dispatchToBlockedOrPreparationMs":500.3463,"firstNewGenerationToCleanupDrainedMs":216.5493,"firstPhysicalMutationToFirstNewGenerationMs":150.8989,"presentationToStrictCompletionMs":171.3567}  |
| 25  | 747.83 / 1260.39  | 895.35 / 1386.75  | 147.51 / 126.36  | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | {"blockedOrPreparationToFirstPhysicalMutationMs":324.2224,"dispatchToBlockedOrPreparationMs":405.8027,"firstNewGenerationToCleanupDrainedMs":166.9843,"firstPhysicalMutationToFirstNewGenerationMs":489.745,"presentationToStrictCompletionMs":209.3018} |
| 26  | 1384.50 / 819.04  | 1484.71 / 910.02  | 100.21 / 90.98   | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3084,"dispatchToBlockedOrPreparationMs":374.1965,"firstNewGenerationToCleanupDrainedMs":182.4897,"firstPhysicalMutationToFirstNewGenerationMs":349.0272,"presentationToStrictCompletionMs":135.8773}  |
| 27  | 636.45 / 533.53   | 933.03 / 702.90   | 296.58 / 169.37  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4752,"dispatchToBlockedOrPreparationMs":346.9035,"firstNewGenerationToCleanupDrainedMs":236.7528,"firstPhysicalMutationToFirstNewGenerationMs":115.7706,"presentationToStrictCompletionMs":169.372}   |
| 28  | 748.29 / 755.19   | 879.08 / 837.11   | 130.78 / 81.92   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3804,"dispatchToBlockedOrPreparationMs":382.759,"firstNewGenerationToCleanupDrainedMs":165.5979,"firstPhysicalMutationToFirstNewGenerationMs":285.3699,"presentationToStrictCompletionMs":126.5807}   |
| 29  | 1978.72 / 1233.19 | 2084.39 / 1375.13 | 105.67 / 141.94  | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | {"blockedOrPreparationToFirstPhysicalMutationMs":22.6826,"dispatchToBlockedOrPreparationMs":668.5188,"firstNewGenerationToCleanupDrainedMs":185.5666,"firstPhysicalMutationToFirstNewGenerationMs":498.362,"presentationToStrictCompletionMs":235.3212}  |
| 30  | 560.37 / 469.74   | 744.49 / 702.75   | 184.12 / 233.01  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1336,"dispatchToBlockedOrPreparationMs":353.8621,"firstNewGenerationToCleanupDrainedMs":233.7121,"firstPhysicalMutationToFirstNewGenerationMs":112.0376,"presentationToStrictCompletionMs":233.0102}  |
| 31  | 610.70 / 685.02   | 610.70 / 685.02   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":47.443,"dispatchToBlockedOrPreparationMs":406.2882,"firstNewGenerationToCleanupDrainedMs":42.135,"firstPhysicalMutationToFirstNewGenerationMs":189.157,"presentationToStrictCompletionMs":0}            |
| 32  | 197.56 / 181.13   | 481.57 / 442.08   | 284.01 / 260.95  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":121.4218,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":260.9483}            |
| 33  | 296.51 / 244.05   | 296.51 / 244.05   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":244.0526,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 9                    | 0            | 465.82 / 447.51      | -18.31   | 14 / 14           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 4   | 12 / 12                  | 0            | 748.53 / 627.65      | -120.88  | 17 / 17           | 0            |
| 5   | 14 / 13                  | -1           | 960.72 / 873.36      | -87.36   | 19 / 18           | -1           |
| 6   | 17 / 17                  | 0            | 904.60 / 833.89      | -70.71   | 22 / 22           | 0            |
| 7   | 16 / 17                  | 1            | 1113.76 / 1018.19    | -95.57   | 21 / 22           | 1            |
| 8   | 17 / 17                  | 0            | 1079.31 / 1054.74    | -24.58   | 22 / 22           | 0            |
| 9   | 16 / 17                  | 1            | 1094.95 / 967.71     | -127.23  | 21 / 22           | 1            |
| 10  | 9 / 9                    | 0            | 541.02 / 513.42      | -27.60   | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 169.91 / 167.17      | -2.74    | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 775.12 / 775.81      | 0.69     | 10 / 10           | 0            |
| 14  | 23 / 22                  | -1           | 1155.29 / 1067.11    | -88.17   | 28 / 27           | -1           |
| 15  | 12 / 12                  | 0            | 570.50 / 552.09      | -18.42   | 16 / 16           | 0            |
| 16  | 12 / 12                  | 0            | 611.67 / 558.89      | -52.78   | 18 / 20           | 2            |
| 17  | 12 / 12                  | 0            | 571.11 / 561.54      | -9.57    | 16 / 15           | -1           |
| 18  | 22 / 12                  | -10          | 1076.58 / 568.18     | -508.39  | 29 / 16           | -13          |
| 19  | 12 / 22                  | 10           | 584.50 / 1014.82     | 430.32   | 16 / 27           | 11           |
| 20  | 10 / 10                  | 0            | 485.07 / 474.69      | -10.39   | 16 / 15           | -1           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 16 / 10                  | -6           | 992.56 / 654.84      | -337.72  | 21 / 15           | -6           |
| 25  | 13 / 23                  | 10           | 704.32 / 1219.77     | 515.45   | 18 / 28           | 10           |
| 26  | 24 / 13                  | -11          | 1288.34 / 727.53     | -560.81  | 29 / 18           | -11          |
| 27  | 10 / 10                  | 0            | 635.73 / 466.15      | -169.58  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 704.97 / 671.51      | -33.46   | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1879.02 / 1189.56    | -689.45  | 34 / 28           | -6           |
| 30  | 11 / 9                   | -2           | 506.46 / 469.03      | -37.43   | 16 / 14           | -2           |
| 31  | 9 / 10                   | 1            | 563.57 / 642.89      | 79.32    | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C   | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ---------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 116.24 / 112.70  | -3.55    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.45 / 197.99  | -28.45   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 225.50 / 206.39  | -19.11   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 378.87 / 380.29  | 1.42     |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 379.41 / 375.72  | -3.69    |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 375.23 / 386.74  | 11.51    |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 422.38 / 373.36  | -49.02   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 293.63 / 276.05  | -17.58   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.92 / 139.40  | -4.52    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 156.52 / 156.80  | 0.29     |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 152.52 / 139.60  | -12.92   |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 646.28 / 141.29  | -504.99  |
| 19  | 1 / 1                | 0     | 3 / 13             | 10    | 151.17 / 620.31  | 469.14   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 207.88 / 366.61  | 158.73   |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 629.14 / 114.71  | -514.43  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1214.84 / 687.44 | -527.40  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 716.92 / 697.75   | -19.17   | -2.674  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 170.16 / 177.48   | 7.32     | 4.304   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.34 / 257.49   | -28.84   | -10.074 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1032.56 / 990.08  | -42.48   | -4.114  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1027.22 / 987.03  | -40.19   | -3.913  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1169.54 / 1125.32 | -44.22   | -3.781  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1390.70 / 1148.39 | -242.31  | -17.423 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.92 / 1195.79 | -0.13    | -0.011  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1249.41 / 1128.61 | -120.79  | -9.668  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 914.29 / 760.69   | -153.60  | -16.800 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 480.25 / 425.13   | -55.12   | -11.478 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.88 / 165.31   | -7.57    | -4.381  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 619.98 / 582.54   | -37.44   | -6.039  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1408.41 / 1370.68 | -37.73   | -2.679  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 945.15 / 1256.79  | 311.64   | 32.972  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 789.23 / 795.10   | 5.87     | 0.744   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 831.97 / 844.20   | 12.23    | 1.469   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 757.56 / 1493.80  | 736.24   | 97.186  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 758.89 / 771.31   | 12.43    | 1.638   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1053.68 / 763.53  | -290.15  | -27.537 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 495.00 / 519.99   | 24.99    | 5.049   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 217.05 / 182.08   | -34.97   | -16.113 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 254.96 / 258.24   | 3.28     | 1.286   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1118.33 / 830.24  | -288.09  | -25.760 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1028.50 / 957.41  | -71.10   | -6.913  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1514.58 / 1468.30 | -46.28   | -3.056  | 1/1         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 774.19 / 781.30   | 7.11     | 0.919   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 939.20 / 905.80   | -33.41   | -3.557  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1918.33 / 1520.56 | -397.77  | -20.735 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 827.14 / 730.67   | -96.47   | -11.663 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 628.12 / 594.05   | -34.07   | -5.425  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 531.44 / 479.11   | -52.33   | -9.847  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 273.67 / 250.27   | -23.40   | -8.550  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 485.14 / 472.52   | 716.92 / 697.75   | 231.78 / 225.23  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0479,"dispatchToBlockedOrPreparationMs":350.0684,"firstNewGenerationToCleanupDrainedMs":225.9804,"firstPhysicalMutationToFirstNewGenerationMs":118.6504,"presentationToStrictCompletionMs":225.225}    |
| 2   | 170.16 / 177.48   | 170.16 / 177.48   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.4832,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 286.34 / 257.49   | 286.34 / 257.49   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":257.4931,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 804.48 / 758.80   | 948.80 / 904.95   | 144.32 / 146.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9947,"dispatchToBlockedOrPreparationMs":360.3242,"firstNewGenerationToCleanupDrainedMs":193.0035,"firstPhysicalMutationToFirstNewGenerationMs":348.6226,"presentationToStrictCompletionMs":231.2829}   |
| 5   | 805.31 / 762.04   | 939.03 / 898.02   | 133.72 / 135.98  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.76,"dispatchToBlockedOrPreparationMs":358.469,"firstNewGenerationToCleanupDrainedMs":181.2246,"firstPhysicalMutationToFirstNewGenerationMs":354.5673,"presentationToStrictCompletionMs":224.9864}      |
| 6   | 953.78 / 908.79   | 1087.27 / 1041.35 | 133.49 / 132.56  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7777,"dispatchToBlockedOrPreparationMs":371.3996,"firstNewGenerationToCleanupDrainedMs":174.592,"firstPhysicalMutationToFirstNewGenerationMs":491.5763,"presentationToStrictCompletionMs":216.5307}    |
| 7   | 1093.29 / 924.31  | 1251.22 / 1065.55 | 157.94 / 141.23  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6605,"dispatchToBlockedOrPreparationMs":392.5265,"firstNewGenerationToCleanupDrainedMs":182.9429,"firstPhysicalMutationToFirstNewGenerationMs":486.4149,"presentationToStrictCompletionMs":224.0807}   |
| 8   | 968.84 / 963.67   | 1111.09 / 1100.54 | 142.25 / 136.88  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0819,"dispatchToBlockedOrPreparationMs":399.8201,"firstNewGenerationToCleanupDrainedMs":182.6845,"firstPhysicalMutationToFirstNewGenerationMs":514.9579,"presentationToStrictCompletionMs":232.1241}   |
| 9   | 971.19 / 921.52   | 1146.17 / 1048.49 | 174.99 / 126.96  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7925,"dispatchToBlockedOrPreparationMs":406.9526,"firstNewGenerationToCleanupDrainedMs":168.8756,"firstPhysicalMutationToFirstNewGenerationMs":467.8669,"presentationToStrictCompletionMs":207.0885}   |
| 10  | 692.33 / 591.58   | 914.29 / 760.69   | 221.96 / 169.11  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7673,"dispatchToBlockedOrPreparationMs":353.5765,"firstNewGenerationToCleanupDrainedMs":217.3099,"firstPhysicalMutationToFirstNewGenerationMs":185.0336,"presentationToStrictCompletionMs":169.1063}   |
| 11  | 200.50 / 165.34   | 480.25 / 425.13   | 279.75 / 259.79  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0572,"dispatchToBlockedOrPreparationMs":123.3176,"firstNewGenerationToCleanupDrainedMs":260.6446,"firstPhysicalMutationToFirstNewGenerationMs":37.1097,"presentationToStrictCompletionMs":259.7866}    |
| 12  | 172.88 / 165.31   | 172.88 / 165.31   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":165.307,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 619.98 / 582.54   | 619.98 / 582.54   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":50.8977,"dispatchToBlockedOrPreparationMs":390.5072,"firstNewGenerationToCleanupDrainedMs":45.281,"firstPhysicalMutationToFirstNewGenerationMs":95.8559,"presentationToStrictCompletionMs":0}            |
| 14  | 1273.89 / 1370.68 | 1361.41 / 1324.61 | 87.52 / 0.00     | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} | {"blockedOrPreparationToFirstPhysicalMutationMs":305.751,"dispatchToBlockedOrPreparationMs":424.6356,"firstNewGenerationToCleanupDrainedMs":165.0926,"firstPhysicalMutationToFirstNewGenerationMs":429.132,"presentationToStrictCompletionMs":0}          |
| 15  | 945.15 / 1256.79  | 634.14 / 1083.70  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":281.994,"dispatchToBlockedOrPreparationMs":394.7142,"firstNewGenerationToCleanupDrainedMs":41.5369,"firstPhysicalMutationToFirstNewGenerationMs":365.4539,"presentationToStrictCompletionMs":0}          |
| 16  | 789.23 / 795.10   | 608.75 / 604.08   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4423,"dispatchToBlockedOrPreparationMs":344.7541,"firstNewGenerationToCleanupDrainedMs":44.7263,"firstPhysicalMutationToFirstNewGenerationMs":211.1566,"presentationToStrictCompletionMs":0}           |
| 17  | 831.97 / 844.20   | 680.70 / 702.35   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.6416,"dispatchToBlockedOrPreparationMs":419.1894,"firstNewGenerationToCleanupDrainedMs":47.4633,"firstPhysicalMutationToFirstNewGenerationMs":230.0514,"presentationToStrictCompletionMs":0}           |
| 18  | 757.56 / 1493.80  | 620.77 / 1126.68  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":275.2212,"dispatchToBlockedOrPreparationMs":429.5467,"firstNewGenerationToCleanupDrainedMs":48.8497,"firstPhysicalMutationToFirstNewGenerationMs":373.0666,"presentationToStrictCompletionMs":0}         |
| 19  | 758.89 / 771.31   | 624.11 / 640.71   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5632,"dispatchToBlockedOrPreparationMs":388.3442,"firstNewGenerationToCleanupDrainedMs":44.5207,"firstPhysicalMutationToFirstNewGenerationMs":203.2858,"presentationToStrictCompletionMs":0}           |
| 20  | 807.76 / 560.51   | 1053.68 / 763.53  | 245.92 / 203.03  | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8874,"dispatchToBlockedOrPreparationMs":354.7334,"firstNewGenerationToCleanupDrainedMs":254.5382,"firstPhysicalMutationToFirstNewGenerationMs":149.3737,"presentationToStrictCompletionMs":203.0274}   |
| 21  | 222.84 / 222.93   | 495.00 / 519.99   | 272.17 / 297.07  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":143.4389,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":297.0673}             |
| 22  | 217.05 / 182.08   | 217.05 / 182.08   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.0753,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 254.96 / 258.24   | 254.96 / 258.24   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.243,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 935.68 / 656.73   | 1118.33 / 830.24  | 182.65 / 173.51  | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9576,"dispatchToBlockedOrPreparationMs":464.0565,"firstNewGenerationToCleanupDrainedMs":215.4267,"firstPhysicalMutationToFirstNewGenerationMs":147.8015,"presentationToStrictCompletionMs":173.5111}   |
| 25  | 804.20 / 742.12   | 943.12 / 873.56   | 138.93 / 131.44  | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":26.7315,"dispatchToBlockedOrPreparationMs":349.6094,"firstNewGenerationToCleanupDrainedMs":173.2963,"firstPhysicalMutationToFirstNewGenerationMs":323.9192,"presentationToStrictCompletionMs":215.2918}  |
| 26  | 1514.58 / 1333.26 | 1459.93 / 1420.79 | 0.00 / 87.52     | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":284.7751,"dispatchToBlockedOrPreparationMs":436.6673,"firstNewGenerationToCleanupDrainedMs":178.4377,"firstPhysicalMutationToFirstNewGenerationMs":520.9055,"presentationToStrictCompletionMs":135.0364} |
| 27  | 527.13 / 550.55   | 774.19 / 781.30   | 247.07 / 230.76  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4852,"dispatchToBlockedOrPreparationMs":401.9437,"firstNewGenerationToCleanupDrainedMs":231.4649,"firstPhysicalMutationToFirstNewGenerationMs":144.4103,"presentationToStrictCompletionMs":230.7545}   |
| 28  | 798.74 / 816.04   | 887.62 / 859.56   | 88.88 / 43.52    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3751,"dispatchToBlockedOrPreparationMs":397.3447,"firstNewGenerationToCleanupDrainedMs":171.9637,"firstPhysicalMutationToFirstNewGenerationMs":286.8724,"presentationToStrictCompletionMs":89.7583}    |
| 29  | 1701.86 / 1282.28 | 1838.12 / 1438.32 | 136.26 / 156.04  | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} | {"blockedOrPreparationToFirstPhysicalMutationMs":23.5635,"dispatchToBlockedOrPreparationMs":703.5446,"firstNewGenerationToCleanupDrainedMs":202.6941,"firstPhysicalMutationToFirstNewGenerationMs":508.5164,"presentationToStrictCompletionMs":238.2744}  |
| 30  | 588.64 / 503.87   | 827.14 / 730.67   | 238.51 / 226.81  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2535,"dispatchToBlockedOrPreparationMs":391.7451,"firstNewGenerationToCleanupDrainedMs":227.109,"firstPhysicalMutationToFirstNewGenerationMs":108.566,"presentationToStrictCompletionMs":226.8046}     |
| 31  | 628.12 / 594.05   | 628.12 / 594.05   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0265,"dispatchToBlockedOrPreparationMs":404.5485,"firstNewGenerationToCleanupDrainedMs":50.1418,"firstPhysicalMutationToFirstNewGenerationMs":90.333,"presentationToStrictCompletionMs":0}            |
| 32  | 210.82 / 192.35   | 531.44 / 479.11   | 320.63 / 286.76  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.1379,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.7583}             |
| 33  | 273.67 / 250.27   | 273.67 / 250.27   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2714,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 484.99 / 471.77      | -13.22   | 15 / 16           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 12                  | -2           | 761.17 / 711.94      | -49.23   | 19 / 17           | -2           |
| 5   | 13 / 13                  | 0            | 761.52 / 716.80      | -44.73   | 18 / 18           | 0            |
| 6   | 17 / 17                  | 0            | 909.74 / 866.75      | -42.99   | 22 / 22           | 0            |
| 7   | 18 / 17                  | -1           | 1044.52 / 882.60     | -161.92  | 23 / 22           | -1           |
| 8   | 17 / 18                  | 1            | 923.90 / 917.86      | -6.04    | 22 / 23           | 1            |
| 9   | 16 / 18                  | 2            | 917.46 / 879.61      | -37.85   | 21 / 23           | 2            |
| 10  | 10 / 10                  | 0            | 628.55 / 543.38      | -85.17   | 15 / 16           | 1            |
| 11  | 3 / 3                    | 0            | 198.66 / 164.48      | -34.17   | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 577.40 / 537.26      | -40.14   | 10 / 10           | 0            |
| 14  | 23 / 22                  | -1           | 1185.49 / 1159.52    | -25.97   | 28 / 27           | -1           |
| 15  | 12 / 22                  | 10           | 587.85 / 1042.16     | 454.32   | 20 / 27           | 7            |
| 16  | 12 / 12                  | 0            | 564.51 / 559.35      | -5.16    | 17 / 17           | 0            |
| 17  | 12 / 12                  | 0            | 632.50 / 654.88      | 22.38    | 16 / 16           | 0            |
| 18  | 12 / 22                  | 10           | 576.86 / 1077.83     | 500.97   | 16 / 31           | 15           |
| 19  | 12 / 12                  | 0            | 581.25 / 596.19      | 14.94    | 16 / 16           | 0            |
| 20  | 16 / 9                   | -7           | 807.48 / 509.00      | -298.48  | 21 / 14           | -7           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 15 / 10                  | -5           | 889.11 / 614.82      | -274.30  | 20 / 15           | -5           |
| 25  | 13 / 12                  | -1           | 760.23 / 700.26      | -59.97   | 18 / 17           | -1           |
| 26  | 23 / 23                  | 0            | 1232.03 / 1242.35    | 10.32    | 28 / 28           | 0            |
| 27  | 10 / 11                  | 1            | 526.07 / 549.84      | 23.77    | 16 / 17           | 1            |
| 28  | 11 / 11                  | 0            | 711.45 / 687.59      | -23.85   | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1659.32 / 1235.62    | -423.70  | 34 / 28           | -6           |
| 30  | 10 / 10                  | 0            | 587.95 / 503.57      | -84.39   | 16 / 16           | 0            |
| 31  | 9 / 9                    | 0            | 584.10 / 543.91      | -40.19   | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C   | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ---------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 213.83 / 228.56  | 14.73    |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 210.09 / 242.75  | 32.66    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.85 / 385.64  | 4.79     |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 441.44 / 376.35  | -65.09   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.86 / 404.97  | 3.11     |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 429.95 / 363.20  | -66.75   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 308.40 / 275.83  | -32.57   |
| 15  | 1 / 1                | 0     | 3 / 13             | 10    | 158.15 / 647.94  | 489.80   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 143.76 / 157.09  | 13.34    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 165.51 / 171.02  | 5.51     |
| 18  | 1 / 1                | 0     | 3 / 13             | 10    | 155.22 / 648.80  | 493.58   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 149.94 / 149.21  | -0.73    |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 341.31 / 0.00    | -341.31  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 210.71 / 211.05  | 0.33     |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 617.14 / 638.03  | 20.89    |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1054.95 / 700.34 | -354.61  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |

## Cumulative gates and other health evidence

### nvidia-20260910T124329625Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                     | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                    | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":89,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":192,"allowedPresentationStretchMaximumFrames":13,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                                                                                                                                                                                             | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":52644,"leftPath":"NativeOriginal","referenceFrame":53015,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                            | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 4            |
| vendorFailureStretchEyeObservations   | 2            |
| boundsMismatchFallbackEyeObservations | 0            |

### nvidia-20260910T124329625Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":83,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":181,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":57130,"leftPath":"NativeOriginal","referenceFrame":57515,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 4            |
| vendorFailureStretchEyeObservations   | 2            |
| boundsMismatchFallbackEyeObservations | 0            |

### renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":16675,"leftPath":"NativeOriginal","referenceFrame":17084,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

### renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                  | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":21403,"leftPath":"NativeOriginal","referenceFrame":21805,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Failure counter                       | Observations |
| ------------------------------------- | ------------ |
| deviceLost                            | 0            |
| outOfMemory                           | 0            |
| transition                            | 0            |
| dlssLifecycle                         | 0            |
| fsrLifecycle                          | 0            |
| memoryTrim                            | 0            |
| retirementFence                       | 0            |
| fidelityMismatches                    | 0            |
| vendorFailureStretchEyeObservations   | 0            |
| boundsMismatchFallbackEyeObservations | 0            |

## Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                            | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | --------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16894.5078125 / 16681.24609375 / -213.26171875 | 16733.71484375 / 16496.359375 / -237.35546875 | -24.094               |
| nvidia | 1    | systemCommitMiB   | 55422.94921875 / 55060.37890625 / -362.5703125 | 55395.53125 / 55048.33984375 / -347.19140625  | 15.379                |
| nvidia | 1    | dxgiUsageMiB      | 5583.1171875 / 3750.015625 / -1833.1015625     | 4285.1640625 / 3534.4765625 / -750.6875       | 1082.414              |
| nvidia | 1    | liveTextures      | 0 / 261 / 261                                  | 0 / 263 / 263                                 | 2                     |
| nvidia | 1    | liveTextureMiB    | 0 / 2429.8961448669434 / 2429.8961448669434    | 0 / 2402.765727996826 / 2402.765727996826     | -27.130               |
| nvidia | 2    | processPrivateMiB | 17097.22265625 / 16617.34765625 / -479.875     | 16822.87109375 / 16603.328125 / -219.54296875 | 260.332               |
| nvidia | 2    | systemCommitMiB   | 55505.6015625 / 54815.92578125 / -689.67578125 | 55520.37890625 / 55103.39453125 / -416.984375 | 272.691               |
| nvidia | 2    | dxgiUsageMiB      | 4066.75390625 / 3643.64453125 / -423.109375    | 3822.21484375 / 3647.90234375 / -174.3125     | 248.797               |
| nvidia | 2    | liveTextures      | 0 / 211 / 211                                  | 0 / 236 / 236                                 | 25                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2274.9311332702637 / 2274.9311332702637    | 0 / 2358.7650413513184 / 2358.7650413513184   | 83.834                |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2168        | 2294        | 126        |
| cpu/compactPresentationContract/reuses                  | 2146        | 2272        | 126        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 71          | 1          |
| cpu/generationResourceValidation/contractPublishes      | 155         | 145         | -10        |
| cpu/generationResourceValidation/fullValidations        | 581         | 567         | -14        |
| cpu/generationResourceValidation/stableChecks           | 8236        | 8647        | 411        |
| cpu/generationResourceValidation/stableHits             | 8159        | 8568        | 409        |
| cpu/generationResourceValidation/stableMisses           | 77          | 79          | 2          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4149        | 4457        | 308        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4149        | 4457        | 308        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4107        | 4415        | 308        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4149        | 4457        | 308        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4128        | 4436        | 308        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 35          | 1          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4115        | 4422        | 307        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4054        | 4362        | 308        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4134        | 4442        | 308        |
| cpu/strongStereoPacket/captures                         | 4492        | 4749        | 257        |
| cpu/strongStereoPacket/commitAccepts                    | 4317        | 4570        | 253        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 64          | -1         |
| cpu/strongStereoPacket/commitValidations                | 4382        | 4634        | 252        |
| cpu/strongStereoPacket/cycleReuses                      | 2205        | 2335        | 130        |
| cpu/strongStereoPacket/fastSkips                        | 3806        | 4165        | 359        |
| cpu/strongStereoPacket/invalidations                    | 4706        | 5009        | 303        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 101         | -3         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2183        | 2313        | 130        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.867       | 1.630       | -0.237     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 22.700      | 26.800      | 4.100      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.108       | -0.025     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 1.400       | -0.200     |
| cpu/window/currentFrame                                 | 53015       | 17085       | -35930     |
| cpu/window/elapsedFrames                                | 4148        | 4456        | 308        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 48867       | 12629       | -36238     |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 53017       | 17086       | -35931     |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5974703856  | 6408627984  | 433924128  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4709        | 5051        | 342        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11961613440 | 12830348160 | 868734720  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.297       | 0.308       | 0.010      |
| gpu/item5ActiveFSRCopies/activePixels                   | 11119322840 | 12453511360 | 1334188520 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26297233960 | 28036639040 | 1739405080 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14730       | 15940       | 1210       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1971        | 2105        | 134        |
| gpu/item7EarlyHAM/executedClears                        | 1974        | 2116        | 142        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1974        | 2116        | 142        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4104        | 4380        | 276        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4150        | 4457        | 307        |
| gpu/startFrame                                          | 48867       | 12629       | -36238     |
| profiler/available                                      | true        | true        | n/a        |
| profiler/capabilities                                   | 63          | 63          | 0          |
| profiler/capturing                                      | false       | false       | n/a        |
| profiler/enabled                                        | true        | true        | n/a        |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0          |
| profiler/frame/captured                                 | 0           | 0           | 0          |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0          |
| profiler/frame/slotRefusals                             | 0           | 0           | 0          |
| profiler/limits/frameLatency                            | 3           | 3           | 0          |
| profiler/limits/historyCapacity                         | 300         | 300         | 0          |
| profiler/limits/maximumTimers                           | 128         | 128         | 0          |
| profiler/timerCount                                     | 0           | 0           | 0          |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a        |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a        |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a        |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a        |
| texture/active                                          | false       | false       | n/a        |
| texture/attachFailures                                  | 0           | 0           | 0          |
| texture/createdCount                                    | 3962        | 3962        | 0          |
| texture/createdEstimatedBytes                           | 38815224912 | 38741091672 | -74133240  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3701        | 3699        | -2         |
| texture/destroyedEstimatedBytes                         | 36267294132 | 36221609196 | -45684936  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 886         | 893         | 7          |
| texture/liveTextureRecordCount                          | 261         | 263         | 2          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 50          | 54          | 4          |
| texture/niSourceTextureMatchedEstimatedBytes            | 172279000   | 143830896   | -28448104  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1536        | 1504        | -32        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 261         | 263         | 2          |
| texture/outstandingEstimatedBytes                       | 2547930780  | 2519482476  | -28448304  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 1           | 1           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta     |
| ------------------------------------------------------- | ----------- | ----------- | --------- |
| cpu/active                                              | false       | false       | n/a       |
| cpu/compactPresentationContract/publishes               | 2143        | 2249        | 106       |
| cpu/compactPresentationContract/reuses                  | 2121        | 2227        | 106       |
| cpu/devBenchOnly                                        | true        | true        | n/a       |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0         |
| cpu/generationResourceValidation/contractPublishes      | 156         | 151         | -5        |
| cpu/generationResourceValidation/fullValidations        | 570         | 584         | 14        |
| cpu/generationResourceValidation/stableChecks           | 8160        | 8533        | 373       |
| cpu/generationResourceValidation/stableHits             | 8083        | 8456        | 373       |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0         |
| cpu/schemaVersion                                       | 1           | 1           | 0         |
| cpu/sessionId                                           | 2           | 2           | 0         |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0         |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4106        | 4313        | 207       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0         |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4106        | 4313        | 207       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4064        | 4271        | 207       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0         |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4106        | 4313        | 207       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0         |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4085        | 4292        | 207       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0         |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0         |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4072        | 4279        | 207       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4017        | 4215        | 198       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 98          | 8         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0         |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4091        | 4298        | 207       |
| cpu/strongStereoPacket/captures                         | 4444        | 4646        | 202       |
| cpu/strongStereoPacket/commitAccepts                    | 4266        | 4479        | 213       |
| cpu/strongStereoPacket/commitRejects                    | 66          | 65          | -1        |
| cpu/strongStereoPacket/commitValidations                | 4332        | 4544        | 212       |
| cpu/strongStereoPacket/cycleReuses                      | 2180        | 2283        | 103       |
| cpu/strongStereoPacket/fastSkips                        | 3768        | 3980        | 212       |
| cpu/strongStereoPacket/invalidations                    | 4653        | 4854        | 201       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 102         | -2        |
| cpu/strongStereoPacket/lifetimeReuses                   | 2160        | 2261        | 101       |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.869       | 1.673       | -0.197    |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 36.700      | 64.200      | 27.500    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.106       | -0.027    |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.500       | 1.200       | -0.300    |
| cpu/window/currentFrame                                 | 57516       | 21805       | -35711    |
| cpu/window/elapsedFrames                                | 4107        | 4313        | 206       |
| cpu/window/initialized                                  | true        | true        | n/a       |
| cpu/window/startFrame                                   | 53409       | 17492       | -35917    |
| gpu/active                                              | false       | false       | n/a       |
| gpu/currentFrame                                        | 57516       | 21807       | -35709    |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0         |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5901114384  | 6208160112  | 307045728 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4651        | 4893        | 242       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11814284160 | 12429002880 | 614718720 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0         |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.307       | 0.000     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11542473520 | 12004040680 | 461567160 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26077296080 | 27063620120 | 986324040 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14810       | 15380       | 570       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0         |
| gpu/item7EarlyHAM/directOutputSkips                     | 1955        | 2027        | 72        |
| gpu/item7EarlyHAM/executedClears                        | 1948        | 2072        | 124       |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1948        | 2072        | 124       |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4064        | 4260        | 196       |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0         |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0         |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0         |
| gpu/observedFrames                                      | 4107        | 4315        | 208       |
| gpu/startFrame                                          | 53409       | 17492       | -35917    |
| profiler/available                                      | true        | true        | n/a       |
| profiler/capabilities                                   | 63          | 63          | 0         |
| profiler/capturing                                      | false       | false       | n/a       |
| profiler/enabled                                        | true        | true        | n/a       |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0         |
| profiler/frame/captured                                 | 0           | 0           | 0         |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0         |
| profiler/frame/slotRefusals                             | 0           | 0           | 0         |
| profiler/limits/frameLatency                            | 3           | 3           | 0         |
| profiler/limits/historyCapacity                         | 300         | 300         | 0         |
| profiler/limits/maximumTimers                           | 128         | 128         | 0         |
| profiler/timerCount                                     | 0           | 0           | 0         |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a       |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a       |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a       |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a       |
| texture/active                                          | false       | false       | n/a       |
| texture/attachFailures                                  | 0           | 0           | 0         |
| texture/createdCount                                    | 3914        | 3965        | 51        |
| texture/createdEstimatedBytes                           | 38675267808 | 38833321536 | 158053728 |
| texture/currentCohort                                   | 0           | 0           | 0         |
| texture/destroyedCount                                  | 3703        | 3729        | 26        |
| texture/destroyedEstimatedBytes                         | 36289829620 | 36359977124 | 70147504  |
| texture/droppedTextureRecords                           | 0           | 0           | 0         |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0         |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0         |
| texture/groupCount                                      | 863         | 887         | 24        |
| texture/liveTextureRecordCount                          | 211         | 236         | 25        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0         |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0         |
| texture/niSourceTextureMatchedCount                     | 4           | 29          | 25        |
| texture/niSourceTextureMatchedEstimatedBytes            | 9786808     | 97693032    | 87906224  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0         |
| texture/niSourceTextureResourceCount                    | 1509        | 1509        | 0         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a       |
| texture/outstandingCount                                | 211         | 236         | 25        |
| texture/outstandingEstimatedBytes                       | 2385438188  | 2473344412  | 87906224  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0         |
| texture/recordingFailures                               | 0           | 0           | 0         |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0         |
| texture/sessionID                                       | 2           | 2           | 0         |
| texture/supported                                       | true        | true        | n/a       |

</details>

## Context, memory, CPU/GPU and evidence

| Retained context matches | Result |
| ------------------------ | ------ |
| adapter                  | true   |
| scene                    | false  |
| foveation                | true   |
| toolchain                | true   |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.

## Requested comparison with the previous PR73 measurement

See [the complete additional comparison](pr73-readiness-nvidia-comparison-20260911.md). The previous
measurement and main-VR comparison remain retained. Complete comparison
and statistics reconstruction passed; every historical ledger cell is
preserved and all 1,056 paired timing cells were audited again.
