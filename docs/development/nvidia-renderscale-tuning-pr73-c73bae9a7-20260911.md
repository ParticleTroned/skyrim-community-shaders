# NVIDIA render-scale tuning: PR73 readiness change, September 11

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
779.947 / 800.456 ms, changing -9.938% / -3.916%.
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
Full-pass stretch totals are 79 / 93 frames and 4675.398 / 5600.774 ms,
17 episodes per pass, with no active tail. The raw fixed two-frame cutoff
is DIAGNOSTIC_ONLY under imposed settling. The raw scaled-presentation gate
at the proven native terminal target is a CONTRACT_MISMATCH. Both raw failed
gates, observations and limits remain visible below and in the ledger.

Owned captures are verified inactive and the entire journal is flushed.
The maximum extra client dispatch gap was 34.248 ms
against a 250 ms diagnostic budget. No transition was replayed.

## Evidence and validation

The [canonical ledger](vr-render-scale-comparison-ledger.csv) contains all
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

| Row | Route                             | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |     Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | --------------------------------- | ------------------- | ------------- | ------------ | ------------ | ------------------: | --------------------: | ---------------------: | -------------------------------- |
| 3   | TAA -> DLAA                       | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   279.702 / 257.493 |         0.000 / 0.000 |                -22.209 | False/False / False/False        |
| 4   | DLAA -> DLSS Hoshipa              | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   865.768 / 990.084 |     118.537 / 146.144 |               +124.316 | True/True / True/True            |
| 5   | DLSS Hoshipa -> DLSS UQ           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |  1120.619 / 987.027 |      82.596 / 135.981 |               -133.592 | True/True / True/True            |
| 6   | DLSS UQ -> DLSS Quality           | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1091.192 / 1125.321 |     132.806 / 132.556 |                +34.128 | True/True / True/True            |
| 7   | DLSS Quality -> DLSS Balanced     | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1271.037 / 1148.391 |     126.116 / 141.234 |               -122.646 | True/True / True/True            |
| 8   | DLSS Balanced -> DLSS Performance | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1301.079 / 1195.793 |     125.830 / 136.876 |               -105.287 | True/True / True/True            |
| 9   | DLSS Performance -> DLSS UP       | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1209.841 / 1128.613 |     123.616 / 126.963 |                -81.229 | True/True / True/True            |
| 10  | DLSS UP -> DLAA                   | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   734.508 / 760.687 |     176.294 / 169.106 |                +26.179 | False/False / False/False        |
| 23  | NONE -> DLAA                      | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   246.583 / 258.243 |         0.000 / 0.000 |                +11.660 | False/False / False/False        |
| 25  | FSR Native AA -> DLSS Hoshipa     | complete / complete | 1 / 0         | PASS / PASS  | PASS / PASS  |  1469.693 / 957.409 |     126.364 / 131.440 |               -512.284 | True/True / True/True            |
| 29  | FSR UP -> DLSS UP                 | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1468.512 / 1520.555 |     141.939 / 156.038 |                +52.043 | True/True / True/True            |
| 33  | NONE -> DLAA                      | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   244.053 / 250.271 |         0.000 / 0.000 |                 +6.219 | False/False / False/False        |

### FSR3 destinations

| Row | Route                           | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |     Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | ------------------------------- | ------------------- | ------------- | ------------ | ------------ | ------------------: | --------------------: | ---------------------: | -------------------------------- |
| 13  | NONE -> FSR Native AA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   815.505 / 582.542 |         0.000 / 0.000 |               -232.963 | False/False / False/False        |
| 14  | FSR Native AA -> FSR Hoshipa    | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1279.581 / 1370.679 |       123.918 / 0.000 |                +91.098 | True/True / True/True            |
| 15  | FSR Hoshipa -> FSR UQ           | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  727.243 / 1256.793 |         0.000 / 0.000 |               +529.551 | True/True / True/True            |
| 16  | FSR UQ -> FSR Quality           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   923.933 / 795.099 |         0.000 / 0.000 |               -128.835 | True/True / True/True            |
| 17  | FSR Quality -> FSR Balanced     | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   689.066 / 844.197 |         0.000 / 0.000 |               +155.131 | True/True / True/True            |
| 18  | FSR Balanced -> FSR Performance | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  735.192 / 1493.796 |         0.000 / 0.000 |               +758.604 | True/True / True/True            |
| 19  | FSR Performance -> FSR UP       | complete / complete | 1 / 0         | PASS / PASS  | PASS / PASS  |  1220.774 / 771.313 |         0.000 / 0.000 |               -449.461 | True/True / True/True            |
| 20  | FSR UP -> FSR Native AA         | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   713.693 / 763.533 |     238.498 / 203.027 |                +49.839 | False/False / False/False        |
| 24  | DLAA -> FSR Native AA           | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  |   871.391 / 830.242 |     171.357 / 173.511 |                -41.149 | False/False / False/False        |
| 26  | DLSS Hoshipa -> FSR Hoshipa     | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  954.918 / 1468.298 |       90.981 / 87.524 |               +513.380 | True/True / True/True            |
| 28  | NONE -> FSR UP                  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   881.771 / 905.797 |       81.917 / 43.517 |                +24.026 | False/False / False/False        |
| 31  | TAA -> FSR Native AA            | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  |   685.023 / 594.050 |         0.000 / 0.000 |                -90.973 | False/False / False/False        |

### Vendor provider crossings

