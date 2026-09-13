# renderscale-tuning-nvidia final report

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
-   Presentation stretch: **32 selected, 32 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

## Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 940.919        | 1405.443 | 1483.574 | 14.758             | 843.301         | 13.120              | 18               | 59             | 4780.233   | 14      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 938.066        | 1387.998 | 1503.259 | 15                 | 847.314         | 13.280              | 18               | 62             | 4869.833   | 15      | 0        | 0                   | MET             | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":65391,"leftPath":"NativeOriginal","referenceFrame":65700,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":69151,"leftPath":"NativeOriginal","referenceFrame":69490,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    17063.309 |  16836.586 |     -226.723 |      17176.172 |    17176.172 |              0 |    17194.941 |  16866.961 |      -327.98 |                   n.d. |
| System commit MiB                  |    54768.605 |  54660.301 |     -108.305 |      55081.223 |    55677.168 |        595.945 |    55611.832 |  54697.699 |     -914.133 |                   n.d. |
| DXGI process usage MiB             |     4257.391 |   3415.012 |     -842.379 |       3672.574 |     3672.574 |              0 |     3722.063 |   3310.137 |     -411.926 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        209 |          209 |            209 |          209 |              0 |            0 |        217 |          217 |                  1.038 |
| Estimated live tracked texture MiB |            0 |   2265.723 |     2265.723 |       2265.723 |     2265.723 |              0 |            0 |   2281.598 |     2281.598 |                  1.007 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -226.72265625,
        "systemCommitMiB": -108.3046875,
        "dxgiUsageMiB": -842.37890625,
        "liveTextures": 209,
        "liveTextureMiB": 2265.7227210998535
    },
    "pass2": {
        "processPrivateMiB": -327.98046875,
        "systemCommitMiB": -914.1328125,
        "dxgiUsageMiB": -411.92578125,
        "liveTextures": 217,
        "liveTextureMiB": 2281.5979194641113
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

## Owned provider drain and commit intervals

Cells show frames / milliseconds from exact retained QPC endpoints. Ready means the last observed provider-ready event before the first commit; the required-provider set is not exposed. Commit-to-shared-cleanup is elapsed time to that marker, not isolated cleanup cost. Subsequent commit attempts, raw poll counts and missing identity fields remain in summary.json and CSV. The six-frame settling guard is reported separately.

| Lane   | Pass | Row | Begin sequence | Status   | Begin to pending | Pending to ready | Ready to commit | Commit to cleanup | Commit to applied | Reasons |
| ------ | ---: | --: | -------------: | -------- | ---------------- | ---------------- | --------------- | ----------------- | ----------------- | ------- |
| nvidia |    1 |  14 |             93 | complete | 0 / 0.1041       | 1 / 57.2458      | 0 / 0.2434      | 0 / 0.0349        | 0 / 30.2858       | none    |
| nvidia |    1 |  15 |            113 | complete | 0 / 0.1052       | 1 / 56.6483      | 0 / 0.1554      | 0 / 0.0399        | 0 / 23.5263       | none    |
| nvidia |    1 |  16 |            131 | complete | 0 / 0.0910       | 1 / 63.9945      | 0 / 0.3267      | 0 / 0.0631        | 0 / 25.1330       | none    |
| nvidia |    1 |  17 |            149 | complete | 0 / 0.0663       | 1 / 57.5319      | 0 / 0.1207      | 0 / 0.0558        | 0 / 18.1472       | none    |
| nvidia |    1 |  18 |            167 | complete | 0 / 0.0431       | 1 / 59.5700      | 0 / 0.1542      | 0 / 0.0411        | 0 / 15.1881       | none    |
| nvidia |    1 |  19 |            185 | complete | 0 / 0.1413       | 1 / 57.7958      | 0 / 0.3165      | 0 / 0.0585        | 0 / 14.2390       | none    |
| nvidia |    1 |  25 |            217 | complete | 0 / 0.0723       | 1 / 58.0621      | 0 / 0.1989      | 0 / 0.1090        | 0 / 56.6024       | none    |
| nvidia |    1 |  26 |            238 | complete | 0 / 0.0738       | 1 / 57.6988      | 0 / 0.3128      | 0 / 0.0892        | 0 / 62.1210       | none    |
| nvidia |    1 |  29 |            264 | complete | 0 / 0.0522       | 1 / 56.3305      | 0 / 0.1206      | 0 / 0.1168        | 0 / 46.4091       | none    |
| nvidia |    2 |  14 |             93 | complete | 0 / 0.1729       | 1 / 61.6471      | 0 / 0.3593      | 0 / 0.0594        | 0 / 26.0980       | none    |
| nvidia |    2 |  15 |            113 | complete | 0 / 0.0763       | 1 / 59.0102      | 0 / 0.2294      | 0 / 0.0460        | 0 / 32.2935       | none    |
| nvidia |    2 |  16 |            131 | complete | 0 / 0.3038       | 1 / 68.3178      | 0 / 0.1600      | 0 / 0.0443        | 0 / 24.1964       | none    |
| nvidia |    2 |  17 |            149 | complete | 0 / 0.1783       | 1 / 70.5782      | 0 / 0.2487      | 0 / 0.0537        | 0 / 15.2179       | none    |
| nvidia |    2 |  18 |            167 | complete | 0 / 0.0477       | 1 / 55.2713      | 0 / 0.1406      | 0 / 0.0374        | 0 / 13.1152       | none    |
| nvidia |    2 |  19 |            185 | complete | 0 / 0.1518       | 1 / 54.7020      | 0 / 0.2428      | 0 / 0.0423        | 0 / 13.4660       | none    |
| nvidia |    2 |  25 |            219 | complete | 0 / 0.0682       | 1 / 55.9676      | 0 / 0.2436      | 0 / 0.1399        | 0 / 44.6829       | none    |
| nvidia |    2 |  26 |            240 | complete | 0 / 0.0463       | 1 / 73.0874      | 0 / 0.2064      | 0 / 0.1229        | 0 / 71.5814       | none    |
| nvidia |    2 |  29 |            266 | complete | 0 / 0.1485       | 1 / 55.7062      | 0 / 0.1610      | 0 / 0.1067        | 0 / 40.2867       | none    |

## Owned release stages

Guard exemption does not enable vendor dispatch. Provider preparation and coherent stereo still gate promotion. Denied, revoked and unproven receipts do not establish proof-driven release. Intervals use producer CPU observations; fence readiness is not the exact GPU completion time. blockingCleanupReadyQpc observes cleanup-ownership readiness, not completion of detached retirement fences. Displayed milliseconds are rounded to two decimals; exact QPC, frame endpoints and full precision remain in summary.json and transitions.csv.

| Lane   | Pass | Row | Certificate | Guard exempt | Request to admission ms | All ready to consumed ms | Consumed to published ms | Published to provider prepared ms | Provider prepared to promoted ms | Gaps |
| ------ | ---: | --: | ----------- | ------------ | ----------------------: | -----------------------: | -----------------------: | --------------------------------: | -------------------------------: | ---- |
| nvidia |    1 |  14 | revoked     | false        |                  395.00 |                     0.34 |                    30.20 |                             68.52 |                           307.90 | none |
| nvidia |    1 |  15 | complete    | true         |                  393.58 |                     0.24 |                    23.45 |                             65.56 |                           140.00 | none |
| nvidia |    1 |  16 | complete    | true         |                  406.52 |                     0.47 |                    25.00 |                             63.14 |                           143.28 | none |
| nvidia |    1 |  17 | complete    | true         |                  411.45 |                     0.23 |                    18.05 |                             81.99 |                           173.66 | none |
| nvidia |    1 |  18 | complete    | true         |                  390.08 |                     0.25 |                    15.10 |                             72.10 |                           156.92 | none |
| nvidia |    1 |  19 | complete    | true         |                  394.46 |                     0.44 |                    14.13 |                             65.56 |                           144.33 | none |
| nvidia |    1 |  25 | complete    | true         |                  421.87 |                    33.08 |                    23.73 |                             66.30 |                           138.52 | none |
| nvidia |    1 |  26 | complete    | true         |                  447.31 |                     0.81 |                    61.63 |                             68.73 |                           143.59 | none |
| nvidia |    1 |  29 | complete    | true         |                  372.81 |                    32.24 |                    14.30 |                             76.96 |                           130.10 | none |
| nvidia |    2 |  14 | revoked     | false        |                  390.20 |                     0.51 |                    25.96 |                             75.05 |                           396.92 | none |
| nvidia |    2 |  15 | complete    | true         |                  390.56 |                     0.33 |                    32.20 |                             64.54 |                           154.82 | none |
| nvidia |    2 |  16 | complete    | true         |                  590.07 |                     0.26 |                    24.11 |                             61.38 |                           139.65 | none |
| nvidia |    2 |  17 | complete    | true         |                  411.78 |                     0.38 |                    15.10 |                             69.32 |                           154.57 | none |
| nvidia |    2 |  18 | complete    | true         |                  386.55 |                     0.23 |                    13.03 |                             63.31 |                           143.35 | none |
| nvidia |    2 |  19 | complete    | true         |                  372.04 |                     0.33 |                    13.38 |                             63.19 |                           131.91 | none |
| nvidia |    2 |  25 | complete    | true         |                  405.69 |                    24.43 |                    20.50 |                             75.83 |                           137.68 | none |
| nvidia |    2 |  26 | complete    | true         |                  456.03 |                     0.68 |                    71.13 |                             68.48 |                           143.59 | none |
| nvidia |    2 |  29 | complete    | true         |                  403.13 |                    26.95 |                    13.51 |                             62.54 |                           130.56 | none |

| Lane   | Pass | Row | Fence      | Issue to observed ready ms | Gaps |
| ------ | ---: | --: | ---------- | -------------------------: | ---- |
| nvidia |    1 |  14 | FSRHost    |                      57.31 | none |
| nvidia |    1 |  14 | FSRInterop |                      57.24 | none |
| nvidia |    1 |  14 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  15 | FSRHost    |                      56.71 | none |
| nvidia |    1 |  15 | FSRInterop |                      56.65 | none |
| nvidia |    1 |  15 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  16 | FSRHost    |                      64.04 | none |
| nvidia |    1 |  16 | FSRInterop |                      64.02 | none |
| nvidia |    1 |  16 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  17 | FSRHost    |                      57.56 | none |
| nvidia |    1 |  17 | FSRInterop |                      57.54 | none |
| nvidia |    1 |  17 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  18 | FSRHost    |                      59.57 | none |
| nvidia |    1 |  18 | FSRInterop |                      59.56 | none |
| nvidia |    1 |  18 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  19 | FSRHost    |                      57.87 | none |
| nvidia |    1 |  19 | FSRInterop |                      57.82 | none |
| nvidia |    1 |  19 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  25 | FSRHost    |                      58.09 | none |
| nvidia |    1 |  25 | FSRInterop |                      58.05 | none |
| nvidia |    1 |  25 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  26 | DLSSHost   |                      57.75 | none |
| nvidia |    1 |  29 | FSRHost    |                      56.34 | none |
| nvidia |    1 |  29 | FSRInterop |                      56.32 | none |
| nvidia |    1 |  29 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  14 | FSRHost    |                      61.75 | none |
| nvidia |    2 |  14 | FSRInterop |                      61.67 | none |
| nvidia |    2 |  14 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  15 | FSRHost    |                      59.04 | none |
| nvidia |    2 |  15 | FSRInterop |                      59.01 | none |
| nvidia |    2 |  15 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  16 | FSRHost    |                      68.57 | none |
| nvidia |    2 |  16 | FSRInterop |                      68.35 | none |
| nvidia |    2 |  16 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  17 | FSRHost    |                      70.69 | none |
| nvidia |    2 |  17 | FSRInterop |                      70.61 | none |
| nvidia |    2 |  17 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  18 | FSRHost    |                      55.28 | none |
| nvidia |    2 |  18 | FSRInterop |                      55.26 | none |
| nvidia |    2 |  18 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  19 | FSRHost    |                      54.81 | none |
| nvidia |    2 |  19 | FSRInterop |                      54.72 | none |
| nvidia |    2 |  19 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  25 | FSRHost    |                      55.99 | none |
| nvidia |    2 |  25 | FSRInterop |                      55.98 | none |
| nvidia |    2 |  25 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  26 | DLSSHost   |                      73.11 | none |
| nvidia |    2 |  29 | FSRHost    |                      55.81 | none |
| nvidia |    2 |  29 | FSRInterop |                      55.72 | none |
| nvidia |    2 |  29 | FSRRuntime |                       0.00 | none |

## Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 62296/667.3498 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 145.371 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 62583/1104.1528 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 134.532 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 2              | 62692/1105.1793 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.689 ms; SubmitStageFoveatedCenter: 62.480 ms | 241.349 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 62804/1133.0292 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 68.791 ms; SubmitStageFoveatedCenter: 68.407 ms | 226.745 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 62918/1178.4577 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 70.006 ms; SubmitStageFoveatedCenter: 69.949 ms | 239.143 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 63032/1265.4645 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 70.522 ms; SubmitStageFoveatedCenter: 70.155 ms | 206.996 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 63145/1163.4116 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 63670/1420.5078 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 63779/1058.7242 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 63889/1113.6345 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 63998/1157.3408 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 64107/1044.3658 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 64216/1166.5529 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 115.201 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 64831/1083.6475 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 64949/1262.2867 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 106.044 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 65282/1050.6593 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 126.901 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 66326/1022.1521 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 147.146 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 66434/1051.3285 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.589 ms; SubmitStageFoveatedCenter: 1.276 ms   | 286.796 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 66548/1139.6984 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.812 ms; SubmitStageFoveatedCenter: 1.148 ms   | 275.918 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 66664/1063.8002 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 61.656 ms; SubmitStageFoveatedCenter: 61.384 ms | 203.190 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 66778/1119.0056 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.451 ms; SubmitStageFoveatedCenter: 1.256 ms   | 271.616 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 66894/1143.7986 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 67414/1442.4341 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 67528/1503.2592 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 67633/1237.2148 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 67738/1096.6692 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 67849/1010.1797 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 67963/1090.224 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 68076/988.1845 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 115.157 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 68600/1035.681 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 68717/1284.469 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 107.442 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 69039/1034.8171 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

## Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                           | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | ---------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   1 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"method":"none","qualityMode":0,"renderScaleMode":false}                    |                  2 | yes            |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  5 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  5 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  4 | yes            |
| nvidia |    2 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
