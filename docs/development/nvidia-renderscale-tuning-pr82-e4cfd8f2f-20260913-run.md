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
-   Presentation stretch: **33 selected, 33 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

## Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 917.809        | 1507.415 | 1537.476 | 14.848             | 845.346         | 13.200              | 19               | 64             | 4716.293   | 15      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 897.031        | 1369.390 | 1379.021 | 14.576             | 813.458         | 13.040              | 18               | 62             | 4713.132   | 15      | 0        | 0                   | MET             | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":28080,"leftPath":"NativeOriginal","referenceFrame":28411,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":5,"maximumObservedFrames":5}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":31965,"leftPath":"NativeOriginal","referenceFrame":32297,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16955.656 |  16833.477 |      -122.18 |      17166.988 |    17165.609 |         -1.379 |    17102.121 |   16749.59 |     -352.531 |                   n.d. |
| System commit MiB                  |    54667.652 |  54357.879 |     -309.773 |      54701.426 |    54678.758 |        -22.668 |    54705.961 |  54517.113 |     -188.848 |                   n.d. |
| DXGI process usage MiB             |     5661.031 |   3865.539 |    -1795.492 |       4123.102 |     4017.301 |       -105.801 |     4066.039 |    3675.57 |     -390.469 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        261 |          261 |            261 |          261 |              0 |            0 |        211 |          211 |                  0.808 |
| Estimated live tracked texture MiB |            0 |   2430.021 |     2430.021 |       2430.021 |     2430.021 |              0 |            0 |   2274.931 |     2274.931 |                  0.936 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -122.1796875,
        "systemCommitMiB": -309.7734375,
        "dxgiUsageMiB": -1795.4921875,
        "liveTextures": 261,
        "liveTextureMiB": 2430.020969390869
    },
    "pass2": {
        "processPrivateMiB": -352.53125,
        "systemCommitMiB": -188.84765625,
        "dxgiUsageMiB": -390.46875,
        "liveTextures": 211,
        "liveTextureMiB": 2274.9311332702637
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
| nvidia |    1 |  14 |             93 | complete | 0 / 0.1159       | 1 / 61.4521      | 0 / 0.2734      | 0 / 0.0392        | 0 / 17.7706       | none    |
| nvidia |    1 |  15 |            113 | complete | 0 / 0.0462       | 1 / 55.3772      | 0 / 0.1183      | 0 / 0.0558        | 0 / 17.2062       | none    |
| nvidia |    1 |  16 |            131 | complete | 0 / 0.1010       | 1 / 55.5473      | 0 / 0.2539      | 0 / 0.0487        | 0 / 15.6731       | none    |
| nvidia |    1 |  17 |            149 | complete | 0 / 0.2777       | 1 / 62.4938      | 0 / 0.2886      | 0 / 0.0412        | 0 / 13.4017       | none    |
| nvidia |    1 |  18 |            167 | complete | 0 / 0.0672       | 1 / 56.3191      | 0 / 0.2257      | 0 / 0.0392        | 0 / 12.6118       | none    |
| nvidia |    1 |  19 |            185 | complete | 0 / 0.0861       | 1 / 57.8773      | 0 / 0.3019      | 0 / 0.0451        | 0 / 12.7285       | none    |
| nvidia |    1 |  25 |            219 | complete | 0 / 0.0753       | 1 / 54.0432      | 0 / 0.3738      | 0 / 0.1232        | 0 / 38.8119       | none    |
| nvidia |    1 |  26 |            240 | complete | 0 / 0.0643       | 1 / 63.6191      | 0 / 0.0989      | 0 / 0.0777        | 0 / 62.3291       | none    |
| nvidia |    1 |  29 |            266 | complete | 0 / 0.0766       | 1 / 52.8176      | 0 / 0.1523      | 0 / 0.1089        | 0 / 32.4049       | none    |
| nvidia |    2 |  14 |             93 | complete | 0 / 0.0602       | 1 / 56.0234      | 0 / 0.2732      | 0 / 0.0192        | 0 / 17.9427       | none    |
| nvidia |    2 |  15 |            113 | complete | 0 / 0.0673       | 1 / 54.3861      | 0 / 0.1779      | 0 / 0.0673        | 0 / 18.0390       | none    |
| nvidia |    2 |  16 |            131 | complete | 0 / 0.0565       | 1 / 53.6688      | 0 / 0.1496      | 0 / 0.0437        | 0 / 15.1355       | none    |
| nvidia |    2 |  17 |            149 | complete | 0 / 0.0956       | 1 / 55.7937      | 0 / 0.1832      | 0 / 0.1002        | 0 / 13.4726       | none    |
| nvidia |    2 |  18 |            167 | complete | 0 / 0.1458       | 1 / 63.4004      | 0 / 0.1600      | 0 / 0.0407        | 0 / 13.8143       | none    |
| nvidia |    2 |  19 |            185 | complete | 0 / 0.1072       | 1 / 64.2224      | 0 / 0.1587      | 0 / 0.0381        | 0 / 13.0197       | none    |
| nvidia |    2 |  25 |            219 | complete | 0 / 0.0717       | 1 / 55.0545      | 0 / 0.2433      | 0 / 0.1152        | 0 / 59.1447       | none    |
| nvidia |    2 |  26 |            240 | complete | 0 / 0.0582       | 1 / 55.6352      | 0 / 0.1769      | 0 / 0.0909        | 0 / 61.9297       | none    |
| nvidia |    2 |  29 |            266 | complete | 0 / 0.0830       | 1 / 54.9613      | 0 / 0.3025      | 0 / 0.1070        | 0 / 34.0039       | none    |

## Owned release stages

Guard exemption does not enable vendor dispatch. Provider preparation and coherent stereo still gate promotion. Denied, revoked and unproven receipts do not establish proof-driven release. Intervals use producer CPU observations; fence readiness is not the exact GPU completion time. blockingCleanupReadyQpc observes cleanup-ownership readiness, not completion of detached retirement fences. Displayed milliseconds are rounded to two decimals; exact QPC, frame endpoints and full precision remain in summary.json and transitions.csv.

| Lane   | Pass | Row | Certificate | Guard exempt | Request to admission ms | All ready to consumed ms | Consumed to published ms | Published to provider prepared ms | Provider prepared to promoted ms | Gaps |
| ------ | ---: | --: | ----------- | ------------ | ----------------------: | -----------------------: | -----------------------: | --------------------------------: | -------------------------------: | ---- |
| nvidia |    1 |  14 | revoked     | false        |                  378.53 |                     0.38 |                    17.68 |                             64.83 |                           306.95 | none |
| nvidia |    1 |  15 | complete    | true         |                  369.57 |                     0.22 |                    17.11 |                             61.23 |                           129.35 | none |
| nvidia |    1 |  16 | complete    | true         |                  384.61 |                     0.35 |                    15.58 |                             58.16 |                           131.38 | none |
| nvidia |    1 |  17 | complete    | true         |                  382.62 |                     0.38 |                    13.32 |                             62.08 |                           129.73 | none |
| nvidia |    1 |  18 | complete    | true         |                  374.91 |                     0.31 |                    12.54 |                             62.00 |                           129.44 | none |
| nvidia |    1 |  19 | complete    | true         |                  394.40 |                     0.40 |                    12.64 |                             62.73 |                           126.90 | none |
| nvidia |    1 |  25 | complete    | true         |                  363.76 |                    20.88 |                    18.31 |                             60.25 |                           132.45 | none |
| nvidia |    1 |  26 | complete    | true         |                  365.57 |                     0.56 |                    61.88 |                             67.93 |                           145.41 | none |
| nvidia |    1 |  29 | complete    | true         |                  398.22 |                    19.61 |                    12.95 |                             61.33 |                           129.96 | none |
| nvidia |    2 |  14 | revoked     | false        |                  363.42 |                     0.35 |                    17.88 |                             63.29 |                           305.37 | none |
| nvidia |    2 |  15 | complete    | true         |                  372.87 |                     0.30 |                    17.92 |                             61.85 |                           134.08 | none |
| nvidia |    2 |  16 | complete    | true         |                  371.14 |                     0.24 |                    15.05 |                             59.31 |                           134.55 | none |
| nvidia |    2 |  17 | complete    | true         |                  380.57 |                     0.34 |                    13.32 |                             62.56 |                           132.84 | none |
| nvidia |    2 |  18 | complete    | true         |                  419.01 |                     0.25 |                    13.74 |                             68.65 |                           137.56 | none |
| nvidia |    2 |  19 | complete    | true         |                  386.58 |                     0.25 |                    12.94 |                             62.83 |                           130.00 | none |
| nvidia |    2 |  25 | complete    | true         |                  381.03 |                    25.24 |                    34.16 |                             70.12 |                           144.67 | none |
| nvidia |    2 |  26 | complete    | true         |                  373.41 |                     0.61 |                    61.51 |                             73.19 |                           142.70 | none |
| nvidia |    2 |  29 | complete    | true         |                  365.71 |                    21.52 |                    12.79 |                             65.08 |                           133.91 | none |

| Lane   | Pass | Row | Fence      | Issue to observed ready ms | Gaps |
| ------ | ---: | --: | ---------- | -------------------------: | ---- |
| nvidia |    1 |  14 | FSRHost    |                      61.52 | none |
| nvidia |    1 |  14 | FSRInterop |                      61.46 | none |
| nvidia |    1 |  14 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  15 | FSRHost    |                      55.38 | none |
| nvidia |    1 |  15 | FSRInterop |                      55.37 | none |
| nvidia |    1 |  15 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  16 | FSRHost    |                      55.61 | none |
| nvidia |    1 |  16 | FSRInterop |                      55.59 | none |
| nvidia |    1 |  16 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  17 | FSRHost    |                      62.72 | none |
| nvidia |    1 |  17 | FSRInterop |                      62.52 | none |
| nvidia |    1 |  17 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  18 | FSRHost    |                      56.35 | none |
| nvidia |    1 |  18 | FSRInterop |                      56.32 | none |
| nvidia |    1 |  18 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  19 | FSRHost    |                      57.92 | none |
| nvidia |    1 |  19 | FSRInterop |                      57.87 | none |
| nvidia |    1 |  19 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  25 | FSRHost    |                      54.07 | none |
| nvidia |    1 |  25 | FSRInterop |                      54.04 | none |
| nvidia |    1 |  25 | FSRRuntime |                       0.00 | none |
| nvidia |    1 |  26 | DLSSHost   |                      63.66 | none |
| nvidia |    1 |  29 | FSRHost    |                      52.85 | none |
| nvidia |    1 |  29 | FSRInterop |                      52.81 | none |
| nvidia |    1 |  29 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  14 | FSRHost    |                      56.04 | none |
| nvidia |    2 |  14 | FSRInterop |                      56.01 | none |
| nvidia |    2 |  14 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  15 | FSRHost    |                      54.41 | none |
| nvidia |    2 |  15 | FSRInterop |                      54.38 | none |
| nvidia |    2 |  15 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  16 | FSRHost    |                      53.69 | none |
| nvidia |    2 |  16 | FSRInterop |                      53.66 | none |
| nvidia |    2 |  16 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  17 | FSRHost    |                      55.84 | none |
| nvidia |    2 |  17 | FSRInterop |                      55.78 | none |
| nvidia |    2 |  17 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  18 | FSRHost    |                      63.50 | none |
| nvidia |    2 |  18 | FSRInterop |                      63.42 | none |
| nvidia |    2 |  18 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  19 | FSRHost    |                      64.28 | none |
| nvidia |    2 |  19 | FSRInterop |                      64.23 | none |
| nvidia |    2 |  19 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  25 | FSRHost    |                      55.08 | none |
| nvidia |    2 |  25 | FSRInterop |                      55.04 | none |
| nvidia |    2 |  25 | FSRRuntime |                       0.00 | none |
| nvidia |    2 |  26 | DLSSHost   |                      55.67 | none |
| nvidia |    2 |  29 | FSRHost    |                      54.99 | none |
| nvidia |    2 |  29 | FSRInterop |                      54.97 | none |
| nvidia |    2 |  29 | FSRRuntime |                       0.00 | none |

## Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 24789/649.9013 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 110.095 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 25102/1000.0278 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 125.610 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 25216/1165.8352 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 64.359 ms; SubmitStageFoveatedCenter: 64.186 ms | 214.050 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 25338/1316.7699 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 59.223 ms; SubmitStageFoveatedCenter: 59.035 ms | 208.294 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 25458/1286.1675 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 54.730 ms; SubmitStageFoveatedCenter: 54.480 ms | 206.002 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 25575/1245.9845 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.845 ms; SubmitStageFoveatedCenter: 52.617 ms | 198.184 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 25695/1168.7987 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 68 records / 3 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 26253/1154.7767 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 26370/989.5088 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 26486/1044.9117 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 26599/1015.2475 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 26711/994.2494 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 26829/1116.0583 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 26946/958.9452 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 109.432 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 27497/965.6714 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 27619/1069.36 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 106.502 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 27967/992.3409 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 130.859 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 29066/975.3127 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 109.036 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 2              | 29179/958.9024 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.151 ms; SubmitStageFoveatedCenter: 61.968 ms | 219.951 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 29296/1122.1321 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 59.181 ms; SubmitStageFoveatedCenter: 58.963 ms | 211.074 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 29415/1166.6981 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 0.947 ms; SubmitStageFoveatedCenter: 0.875 ms   | 272.365 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 29531/1094.5781 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 56.750 ms; SubmitStageFoveatedCenter: 56.305 ms | 201.295 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 29648/1044.8319 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 30197/1106.9796 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 30313/1002.9067 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 30428/1043.7841 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 30542/1016.979 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 30653/1065.7187 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 4              | 30761/950.1882 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 30874/1052.4259 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 120.556 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 31399/1023.5469 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 31513/963.7643 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 110.002 ms                     | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 31854/958.1658 ms  | not_needed | MATCHED   | none                | none             | none                      |
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
| nvidia |    1 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
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