| Row | Route                         | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |     Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | ----------------------------- | ------------------- | ------------- | ------------ | ------------ | ------------------: | --------------------: | ---------------------: | -------------------------------- |
| 24  | DLAA -> FSR Native AA         | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  |   871.391 / 830.242 |     171.357 / 173.511 |                -41.149 | False/False / False/False        |
| 25  | FSR Native AA -> DLSS Hoshipa | complete / complete | 1 / 0         | PASS / PASS  | PASS / PASS  |  1469.693 / 957.409 |     126.364 / 131.440 |               -512.284 | True/True / True/True            |
| 26  | DLSS Hoshipa -> FSR Hoshipa   | complete / complete | 0 / 1         | PASS / PASS  | PASS / PASS  |  954.918 / 1468.298 |       90.981 / 87.524 |               +513.380 | True/True / True/True            |
| 29  | FSR UP -> DLSS UP             | complete / complete | 1 / 1         | PASS / PASS  | PASS / PASS  | 1468.512 / 1520.555 |     141.939 / 156.038 |                +52.043 | True/True / True/True            |

### TAA and None routes

| Row | Route                 | Retry status P1/P2  | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 |   Strict ms P1/P2 | Cleanup tail ms P1/P2 | Repeat strict delta ms | Stretch selected/recovered P1/P2 |
| --- | --------------------- | ------------------- | ------------- | ------------ | ------------ | ----------------: | --------------------: | ---------------------: | -------------------------------- |
| 1   | DLSS Hoshipa -> NONE  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 656.586 / 697.747 |     208.767 / 225.225 |                +41.161 | True/True / False/False          |
| 2   | NONE -> TAA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 153.417 / 177.483 |         0.000 / 0.000 |                +24.066 | False/False / False/False        |
| 3   | TAA -> DLAA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 279.702 / 257.493 |         0.000 / 0.000 |                -22.209 | False/False / False/False        |
| 11  | DLAA -> TAA           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 457.950 / 425.129 |     289.946 / 259.787 |                -32.821 | False/False / False/False        |
| 12  | TAA -> NONE           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 173.229 / 165.307 |         0.000 / 0.000 |                 -7.922 | False/False / False/False        |
| 13  | NONE -> FSR Native AA | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 815.505 / 582.542 |         0.000 / 0.000 |               -232.963 | False/False / False/False        |
| 21  | FSR Native AA -> TAA  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 481.173 / 519.992 |     275.101 / 297.067 |                +38.819 | False/False / False/False        |
| 22  | TAA -> NONE           | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 167.496 / 182.075 |         0.000 / 0.000 |                +14.580 | False/False / False/False        |
| 23  | NONE -> DLAA          | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 246.583 / 258.243 |         0.000 / 0.000 |                +11.660 | False/False / False/False        |
| 27  | FSR Hoshipa -> NONE   | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 702.902 / 781.304 |     169.372 / 230.755 |                +78.402 | False/False / False/False        |
| 28  | NONE -> FSR UP        | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 881.771 / 905.797 |       81.917 / 43.517 |                +24.026 | False/False / False/False        |
| 30  | DLSS UP -> TAA        | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 702.745 / 730.674 |     233.010 / 226.805 |                +27.928 | False/False / False/False        |
| 31  | TAA -> FSR Native AA  | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 685.023 / 594.050 |         0.000 / 0.000 |                -90.973 | False/False / False/False        |
| 32  | FSR Native AA -> NONE | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 442.075 / 479.112 |     260.948 / 286.758 |                +37.037 | False/False / False/False        |
| 33  | NONE -> DLAA          | complete / complete | 0 / 0         | PASS / PASS  | PASS / PASS  | 244.053 / 250.271 |         0.000 / 0.000 |                 +6.219 | False/False / False/False        |

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

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 779.947        | 1368.052 | 1469.693 | 15.030             | 724.722         | 13.360              | 17               | 79             | 4675.398   | 9       | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 800.456        | 1478.497 | 1520.555 | 15.545             | 734.774         | 13.880              | 17               | 93             | 5600.774   | 10      | 0        | 0                   | MET             | COMPLETE |

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

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

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

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 1              | 12638/447.8196 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 99.237 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 13060/667.7666 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 92.111 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 13206/955.6184 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 48.500 ms; SubmitStageFoveatedCenter: 48.350 ms | 225.592 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 13350/875.1906 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.953 ms; SubmitStageFoveatedCenter: 51.895 ms | 222.424 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 13492/1060.6659 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 59.330 ms; SubmitStageFoveatedCenter: 59.299 ms | 221.089 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 6              | 13634/1095.1331 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.539 ms; SubmitStageFoveatedCenter: 52.475 ms | 208.037 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 13778/1006.7068 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 14463/1109.6968 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 14603/727.2426 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 14744/923.9334 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 14879/689.0664 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 15015/735.1925 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 15163/1220.7745 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 273.663 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 15972/1260.3908 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 16113/819.0405 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 282.461 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 16540/1233.1909 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 118.118 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 17888/758.8008 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 123.090 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 18024/762.0404 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.085 ms; SubmitStageFoveatedCenter: 51.072 ms | 216.645 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 18166/908.79 ms    | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 55.304 ms; SubmitStageFoveatedCenter: 55.217 ms | 214.355 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 18307/924.3108 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.101 ms; SubmitStageFoveatedCenter: 0.994 ms   | 285.134 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 18449/963.6685 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.886 ms; SubmitStageFoveatedCenter: 53.863 ms | 207.779 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 18591/921.5243 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 19263/1370.6792 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 19407/1256.7934 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 19541/795.0986 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 19673/844.1971 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 19814/1493.796 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 19946/771.3135 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 0       | complete                       | none observed                                            | 108.673 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 20706/742.1168 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 20853/1333.2613 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 282.315 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 21268/1282.2805 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

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

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 866.009/779.947 | -9.938       | 10/9        | 4/0          | 2/0                 | none             | NOT_MET/MET         |
| nvidia | 2    | 33/33    | 833.078/800.456 | -3.916       | 10/10       | 4/0          | 2/0                 | none             | NOT_MET/MET         |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 807.337   | 724.722   | -82.614   | -10.233 |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920    | 13.360    | -0.560    | -4.023  |
| nvidia | 1    | Relatch proof total        | ms          | 20183.415 | 18118.055 | -2065.360 | -10.233 |
| nvidia | 1    | Relatch proof total        | frames      | 348       | 334       | -14       | -4.023  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 866.009   | 779.947   | -86.061   | -9.938  |
| nvidia | 1    | Strict completion mean     | frames      | 15.576    | 15.030    | -0.545    | -3.502  |
| nvidia | 1    | Strict completion total    | ms          | 28578.281 | 25738.254 | -2840.027 | -9.938  |
| nvidia | 1    | Strict completion total    | frames      | 514       | 496       | -18       | -3.502  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 17        | -2        | -10.526 |
| nvidia | 1    | Stretch completed total    | frames      | 89        | 79        | -10       | -11.236 |
| nvidia | 1    | Stretch completed total    | ms          | 5719.962  | 4675.398  | -1044.564 | -18.262 |
| nvidia | 1    | Stretch longest episode    | ms          | 646.277   | 620.308   | -25.969   | -4.018  |
| nvidia | 2    | Relatch proof mean         | ms          | 763.765   | 734.774   | -28.992   | -3.796  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.840    | 13.880    | 0.040     | 0.289   |
| nvidia | 2    | Relatch proof total        | ms          | 19094.136 | 18369.345 | -724.791  | -3.796  |
| nvidia | 2    | Relatch proof total        | frames      | 346       | 347       | 1         | 0.289   |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 833.078   | 800.456   | -32.622   | -3.916  |
| nvidia | 2    | Strict completion mean     | frames      | 15.455    | 15.545    | 0.091     | 0.588   |
| nvidia | 2    | Strict completion total    | ms          | 27491.579 | 26415.048 | -1076.530 | -3.916  |
| nvidia | 2    | Strict completion total    | frames      | 510       | 513       | 3         | 0.588   |
| nvidia | 2    | Stretch completed episodes | episodes    | 19        | 17        | -2        | -10.526 |
| nvidia | 2    | Stretch completed total    | frames      | 83        | 93        | 10        | 12.048  |
| nvidia | 2    | Stretch completed total    | ms          | 5383.107  | 5600.774  | 217.667   | 4.044   |
| nvidia | 2    | Stretch longest episode    | ms          | 441.441   | 648.801   | 207.360   | 46.973  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 688.545 / 656.586   | -31.959  | -4.641  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.425 / 153.417   | -16.008  | -9.448  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 249.126 / 279.702   | 30.576   | 12.273  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.374 / 865.768  | -138.606 | -13.800 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.234 / 1120.619 | -145.615 | -11.500 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.702 / 1091.192 | -84.509  | -7.188  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.400 / 1271.037 | -122.363 | -8.782  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.171 / 1301.079 | -51.091  | -3.778  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.511 / 1209.841 | -176.670 | -12.742 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 770.294 / 734.508   | -35.786  | -4.646  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 450.117 / 457.950   | 7.834    | 1.740   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 190.626 / 173.229   | -17.397  | -9.126  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 822.942 / 815.505   | -7.437   | -0.904  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.140 / 1279.581 | -97.559  | -7.084  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.789 / 727.243   | -22.547  | -3.007  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 929.137 / 923.933   | -5.204   | -0.560  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 762.921 / 689.066   | -73.855  | -9.681  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.519 / 735.192  | -665.327 | -47.506 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 778.498 / 1220.774  | 442.277  | 56.812  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 731.796 / 713.693   | -18.103  | -2.474  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 572.501 / 481.173   | -91.328  | -15.952 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 172.732 / 167.496   | -5.236   | -3.031  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 261.855 / 246.583   | -15.272  | -5.832  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1233.573 / 871.391  | -362.182 | -29.360 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.698 / 1469.693  | 492.995  | 50.476  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.005 / 954.918  | -581.088 | -37.831 | 1/0         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.032 / 702.902   | -230.130 | -24.665 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 924.794 / 881.771   | -43.023  | -4.652  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.558 / 1468.512 | -716.046 | -32.778 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 744.487 / 702.745   | -41.742  | -5.607  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 610.701 / 685.023   | 74.322   | 12.170  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 481.567 / 442.075   | -39.493  | -8.201  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 296.510 / 244.053   | -52.457  | -17.691 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 466.266 / 447.820   | 688.545 / 656.586   | 222.279 / 208.767 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6563,"dispatchToBlockedOrPreparationMs":335.0722,"firstNewGenerationToCleanupDrainedMs":209.0736,"firstPhysicalMutationToFirstNewGenerationMs":109.7841,"presentationToStrictCompletionMs":208.7666}  |
| 2   | 169.425 / 153.417   | 169.425 / 153.417   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":153.4173,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 249.126 / 279.702   | 249.126 / 279.702   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":279.7022,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 4   | 790.942 / 667.767   | 920.929 / 786.303   | 129.987 / 118.537 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0033,"dispatchToBlockedOrPreparationMs":320.4408,"firstNewGenerationToCleanupDrainedMs":158.6505,"firstPhysicalMutationToFirstNewGenerationMs":304.2086,"presentationToStrictCompletionMs":198.0014}  |
| 5   | 1014.085 / 955.618  | 1168.706 / 1038.214 | 154.620 / 82.596  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8194,"dispatchToBlockedOrPreparationMs":329.09,"firstNewGenerationToCleanupDrainedMs":164.8594,"firstPhysicalMutationToFirstNewGenerationMs":541.4455,"presentationToStrictCompletionMs":165.0008}    |
| 6   | 947.755 / 875.191   | 1089.396 / 1007.997 | 141.641 / 132.806 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2817,"dispatchToBlockedOrPreparationMs":347.7403,"firstNewGenerationToCleanupDrainedMs":174.1092,"firstPhysicalMutationToFirstNewGenerationMs":482.8658,"presentationToStrictCompletionMs":216.0018}  |
| 7   | 1160.289 / 1060.666 | 1307.700 / 1186.782 | 147.411 / 126.116 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1464,"dispatchToBlockedOrPreparationMs":341.1366,"firstNewGenerationToCleanupDrainedMs":168.5915,"firstPhysicalMutationToFirstNewGenerationMs":673.9075,"presentationToStrictCompletionMs":210.3715}  |
| 8   | 1122.704 / 1095.133 | 1271.880 / 1220.963 | 149.177 / 125.830 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2782,"dispatchToBlockedOrPreparationMs":350.8879,"firstNewGenerationToCleanupDrainedMs":166.2255,"firstPhysicalMutationToFirstNewGenerationMs":700.5718,"presentationToStrictCompletionMs":205.9462}  |
| 9   | 1153.443 / 1006.707 | 1299.332 / 1130.323 | 145.889 / 123.616 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2557,"dispatchToBlockedOrPreparationMs":335.1845,"firstNewGenerationToCleanupDrainedMs":162.6084,"firstPhysicalMutationToFirstNewGenerationMs":629.2742,"presentationToStrictCompletionMs":203.1347}  |
| 10  | 592.366 / 558.214   | 770.294 / 734.508   | 177.929 / 176.294 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5139,"dispatchToBlockedOrPreparationMs":333.0484,"firstNewGenerationToCleanupDrainedMs":221.0865,"firstPhysicalMutationToFirstNewGenerationMs":176.8596,"presentationToStrictCompletionMs":176.2943}  |
| 11  | 171.418 / 168.004   | 450.117 / 457.950   | 278.699 / 289.946 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.388,"dispatchToBlockedOrPreparationMs":126.0806,"firstNewGenerationToCleanupDrainedMs":290.7759,"firstPhysicalMutationToFirstNewGenerationMs":37.7057,"presentationToStrictCompletionMs":289.9458}    |
| 12  | 190.626 / 173.229   | 190.626 / 173.229   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":173.229,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 822.942 / 815.505   | 822.942 / 815.505   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":210.5794,"dispatchToBlockedOrPreparationMs":443.6748,"firstNewGenerationToCleanupDrainedMs":39.6922,"firstPhysicalMutationToFirstNewGenerationMs":121.5584,"presentationToStrictCompletionMs":0}        |
| 14  | 1240.733 / 1109.697 | 1329.442 / 1233.615 | 88.709 / 123.918  | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | {"blockedOrPreparationToFirstPhysicalMutationMs":256.5249,"dispatchToBlockedOrPreparationMs":387.052,"firstNewGenerationToCleanupDrainedMs":166.5031,"firstPhysicalMutationToFirstNewGenerationMs":423.5352,"presentationToStrictCompletionMs":169.8841} |
| 15  | 749.789 / 727.243   | 570.716 / 591.690   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0031,"dispatchToBlockedOrPreparationMs":352.8401,"firstNewGenerationToCleanupDrainedMs":39.6052,"firstPhysicalMutationToFirstNewGenerationMs":195.2417,"presentationToStrictCompletionMs":0}          |
| 16  | 929.137 / 923.933   | 662.595 / 600.731   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0463,"dispatchToBlockedOrPreparationMs":335.5841,"firstNewGenerationToCleanupDrainedMs":41.8456,"firstPhysicalMutationToFirstNewGenerationMs":219.2555,"presentationToStrictCompletionMs":0}          |
| 17  | 762.921 / 689.066   | 616.503 / 603.652   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5866,"dispatchToBlockedOrPreparationMs":368.0976,"firstNewGenerationToCleanupDrainedMs":42.1154,"firstPhysicalMutationToFirstNewGenerationMs":188.8525,"presentationToStrictCompletionMs":0}          |
| 18  | 1400.519 / 735.192  | 1120.851 / 607.982  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":3.943,"dispatchToBlockedOrPreparationMs":363.053,"firstNewGenerationToCleanupDrainedMs":39.7992,"firstPhysicalMutationToFirstNewGenerationMs":201.1864,"presentationToStrictCompletionMs":0}            |
| 19  | 778.498 / 1220.774  | 635.134 / 1055.203  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":262.079,"dispatchToBlockedOrPreparationMs":395.007,"firstNewGenerationToCleanupDrainedMs":40.3851,"firstPhysicalMutationToFirstNewGenerationMs":357.732,"presentationToStrictCompletionMs":0}           |
| 20  | 546.689 / 475.195   | 731.796 / 713.693   | 185.107 / 238.498 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0452,"dispatchToBlockedOrPreparationMs":340.1828,"firstNewGenerationToCleanupDrainedMs":239.0053,"firstPhysicalMutationToFirstNewGenerationMs":128.4599,"presentationToStrictCompletionMs":238.4978}  |
| 21  | 254.898 / 206.072   | 572.501 / 481.173   | 317.603 / 275.101 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":137.6313,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":275.1007}            |
| 22  | 172.732 / 167.496   | 172.732 / 167.496   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.4957,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 261.855 / 246.583   | 261.855 / 246.583   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":246.5831,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 1042.196 / 700.034  | 1233.573 / 871.391  | 191.377 / 171.357 | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5963,"dispatchToBlockedOrPreparationMs":500.3463,"firstNewGenerationToCleanupDrainedMs":216.5493,"firstPhysicalMutationToFirstNewGenerationMs":150.8989,"presentationToStrictCompletionMs":171.3567}  |
| 25  | 747.831 / 1260.391  | 895.346 / 1386.754  | 147.514 / 126.364 | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | {"blockedOrPreparationToFirstPhysicalMutationMs":324.2224,"dispatchToBlockedOrPreparationMs":405.8027,"firstNewGenerationToCleanupDrainedMs":166.9843,"firstPhysicalMutationToFirstNewGenerationMs":489.745,"presentationToStrictCompletionMs":209.3018} |
| 26  | 1384.499 / 819.040  | 1484.709 / 910.022  | 100.210 / 90.981  | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3084,"dispatchToBlockedOrPreparationMs":374.1965,"firstNewGenerationToCleanupDrainedMs":182.4897,"firstPhysicalMutationToFirstNewGenerationMs":349.0272,"presentationToStrictCompletionMs":135.8773}  |
| 27  | 636.450 / 533.530   | 933.032 / 702.902   | 296.582 / 169.372 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4752,"dispatchToBlockedOrPreparationMs":346.9035,"firstNewGenerationToCleanupDrainedMs":236.7528,"firstPhysicalMutationToFirstNewGenerationMs":115.7706,"presentationToStrictCompletionMs":169.372}   |
| 28  | 748.294 / 755.190   | 879.078 / 837.107   | 130.784 / 81.917  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3804,"dispatchToBlockedOrPreparationMs":382.759,"firstNewGenerationToCleanupDrainedMs":165.5979,"firstPhysicalMutationToFirstNewGenerationMs":285.3699,"presentationToStrictCompletionMs":126.5807}   |
| 29  | 1978.717 / 1233.191 | 2084.388 / 1375.130 | 105.670 / 141.939 | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | {"blockedOrPreparationToFirstPhysicalMutationMs":22.6826,"dispatchToBlockedOrPreparationMs":668.5188,"firstNewGenerationToCleanupDrainedMs":185.5666,"firstPhysicalMutationToFirstNewGenerationMs":498.362,"presentationToStrictCompletionMs":235.3212}  |
| 30  | 560.371 / 469.735   | 744.487 / 702.745   | 184.117 / 233.010 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1336,"dispatchToBlockedOrPreparationMs":353.8621,"firstNewGenerationToCleanupDrainedMs":233.7121,"firstPhysicalMutationToFirstNewGenerationMs":112.0376,"presentationToStrictCompletionMs":233.0102}  |
| 31  | 610.701 / 685.023   | 610.701 / 685.023   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":47.443,"dispatchToBlockedOrPreparationMs":406.2882,"firstNewGenerationToCleanupDrainedMs":42.135,"firstPhysicalMutationToFirstNewGenerationMs":189.157,"presentationToStrictCompletionMs":0}            |
| 32  | 197.556 / 181.126   | 481.567 / 442.075   | 284.011 / 260.948 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":121.4218,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":260.9483}            |
| 33  | 296.510 / 244.053   | 296.510 / 244.053   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":244.0526,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 9                    | 0            | 465.818 / 447.513    | -18.305  | 14 / 14           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 4   | 12 / 12                  | 0            | 748.532 / 627.653    | -120.879 | 17 / 17           | 0            |
| 5   | 14 / 13                  | -1           | 960.719 / 873.355    | -87.364  | 19 / 18           | -1           |
| 6   | 17 / 17                  | 0            | 904.599 / 833.888    | -70.711  | 22 / 22           | 0            |
| 7   | 16 / 17                  | 1            | 1113.762 / 1018.191  | -95.572  | 21 / 22           | 1            |
| 8   | 17 / 17                  | 0            | 1079.314 / 1054.738  | -24.576  | 22 / 22           | 0            |
| 9   | 16 / 17                  | 1            | 1094.946 / 967.714   | -127.231 | 21 / 22           | 1            |
| 10  | 9 / 9                    | 0            | 541.019 / 513.422    | -27.597  | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 169.912 / 167.174    | -2.738   | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 775.120 / 775.813    | 0.693    | 10 / 10           | 0            |
| 14  | 23 / 22                  | -1           | 1155.285 / 1067.112  | -88.173  | 28 / 27           | -1           |
| 15  | 12 / 12                  | 0            | 570.504 / 552.085    | -18.419  | 16 / 16           | 0            |
| 16  | 12 / 12                  | 0            | 611.667 / 558.886    | -52.781  | 18 / 20           | 2            |
| 17  | 12 / 12                  | 0            | 571.105 / 561.537    | -9.568   | 16 / 15           | -1           |
| 18  | 22 / 12                  | -10          | 1076.575 / 568.182   | -508.392 | 29 / 16           | -13          |
| 19  | 12 / 22                  | 10           | 584.498 / 1014.818   | 430.320  | 16 / 27           | 11           |
| 20  | 10 / 10                  | 0            | 485.074 / 474.688    | -10.386  | 16 / 15           | -1           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 16 / 10                  | -6           | 992.563 / 654.841    | -337.722 | 21 / 15           | -6           |
| 25  | 13 / 23                  | 10           | 704.319 / 1219.770   | 515.452  | 18 / 28           | 10           |
| 26  | 24 / 13                  | -11          | 1288.337 / 727.532   | -560.805 | 29 / 18           | -11          |
| 27  | 10 / 10                  | 0            | 635.729 / 466.149    | -169.580 | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 704.974 / 671.509    | -33.464  | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1879.015 / 1189.563  | -689.452 | 34 / 28           | -6           |
| 30  | 11 / 9                   | -2           | 506.461 / 469.033    | -37.427  | 16 / 14           | -2           |
| 31  | 9 / 10                   | 1            | 563.570 / 642.888    | 79.318   | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 116.243 / 112.696  | -3.547   |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.448 / 197.994  | -28.454  |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 225.499 / 206.392  | -19.107  |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 378.865 / 380.289  | 1.424    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 379.410 / 375.718  | -3.692   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 375.234 / 386.744  | 11.510   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 422.382 / 373.362  | -49.021  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 293.625 / 276.049  | -17.576  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.915 / 139.395  | -4.520   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 156.516 / 156.803  | 0.287    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 152.521 / 139.603  | -12.918  |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 646.277 / 141.290  | -504.987 |
| 19  | 1 / 1                | 0     | 3 / 13             | 10    | 151.172 / 620.308  | 469.137  |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 207.881 / 366.609  | 158.728  |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 629.135 / 114.709  | -514.426 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1214.838 / 687.436 | -527.402 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 716.918 / 697.747   | -19.171  | -2.674  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 170.160 / 177.483   | 7.323    | 4.304   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.338 / 257.493   | -28.844  | -10.074 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1032.563 / 990.084  | -42.480  | -4.114  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1027.220 / 987.027  | -40.193  | -3.913  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1169.543 / 1125.321 | -44.223  | -3.781  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1390.699 / 1148.391 | -242.307 | -17.423 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.918 / 1195.793 | -0.126   | -0.011  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1249.407 / 1128.613 | -120.794 | -9.668  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 914.287 / 760.687   | -153.600 | -16.800 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 480.252 / 425.129   | -55.123  | -11.478 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.881 / 165.307   | -7.574   | -4.381  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 619.982 / 582.542   | -37.440  | -6.039  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1408.406 / 1370.679 | -37.727  | -2.679  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 945.154 / 1256.793  | 311.639  | 32.972  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 789.230 / 795.099   | 5.868    | 0.744   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 831.971 / 844.197   | 12.226   | 1.469   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 757.557 / 1493.796  | 736.239  | 97.186  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 758.886 / 771.313   | 12.427   | 1.638   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1053.683 / 763.533  | -290.150 | -27.537 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 495.001 / 519.992   | 24.990   | 5.049   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 217.047 / 182.075   | -34.972  | -16.113 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 254.963 / 258.243   | 3.280    | 1.286   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1118.329 / 830.242  | -288.086 | -25.760 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1028.504 / 957.409  | -71.095  | -6.913  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1514.576 / 1468.298 | -46.278  | -3.056  | 1/1         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 774.192 / 781.304   | 7.112    | 0.919   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 939.204 / 905.797   | -33.407  | -3.557  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1918.325 / 1520.555 | -397.770 | -20.735 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 827.143 / 730.674   | -96.470  | -11.663 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 628.123 / 594.050   | -34.073  | -5.425  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 531.443 / 479.112   | -52.331  | -9.847  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 273.671 / 250.271   | -23.400  | -8.550  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 485.143 / 472.522   | 716.918 / 697.747   | 231.775 / 225.225 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0479,"dispatchToBlockedOrPreparationMs":350.0684,"firstNewGenerationToCleanupDrainedMs":225.9804,"firstPhysicalMutationToFirstNewGenerationMs":118.6504,"presentationToStrictCompletionMs":225.225}    |
| 2   | 170.160 / 177.483   | 170.160 / 177.483   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.4832,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 286.338 / 257.493   | 286.338 / 257.493   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":257.4931,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 804.481 / 758.801   | 948.804 / 904.945   | 144.323 / 146.144 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9947,"dispatchToBlockedOrPreparationMs":360.3242,"firstNewGenerationToCleanupDrainedMs":193.0035,"firstPhysicalMutationToFirstNewGenerationMs":348.6226,"presentationToStrictCompletionMs":231.2829}   |
| 5   | 805.305 / 762.040   | 939.029 / 898.021   | 133.724 / 135.981 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.76,"dispatchToBlockedOrPreparationMs":358.469,"firstNewGenerationToCleanupDrainedMs":181.2246,"firstPhysicalMutationToFirstNewGenerationMs":354.5673,"presentationToStrictCompletionMs":224.9864}      |
| 6   | 953.781 / 908.790   | 1087.266 / 1041.346 | 133.485 / 132.556 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7777,"dispatchToBlockedOrPreparationMs":371.3996,"firstNewGenerationToCleanupDrainedMs":174.592,"firstPhysicalMutationToFirstNewGenerationMs":491.5763,"presentationToStrictCompletionMs":216.5307}    |
| 7   | 1093.288 / 924.311  | 1251.222 / 1065.545 | 157.935 / 141.234 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6605,"dispatchToBlockedOrPreparationMs":392.5265,"firstNewGenerationToCleanupDrainedMs":182.9429,"firstPhysicalMutationToFirstNewGenerationMs":486.4149,"presentationToStrictCompletionMs":224.0807}   |
| 8   | 968.840 / 963.668   | 1111.087 / 1100.544 | 142.246 / 136.876 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0819,"dispatchToBlockedOrPreparationMs":399.8201,"firstNewGenerationToCleanupDrainedMs":182.6845,"firstPhysicalMutationToFirstNewGenerationMs":514.9579,"presentationToStrictCompletionMs":232.1241}   |
| 9   | 971.186 / 921.524   | 1146.171 / 1048.488 | 174.985 / 126.963 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7925,"dispatchToBlockedOrPreparationMs":406.9526,"firstNewGenerationToCleanupDrainedMs":168.8756,"firstPhysicalMutationToFirstNewGenerationMs":467.8669,"presentationToStrictCompletionMs":207.0885}   |
| 10  | 692.329 / 591.581   | 914.287 / 760.687   | 221.958 / 169.106 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7673,"dispatchToBlockedOrPreparationMs":353.5765,"firstNewGenerationToCleanupDrainedMs":217.3099,"firstPhysicalMutationToFirstNewGenerationMs":185.0336,"presentationToStrictCompletionMs":169.1063}   |
| 11  | 200.500 / 165.343   | 480.252 / 425.129   | 279.753 / 259.787 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0572,"dispatchToBlockedOrPreparationMs":123.3176,"firstNewGenerationToCleanupDrainedMs":260.6446,"firstPhysicalMutationToFirstNewGenerationMs":37.1097,"presentationToStrictCompletionMs":259.7866}    |
| 12  | 172.881 / 165.307   | 172.881 / 165.307   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":165.307,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 619.982 / 582.542   | 619.982 / 582.542   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":50.8977,"dispatchToBlockedOrPreparationMs":390.5072,"firstNewGenerationToCleanupDrainedMs":45.281,"firstPhysicalMutationToFirstNewGenerationMs":95.8559,"presentationToStrictCompletionMs":0}            |
| 14  | 1273.888 / 1370.679 | 1361.411 / 1324.611 | 87.523 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} | {"blockedOrPreparationToFirstPhysicalMutationMs":305.751,"dispatchToBlockedOrPreparationMs":424.6356,"firstNewGenerationToCleanupDrainedMs":165.0926,"firstPhysicalMutationToFirstNewGenerationMs":429.132,"presentationToStrictCompletionMs":0}          |
| 15  | 945.154 / 1256.793  | 634.137 / 1083.699  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":281.994,"dispatchToBlockedOrPreparationMs":394.7142,"firstNewGenerationToCleanupDrainedMs":41.5369,"firstPhysicalMutationToFirstNewGenerationMs":365.4539,"presentationToStrictCompletionMs":0}          |
| 16  | 789.230 / 795.099   | 608.752 / 604.079   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4423,"dispatchToBlockedOrPreparationMs":344.7541,"firstNewGenerationToCleanupDrainedMs":44.7263,"firstPhysicalMutationToFirstNewGenerationMs":211.1566,"presentationToStrictCompletionMs":0}           |
| 17  | 831.971 / 844.197   | 680.701 / 702.346   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.6416,"dispatchToBlockedOrPreparationMs":419.1894,"firstNewGenerationToCleanupDrainedMs":47.4633,"firstPhysicalMutationToFirstNewGenerationMs":230.0514,"presentationToStrictCompletionMs":0}           |
| 18  | 757.557 / 1493.796  | 620.771 / 1126.684  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":275.2212,"dispatchToBlockedOrPreparationMs":429.5467,"firstNewGenerationToCleanupDrainedMs":48.8497,"firstPhysicalMutationToFirstNewGenerationMs":373.0666,"presentationToStrictCompletionMs":0}         |
| 19  | 758.886 / 771.313   | 624.111 / 640.714   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5632,"dispatchToBlockedOrPreparationMs":388.3442,"firstNewGenerationToCleanupDrainedMs":44.5207,"firstPhysicalMutationToFirstNewGenerationMs":203.2858,"presentationToStrictCompletionMs":0}           |
| 20  | 807.760 / 560.505   | 1053.683 / 763.533  | 245.923 / 203.027 | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8874,"dispatchToBlockedOrPreparationMs":354.7334,"firstNewGenerationToCleanupDrainedMs":254.5382,"firstPhysicalMutationToFirstNewGenerationMs":149.3737,"presentationToStrictCompletionMs":203.0274}   |
| 21  | 222.836 / 222.925   | 495.001 / 519.992   | 272.166 / 297.067 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":143.4389,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":297.0673}             |
| 22  | 217.047 / 182.075   | 217.047 / 182.075   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.0753,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 254.963 / 258.243   | 254.963 / 258.243   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.243,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 935.684 / 656.731   | 1118.329 / 830.242  | 182.645 / 173.511 | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9576,"dispatchToBlockedOrPreparationMs":464.0565,"firstNewGenerationToCleanupDrainedMs":215.4267,"firstPhysicalMutationToFirstNewGenerationMs":147.8015,"presentationToStrictCompletionMs":173.5111}   |
| 25  | 804.197 / 742.117   | 943.123 / 873.556   | 138.926 / 131.440 | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":26.7315,"dispatchToBlockedOrPreparationMs":349.6094,"firstNewGenerationToCleanupDrainedMs":173.2963,"firstPhysicalMutationToFirstNewGenerationMs":323.9192,"presentationToStrictCompletionMs":215.2918}  |
| 26  | 1514.576 / 1333.261 | 1459.934 / 1420.786 | 0 / 87.524        | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":284.7751,"dispatchToBlockedOrPreparationMs":436.6673,"firstNewGenerationToCleanupDrainedMs":178.4377,"firstPhysicalMutationToFirstNewGenerationMs":520.9055,"presentationToStrictCompletionMs":135.0364} |
| 27  | 527.127 / 550.550   | 774.192 / 781.304   | 247.065 / 230.755 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4852,"dispatchToBlockedOrPreparationMs":401.9437,"firstNewGenerationToCleanupDrainedMs":231.4649,"firstPhysicalMutationToFirstNewGenerationMs":144.4103,"presentationToStrictCompletionMs":230.7545}   |
| 28  | 798.736 / 816.038   | 887.620 / 859.556   | 88.884 / 43.517   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3751,"dispatchToBlockedOrPreparationMs":397.3447,"firstNewGenerationToCleanupDrainedMs":171.9637,"firstPhysicalMutationToFirstNewGenerationMs":286.8724,"presentationToStrictCompletionMs":89.7583}    |
| 29  | 1701.862 / 1282.281 | 1838.119 / 1438.319 | 136.257 / 156.038 | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} | {"blockedOrPreparationToFirstPhysicalMutationMs":23.5635,"dispatchToBlockedOrPreparationMs":703.5446,"firstNewGenerationToCleanupDrainedMs":202.6941,"firstPhysicalMutationToFirstNewGenerationMs":508.5164,"presentationToStrictCompletionMs":238.2744}  |
| 30  | 588.638 / 503.869   | 827.143 / 730.674   | 238.505 / 226.805 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2535,"dispatchToBlockedOrPreparationMs":391.7451,"firstNewGenerationToCleanupDrainedMs":227.109,"firstPhysicalMutationToFirstNewGenerationMs":108.566,"presentationToStrictCompletionMs":226.8046}     |
| 31  | 628.123 / 594.050   | 628.123 / 594.050   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0265,"dispatchToBlockedOrPreparationMs":404.5485,"firstNewGenerationToCleanupDrainedMs":50.1418,"firstPhysicalMutationToFirstNewGenerationMs":90.333,"presentationToStrictCompletionMs":0}            |
| 32  | 210.815 / 192.354   | 531.443 / 479.112   | 320.628 / 286.758 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.1379,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.7583}             |
| 33  | 273.671 / 250.271   | 273.671 / 250.271   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2714,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 484.990 / 471.767    | -13.223  | 15 / 16           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 12                  | -2           | 761.174 / 711.942    | -49.233  | 19 / 17           | -2           |
| 5   | 13 / 13                  | 0            | 761.523 / 716.796    | -44.727  | 18 / 18           | 0            |
| 6   | 17 / 17                  | 0            | 909.742 / 866.754    | -42.988  | 22 / 22           | 0            |
| 7   | 18 / 17                  | -1           | 1044.523 / 882.602   | -161.921 | 23 / 22           | -1           |
| 8   | 17 / 18                  | 1            | 923.903 / 917.860    | -6.043   | 22 / 23           | 1            |
| 9   | 16 / 18                  | 2            | 917.460 / 879.612    | -37.848  | 21 / 23           | 2            |
| 10  | 10 / 10                  | 0            | 628.552 / 543.377    | -85.174  | 15 / 16           | 1            |
| 11  | 3 / 3                    | 0            | 198.659 / 164.484    | -34.174  | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 577.400 / 537.261    | -40.139  | 10 / 10           | 0            |
| 14  | 23 / 22                  | -1           | 1185.491 / 1159.519  | -25.972  | 28 / 27           | -1           |
| 15  | 12 / 22                  | 10           | 587.847 / 1042.162   | 454.315  | 20 / 27           | 7            |
| 16  | 12 / 12                  | 0            | 564.512 / 559.353    | -5.159   | 17 / 17           | 0            |
| 17  | 12 / 12                  | 0            | 632.504 / 654.882    | 22.379   | 16 / 16           | 0            |
| 18  | 12 / 22                  | 10           | 576.864 / 1077.834   | 500.971  | 16 / 31           | 15           |
| 19  | 12 / 12                  | 0            | 581.254 / 596.193    | 14.940   | 16 / 16           | 0            |
| 20  | 16 / 9                   | -7           | 807.475 / 508.995    | -298.480 | 21 / 14           | -7           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 15 / 10                  | -5           | 889.114 / 614.816    | -274.298 | 20 / 15           | -5           |
| 25  | 13 / 12                  | -1           | 760.230 / 700.260    | -59.970  | 18 / 17           | -1           |
| 26  | 23 / 23                  | 0            | 1232.033 / 1242.348  | 10.315   | 28 / 28           | 0            |
| 27  | 10 / 11                  | 1            | 526.073 / 549.839    | 23.766   | 16 / 17           | 1            |
| 28  | 11 / 11                  | 0            | 711.446 / 687.592    | -23.854  | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1659.323 / 1235.624  | -423.698 | 34 / 28           | -6           |
| 30  | 10 / 10                  | 0            | 587.951 / 503.565    | -84.386  | 16 / 16           | 0            |
| 31  | 9 / 9                    | 0            | 584.095 / 543.908    | -40.187  | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 213.825 / 228.557  | 14.732   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 210.089 / 242.748  | 32.659   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.853 / 385.643  | 4.789    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 441.441 / 376.351  | -65.090  |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.860 / 404.965  | 3.105    |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 429.952 / 363.200  | -66.752  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 308.404 / 275.831  | -32.572  |
| 15  | 1 / 1                | 0     | 3 / 13             | 10    | 158.148 / 647.943  | 489.796  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 143.759 / 157.094  | 13.335   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 165.514 / 171.022  | 5.508    |
| 18  | 1 / 1                | 0     | 3 / 13             | 10    | 155.220 / 648.801  | 493.581  |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 149.943 / 149.211  | -0.732   |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 341.305 / 0        | -341.305 |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 210.712 / 211.046  | 0.334    |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 617.137 / 638.025  | 20.888   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1054.946 / 700.338 | -354.608 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

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
