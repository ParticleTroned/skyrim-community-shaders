# NVIDIA tuning: PR73 269bded15, September 11

Displayed millisecond values are rounded to two decimal places. Full
precision is retained in the comparison ledger and raw evidence.

Run `nv-pr73-269bded1-mtwz6pez` completed both 33-transition NVIDIA passes. Execution is
**COMPLETE**, terminal render verdict **PASS**,
and Task 2 counts are **66 PASS / 0 FAIL / 0 INCONCLUSIVE**.
Full-history health is **NO_COUNTED_FAILURES**: pass 1: NO_COUNTED_FAILURES, applicable standard MET; pass 2: NO_COUNTED_FAILURES, applicable standard MET.
Reporting is **COMPLETE**; reasons:
none.
The independently assessed improvement-or-neutral results are pr66: INCONCLUSIVE (retained_context_not_matched:scene, retained_context_not_matched:toolchain, matching_fixture_fingerprint_unavailable, explicit_versioned_tolerance_policy_missing); previous_pr73: INCONCLUSIVE (retained_context_not_matched:scene, matching_fixture_fingerprint_unavailable, explicit_versioned_tolerance_policy_missing).
These classifications are separate; completion and passing terminal checks
do not establish a performance improvement. The imposed stretch cutoff is a
diagnostic, not an applicable health gate. Separate
`csx-render-scale-pr-v1` release qualification remains unrun.

## Build and retained evidence

The producer is clean Release source `269bded159c66f4d8b1a45a836078dfa6cdf3241`,
Build ID `9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b`. The exact enabled AIO is
`CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-269bded1-DevBench-no-cache`. Its physical DLL is 28,184,576
bytes with SHA-256 `086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a`. The adjacent manifest,
producer receipt and preserved runtime identity agree; the complete
identity checks and their limitations remain in `dll-identity-verification.json`.

All evidence is retained under `C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nv-pr73-269bded1-mtwz6pez`. The local canonical CSV at
`docs/development/vr-render-scale-comparison-ledger.csv` reconstructs the
complete corrected summary and both comparisons exactly, including
528 unique candidate numeric timing
cells and 1056 checked numeric
cells per comparison. Every historical cell is preserved. The exact source
hashes and audit are in `complete-ledger-validation.json` and the report
writer receipt.

The GitHub ledger snapshot already occupies
104,132,789 bytes. An estimate using the initial report and
existing lossless subtree-reference encoding already required
109,580,999 bytes, exceeding its
104,857,600-byte blob limit. Every new field
remains in the complete local canonical ledger; the repository-published
[published ledger snapshot](vr-render-scale-comparison-ledger.csv) does not
yet include this run. This report publishes the
current run and comparison tables without rewriting historical ledger cells.
The estimate and exact preservation checks remain in the evidence directory.

Finalization took 12.404 s. The PR66 and
prior-PR73 comparison wrappers took
3.148 and
3.137 s;
complete ledger preparation, reconstruction and update took
17.386 s. Exact per-stage timing receipts are retained.

## Owned drain and unchanged guard

Eighteen owned operations ran: rows 14-19, 25, 26 and 29 in both passes.
Each recorded one Pending event, one Backend retry, one commit and one
shared-cleanup completion marker. Readiness was observed one frame later,
45.33-56.79 ms after Pending. There are 22 provider-specific Ready
events because four operations observed both providers. No invalidation or
six-frame observation-budget expiry was exercised.

All 32 guard-arm events retain `minimumSettleFrames=6` and start in the
replacement's Applied frame. All 26 observed guard-satisfied events occur
at age six frames. The eight focused transitions retain Backend history,
`proofDrivenRelease=false` and `settleGuardRequired=true`; promotion follows
at age seven frames. The four earlier outliers become shorter, while their
previously short counterpart passes become longer. All eight focused
stretches remain four frames above PR66.

| Pass | Row | Prior c73 stretch frames / ms | Candidate stretch frames / ms | Prior / candidate strict ms |
| ---- | --- | ----------------------------- | ----------------------------- | --------------------------- |
| 1    | 15  | 3 / 139.39                    | 7 / 343.05                    | 727.24 / 926.43             |
| 1    | 18  | 3 / 141.29                    | 7 / 345.79                    | 735.19 / 1079.17            |
| 1    | 19  | 13 / 620.31                   | 7 / 338.28                    | 1220.77 / 975.33            |
| 1    | 26  | 2 / 114.71                    | 6 / 336.56                    | 954.92 / 1150.18            |
| 2    | 15  | 13 / 647.94                   | 7 / 371.79                    | 1256.79 / 977.10            |
| 2    | 18  | 13 / 648.80                   | 7 / 346.98                    | 1493.80 / 1087.06           |
| 2    | 19  | 3 / 149.21                    | 7 / 343.25                    | 771.31 / 981.55             |
| 2    | 26  | 11 / 638.02                   | 6 / 374.76                    | 1468.30 / 1207.49           |

The FSR-only outlier audit changes from six pre-mutation plus seven
post-mutation stretch cycles to one plus six. The guard age is unchanged;
presentation-cycle counts and guard age are distinct measurements. Row 26
contains two stretch episodes totaling six frames in each candidate pass,
one before and five after mutation.

| Pass | Row | Pending to Ready frames / ms | Ready to commit ms | Commit to Applied ms | Guard to satisfied frames / ms |
| ---- | --- | ---------------------------- | ------------------ | -------------------- | ------------------------------ |
| 1    | 15  | 1 / 45.33                    | 0.30               | 26.26                | 6 / 273.81                     |
| 1    | 18  | 1 / 46.81                    | 0.15               | 12.83                | 6 / 288.78                     |
| 1    | 19  | 1 / 46.83                    | 0.18               | 13.38                | 6 / 282.68                     |
| 1    | 26  | 1 / 46.91                    | 0.11               | 61.10                | 6 / 294.64                     |
| 2    | 15  | 1 / 56.79                    | 0.25               | 31.19                | 6 / 295.01                     |
| 2    | 18  | 1 / 48.14                    | 0.14               | 12.95                | 6 / 290.53                     |
| 2    | 19  | 1 / 51.14                    | 0.23               | 13.76                | 6 / 285.98                     |
| 2    | 26  | 1 / 48.23                    | 0.22               | 63.03                | 6 / 331.84                     |

Intervals use saved QPC ticks and their producer frequency, correlated to
the immutable request and transition epoch. Ready means the last observed
provider-ready marker before commit, since one provider can become ready
before the operation's Pending event. These are observed intervals, not the
exact GPU completion instant. Commit-to-cleanup is elapsed time to a
completion marker, not isolated cleanup cost. Source generation, required
provider set and resource revision are not serialized in these event rows;
they must not be inferred from the target generation or poll counter.

## Reporter compatibility and lifecycle diagnostics

The original finalizer recognized only the older retry-event vocabulary.
New drain events made its cumulative capture validation report
`invalid_event`; that reporting defect did not erase the saved measurements
or change their terminal outcome. Corrected reporting is generated offline
from the same retained evidence. The prior report and comparisons remain
under `reporting-before-protocol-update`; no transitions were replayed to
repair this reporter gap.

The parser and protocol repair is committed in maintained
`skyrim-vr-automation` dev as
`c900f176eadcf6bc35cc4c8ffb64e564156fc367`. Parser, finalizer and comparison
Node tests, source/package protocol parity and skill validation passed.
`reporting-correction-provenance.json` pins the exact reporter commit,
source hashes and unchanged retained-input hashes. All 18 owned-operation
interval rows are published in the generated "Owned provider drain and
commit intervals" section below; the eight rows above are the focused
comparison requested for the earlier outliers.

An earlier startup handoff, `nv-pr73-269bded1-mtwywzvs`, was blocked before
measured mutation by the dead `cf1616728` crash-run endpoint lock. The exact
lock and diagnostics were preserved and its retirement was explicitly
authorized before this fresh run. This startup failure remains separate
from the completed 66-transition result.

The worker verified owned captures inactive, flushed all queued evidence
and released its endpoint lock. At `2026-09-11T13:24:30.2812610Z`, the worker
was absent and the game was still present. A later read-only observation at
`2026-09-11T13:29:18.0126197Z` found the expected game and MO2 processes
absent; the verifier performed no lifecycle action. No cause or orderly
shutdown is established for that later exit. The complete-run capture
cleanup evidence remains valid. Runtime modules were not freshly queried
after exit; RootBuilder's transient BuildData had also disappeared before
a persistent snapshot could be retained. These limits remain in the DLL
identity report.

## Complete generated run report

## renderscale-tuning-nvidia final report

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

### Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms  | Max ms  | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | ------- | ------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 822.55         | 1301.25 | 1356.82 | 15.455             | 774.29          | 14.120              | 19               | 88             | 5331.10    | 15      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 808.63         | 1210.58 | 1260.14 | 15.636             | 742.53          | 14.120              | 18               | 86             | 5210.46    | 15      | 0        | 0                   | MET             | COMPLETE |

#### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":28860,"leftPath":"NativeOriginal","referenceFrame":29241,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

#### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":33440,"leftPath":"NativeOriginal","referenceFrame":33821,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

### Memory confirmation

#### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000.00 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16922.848 |  16564.875 |     -357.973 |      16912.387 |    16910.145 |         -2.242 |     16923.23 |  16653.371 |     -269.859 |                   n.d. |
| System commit MiB                  |    56564.035 |  56345.586 |     -218.449 |      56703.375 |    56708.785 |           5.41 |     56674.77 |  56488.539 |      -186.23 |                   n.d. |
| DXGI process usage MiB             |     4479.469 |   3604.316 |     -875.152 |       3861.879 |     3861.879 |              0 |     3819.496 |   3605.035 |     -214.461 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        213 |          213 |            213 |          213 |              0 |            0 |        225 |          225 |                  1.056 |
| Estimated live tracked texture MiB |            0 |   2271.056 |     2271.056 |       2271.056 |     2271.056 |              0 |            0 |   2288.473 |     2288.473 |                  1.008 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -357.97265625,
        "systemCommitMiB": -218.44921875,
        "dxgiUsageMiB": -875.15234375,
        "liveTextures": 213,
        "liveTextureMiB": 2271.0563163757324
    },
    "pass2": {
        "processPrivateMiB": -269.859375,
        "systemCommitMiB": -186.23046875,
        "dxgiUsageMiB": -214.4609375,
        "liveTextures": 225,
        "liveTextureMiB": 2288.473041534424
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

### Owned provider drain and commit intervals

Cells show frames / milliseconds from exact retained QPC endpoints. Ready means the last observed provider-ready event before the first commit; the required-provider set is not exposed. Commit-to-shared-cleanup is elapsed time to that marker, not isolated cleanup cost. Subsequent commit attempts, raw poll counts and missing identity fields remain in summary.json and CSV. The six-frame settling guard is reported separately.

| Lane   | Pass | Row | Begin sequence | Status   | Begin to pending | Pending to ready | Ready to commit | Commit to cleanup | Commit to applied | Reasons |
| ------ | ---: | --: | -------------: | -------- | ---------------- | ---------------- | --------------- | ----------------- | ----------------- | ------- |
| nvidia |    1 |  14 |             93 | complete | 0 / 0.09         | 1 / 52.91        | 0 / 0.15        | 0 / 0.02          | 0 / 29.61         | none    |
| nvidia |    1 |  15 |            108 | complete | 0 / 0.15         | 1 / 45.33        | 0 / 0.30        | 0 / 0.05          | 0 / 26.26         | none    |
| nvidia |    1 |  16 |            123 | complete | 0 / 0.10         | 1 / 47.77        | 0 / 0.24        | 0 / 0.06          | 0 / 15.70         | none    |
| nvidia |    1 |  17 |            138 | complete | 0 / 0.06         | 1 / 52.42        | 0 / 0.24        | 0 / 0.07          | 0 / 13.82         | none    |
| nvidia |    1 |  18 |            153 | complete | 0 / 0.06         | 1 / 46.81        | 0 / 0.15        | 0 / 0.04          | 0 / 12.83         | none    |
| nvidia |    1 |  19 |            168 | complete | 0 / 0.14         | 1 / 46.83        | 0 / 0.18        | 0 / 0.07          | 0 / 13.38         | none    |
| nvidia |    1 |  25 |            199 | complete | 0 / 0.15         | 1 / 46.67        | 0 / 0.17        | 0 / 0.11          | 0 / 51.98         | none    |
| nvidia |    1 |  26 |            217 | complete | 0 / 0.11         | 1 / 46.91        | 0 / 0.11        | 0 / 0.09          | 0 / 61.10         | none    |
| nvidia |    1 |  29 |            240 | complete | 0 / 0.08         | 1 / 51.97        | 0 / 0.16        | 0 / 0.09          | 0 / 46.22         | none    |
| nvidia |    2 |  14 |             93 | complete | 0 / 0.10         | 1 / 47.13        | 0 / 0.25        | 0 / 0.02          | 0 / 31.08         | none    |
| nvidia |    2 |  15 |            108 | complete | 0 / 0.10         | 1 / 56.79        | 0 / 0.25        | 0 / 0.04          | 0 / 31.19         | none    |
| nvidia |    2 |  16 |            123 | complete | 0 / 0.07         | 1 / 47.08        | 0 / 0.21        | 0 / 0.04          | 0 / 31.13         | none    |
| nvidia |    2 |  17 |            138 | complete | 0 / 0.13         | 1 / 47.88        | 0 / 0.16        | 0 / 0.06          | 0 / 16.29         | none    |
| nvidia |    2 |  18 |            153 | complete | 0 / 0.07         | 1 / 48.14        | 0 / 0.14        | 0 / 0.04          | 0 / 12.95         | none    |
| nvidia |    2 |  19 |            168 | complete | 0 / 0.19         | 1 / 51.14        | 0 / 0.23        | 0 / 0.04          | 0 / 13.76         | none    |
| nvidia |    2 |  25 |            199 | complete | 0 / 0.06         | 1 / 46.10        | 0 / 0.25        | 0 / 0.13          | 0 / 54.18         | none    |
| nvidia |    2 |  26 |            217 | complete | 0 / 0.05         | 1 / 48.23        | 0 / 0.22        | 0 / 0.08          | 0 / 63.03         | none    |
| nvidia |    2 |  29 |            240 | complete | 0 / 0.07         | 1 / 45.63        | 0 / 0.16        | 0 / 0.10          | 0 / 49.78         | none    |

### Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                          | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | ------------------------------------------------------ | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ---------------- | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 2              | 25043/552.73 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                          | 97.82 ms                       | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 25417/839.82 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                          | 94.06 ms                       | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 25550/995.16 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.43 ms; SubmitStageFoveatedCenter: 62.17 ms | 182.65 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 25689/955.28 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 57.97 ms; SubmitStageFoveatedCenter: 57.62 ms | 187.40 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 25827/1102.23 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.44 ms; SubmitStageFoveatedCenter: 1.13 ms   | 252.80 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 25965/1139.85 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 56.92 ms; SubmitStageFoveatedCenter: 56.87 ms | 175.64 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 26103/1074.13 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 26744/920.47 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 26880/926.43 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 27014/983.36 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 27150/1050.89 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 27288/1079.17 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 27424/975.33 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 27556/849.47 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 232.04 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 5              | 28198/1002.37 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 28337/995.24 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 222.86 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 28733/1009.07 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                          | 106.29 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 30003/852.97 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                          | 96.85 ms                       | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 2              | 30134/848.64 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.12 ms; SubmitStageFoveatedCenter: 53.04 ms | 198.06 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 73 records / 3 pages | 5              | 30270/985.97 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 57.28 ms; SubmitStageFoveatedCenter: 57.19 ms | 173.12 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 30406/949.18 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 63.96 ms; SubmitStageFoveatedCenter: 63.75 ms | 170.65 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 30545/956.27 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 63.64 ms; SubmitStageFoveatedCenter: 63.44 ms | 176.36 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 5              | 30683/953.45 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 31331/916.48 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 31468/977.10 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 31607/1185.51 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 31742/993.63 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 31880/1087.06 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 7              | 32016/981.55 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 5              | 32149/847.75 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 233.70 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 5              | 32783/1072.85 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 32919/1063.64 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 223.07 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 33313/996.73 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none             | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none             | not_needed | MATCHED   | none                | none             | none                      |

### Presentation stretch anomalies

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
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    1 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  5 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  7 | yes            |
| nvidia |    2 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  5 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |

## Complete comparison with PR66

## Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                              |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              | nv-pr73-269bded1-mtwz6pez                                                              |
| Renderer base           | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | 269bded159c66f4d8b1a45a836078dfa6cdf3241                                               |
| Main-VR base/equivalent | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                               |
| Compiled source         | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | 269bded159c66f4d8b1a45a836078dfa6cdf3241                                               |
| Build ID                | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                | 9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b                       |
| DLL SHA-256             | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                | 086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a                       |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nv-pr73-269bded1-mtwz6pez |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C   | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | ------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 793.34/822.55 | 3.682        | 9/15        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 807.13/808.63 | 0.187        | 10/15       | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline | Candidate | Delta   | Delta % |
| ------ | ---- | -------------------------- | ----------- | -------- | --------- | ------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 735.10   | 774.29    | 39.19   | 5.332   |
| nvidia | 1    | Relatch proof mean         | frames      | 13.400   | 14.120    | 0.720   | 5.373   |
| nvidia | 1    | Relatch proof total        | ms          | 18377.38 | 19357.22  | 979.84  | 5.332   |
| nvidia | 1    | Relatch proof total        | frames      | 335      | 353       | 18      | 5.373   |
| nvidia | 1    | Relatch proof samples      | transitions | 25       | 25        | 0       | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 793.34   | 822.55    | 29.21   | 3.682   |
| nvidia | 1    | Strict completion mean     | frames      | 15.091   | 15.455    | 0.364   | 2.410   |
| nvidia | 1    | Strict completion total    | ms          | 26180.09 | 27144.05  | 963.96  | 3.682   |
| nvidia | 1    | Strict completion total    | frames      | 498      | 510       | 12      | 2.410   |
| nvidia | 1    | Stretch completed episodes | episodes    | 17       | 19        | 2       | 11.765  |
| nvidia | 1    | Stretch completed total    | frames      | 69       | 88        | 19      | 27.536  |
| nvidia | 1    | Stretch completed total    | ms          | 4143.53  | 5331.10   | 1187.57 | 28.661  |
| nvidia | 1    | Stretch longest episode    | ms          | 376.75   | 373.10    | -3.65   | -0.968  |
| nvidia | 2    | Relatch proof mean         | ms          | 747.35   | 742.53    | -4.82   | -0.645  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.760   | 14.120    | 0.360   | 2.616   |
| nvidia | 2    | Relatch proof total        | ms          | 18683.66 | 18563.23  | -120.43 | -0.645  |
| nvidia | 2    | Relatch proof total        | frames      | 344      | 353       | 9       | 2.616   |
| nvidia | 2    | Relatch proof samples      | transitions | 25       | 25        | 0       | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 807.13   | 808.63    | 1.51    | 0.187   |
| nvidia | 2    | Strict completion mean     | frames      | 15.303   | 15.636    | 0.333   | 2.178   |
| nvidia | 2    | Strict completion total    | ms          | 26635.15 | 26684.94  | 49.79   | 0.187   |
| nvidia | 2    | Strict completion total    | frames      | 505      | 516       | 11      | 2.178   |
| nvidia | 2    | Stretch completed episodes | episodes    | 18       | 18        | 0       | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 78       | 86        | 8       | 10.256  |
| nvidia | 2    | Stretch completed total    | ms          | 4955.68  | 5210.46   | 254.78  | 5.141   |
| nvidia | 2    | Stretch longest episode    | ms          | 408.50   | 371.79    | -36.72  | -8.988  |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 680.25 / 744.02   | 63.77    | 9.375   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.47 / 168.17   | -1.30    | -0.765  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 250.23 / 249.96   | -0.26    | -0.105  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 911.53 / 1009.43  | 97.91    | 10.741  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1151.04 / 1209.11 | 58.07    | 5.045   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1085.15 / 1171.55 | 86.41    | 7.963   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1311.36 / 1319.77 | 8.40     | 0.641   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1267.60 / 1356.82 | 89.22    | 7.039   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1257.58 / 1288.90 | 31.31    | 2.490   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 760.85 / 775.05   | 14.20    | 1.867   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 463.24 / 479.92   | 16.69    | 3.602   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 171.24 / 172.78   | 1.54     | 0.901   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 747.17 / 809.24   | 62.07    | 8.307   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1350.98 / 1098.56 | -252.42  | -18.684 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 903.65 / 926.43   | 22.78    | 2.520   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 731.84 / 983.36   | 251.52   | 34.368  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 719.44 / 1050.89  | 331.45   | 46.071  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 760.87 / 1079.17  | 318.30   | 41.834  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 781.36 / 975.33   | 193.97   | 24.825  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 739.77 / 1028.87  | 289.10   | 39.080  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 463.47 / 481.52   | 18.06    | 3.896   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 170.55 / 170.78   | 0.23     | 0.136   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 263.13 / 301.22   | 38.09    | 14.477  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1184.66 / 808.22  | -376.44  | -31.776 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1856.76 / 1222.45 | -634.31  | -34.162 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 932.91 / 1150.18  | 217.27   | 23.290  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 751.64 / 761.69   | 10.04    | 1.336   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 910.66 / 920.99   | 10.32    | 1.134   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1446.69 / 1233.79 | -212.90  | -14.717 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 681.04 / 780.67   | 99.64    | 14.630  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 577.39 / 636.91   | 59.52    | 10.308  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 450.36 / 516.36   | 66.00    | 14.656  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 276.26 / 261.97   | -14.29   | -5.174  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                        |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.63 / 552.73   | 680.25 / 744.02   | 178.62 / 191.29  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.2207,"dispatchToBlockedOrPreparationMs":362.6825,"firstNewGenerationToCleanupDrainedMs":242.4389,"firstPhysicalMutationToFirstNewGenerationMs":92.6807,"presentationToStrictCompletionMs":191.2904}  |
| 2   | 169.47 / 168.17   | 169.47 / 168.17   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":168.1689,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 250.23 / 249.96   | 250.23 / 249.96   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.964,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 789.45 / 839.82   | 829.73 / 924.90   | 40.28 / 85.08    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.1203,"dispatchToBlockedOrPreparationMs":361.1041,"firstNewGenerationToCleanupDrainedMs":172.6253,"firstPhysicalMutationToFirstNewGenerationMs":345.0476,"presentationToStrictCompletionMs":169.6118} |
| 5   | 945.18 / 995.16   | 1068.44 / 1124.31 | 123.26 / 129.15  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   | {"blockedOrPreparationToFirstPhysicalMutationMs":55.0599,"dispatchToBlockedOrPreparationMs":342.7084,"firstNewGenerationToCleanupDrainedMs":172.2143,"firstPhysicalMutationToFirstNewGenerationMs":554.3277,"presentationToStrictCompletionMs":213.9487} |
| 6   | 877.56 / 955.28   | 1005.16 / 1087.78 | 127.60 / 132.50  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1022,"dispatchToBlockedOrPreparationMs":382.1632,"firstNewGenerationToCleanupDrainedMs":175.3795,"firstPhysicalMutationToFirstNewGenerationMs":483.1375,"presentationToStrictCompletionMs":216.2703} |
| 7   | 1092.61 / 1102.23 | 1227.59 / 1234.77 | 134.97 / 132.53  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.4041,"dispatchToBlockedOrPreparationMs":349.0306,"firstNewGenerationToCleanupDrainedMs":176.0055,"firstPhysicalMutationToFirstNewGenerationMs":662.3272,"presentationToStrictCompletionMs":217.5327} |
| 8   | 1048.37 / 1139.85 | 1186.92 / 1271.88 | 138.56 / 132.03  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    | {"blockedOrPreparationToFirstPhysicalMutationMs":49.8214,"dispatchToBlockedOrPreparationMs":344.7531,"firstNewGenerationToCleanupDrainedMs":175.8538,"firstPhysicalMutationToFirstNewGenerationMs":701.4561,"presentationToStrictCompletionMs":216.9671} |
| 9   | 1031.84 / 1074.13 | 1174.67 / 1209.27 | 142.83 / 135.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.0548,"dispatchToBlockedOrPreparationMs":349.1578,"firstNewGenerationToCleanupDrainedMs":178.2302,"firstPhysicalMutationToFirstNewGenerationMs":634.8221,"presentationToStrictCompletionMs":214.7668} |
| 10  | 590.58 / 642.71   | 760.85 / 775.05   | 170.27 / 132.34  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5962,"dispatchToBlockedOrPreparationMs":395.5892,"firstNewGenerationToCleanupDrainedMs":176.6887,"firstPhysicalMutationToFirstNewGenerationMs":202.1765,"presentationToStrictCompletionMs":132.3433}  |
| 11  | 197.19 / 172.00   | 463.24 / 479.92   | 266.05 / 307.93  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5808,"dispatchToBlockedOrPreparationMs":128.4443,"firstNewGenerationToCleanupDrainedMs":309.1395,"firstPhysicalMutationToFirstNewGenerationMs":38.7575,"presentationToStrictCompletionMs":307.925}    |
| 12  | 171.24 / 172.78   | 171.24 / 172.78   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7824,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 747.17 / 809.24   | 747.17 / 809.24   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":197.745,"dispatchToBlockedOrPreparationMs":438.364,"firstNewGenerationToCleanupDrainedMs":43.1553,"firstPhysicalMutationToFirstNewGenerationMs":129.9709,"presentationToStrictCompletionMs":0}          |
| 14  | 1222.44 / 920.47  | 1306.23 / 1051.77 | 83.79 / 131.30   | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  | {"blockedOrPreparationToFirstPhysicalMutationMs":53.0763,"dispatchToBlockedOrPreparationMs":398.3695,"firstNewGenerationToCleanupDrainedMs":174.5609,"firstPhysicalMutationToFirstNewGenerationMs":425.7673,"presentationToStrictCompletionMs":178.0895} |
| 15  | 903.65 / 926.43   | 561.96 / 836.45   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":46.2245,"dispatchToBlockedOrPreparationMs":404.3924,"firstNewGenerationToCleanupDrainedMs":42.9698,"firstPhysicalMutationToFirstNewGenerationMs":342.8622,"presentationToStrictCompletionMs":0}         |
| 16  | 731.84 / 983.36   | 602.54 / 882.80   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":48.5628,"dispatchToBlockedOrPreparationMs":415.6594,"firstNewGenerationToCleanupDrainedMs":45.9667,"firstPhysicalMutationToFirstNewGenerationMs":372.6063,"presentationToStrictCompletionMs":0}         |
| 17  | 719.44 / 1050.89  | 587.41 / 825.86   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":53.6722,"dispatchToBlockedOrPreparationMs":409.8709,"firstNewGenerationToCleanupDrainedMs":0.1596,"firstPhysicalMutationToFirstNewGenerationMs":362.1534,"presentationToStrictCompletionMs":0}          |
| 18  | 760.87 / 1079.17  | 630.63 / 838.29   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":47.7944,"dispatchToBlockedOrPreparationMs":400.8995,"firstNewGenerationToCleanupDrainedMs":43.7485,"firstPhysicalMutationToFirstNewGenerationMs":345.8437,"presentationToStrictCompletionMs":0}         |
| 19  | 781.36 / 975.33   | 650.46 / 841.37   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":47.6092,"dispatchToBlockedOrPreparationMs":413.012,"firstNewGenerationToCleanupDrainedMs":42.5309,"firstPhysicalMutationToFirstNewGenerationMs":338.2159,"presentationToStrictCompletionMs":0}          |
| 20  | 530.87 / 849.47   | 739.77 / 1028.87  | 208.90 / 179.39  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   | {"blockedOrPreparationToFirstPhysicalMutationMs":254.0958,"dispatchToBlockedOrPreparationMs":442.0118,"firstNewGenerationToCleanupDrainedMs":242.3936,"firstPhysicalMutationToFirstNewGenerationMs":90.3663,"presentationToStrictCompletionMs":179.3937} |
| 21  | 205.53 / 214.61   | 463.47 / 481.52   | 257.94 / 266.91  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.5939,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":266.9111}            |
| 22  | 170.55 / 170.78   | 170.55 / 170.78   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7807,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 263.13 / 301.22   | 263.13 / 301.22   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":301.2181,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 993.04 / 669.04   | 1184.66 / 808.22  | 191.62 / 139.17  | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} | {"blockedOrPreparationToFirstPhysicalMutationMs":54.8492,"dispatchToBlockedOrPreparationMs":415.4329,"firstNewGenerationToCleanupDrainedMs":183.5175,"firstPhysicalMutationToFirstNewGenerationMs":154.4173,"presentationToStrictCompletionMs":139.1725} |
| 25  | 1645.74 / 1002.37 | 1774.36 / 1138.75 | 128.62 / 136.38  | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  | {"blockedOrPreparationToFirstPhysicalMutationMs":32.0675,"dispatchToBlockedOrPreparationMs":445.7745,"firstNewGenerationToCleanupDrainedMs":181.7968,"firstPhysicalMutationToFirstNewGenerationMs":479.1118,"presentationToStrictCompletionMs":220.0764} |
| 26  | 803.46 / 995.24   | 888.89 / 1099.66  | 85.43 / 104.42   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.9174,"dispatchToBlockedOrPreparationMs":399.4277,"firstNewGenerationToCleanupDrainedMs":198.5605,"firstPhysicalMutationToFirstNewGenerationMs":454.749,"presentationToStrictCompletionMs":154.9414}  |
| 27  | 490.91 / 579.55   | 751.64 / 761.69   | 260.73 / 182.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      | {"blockedOrPreparationToFirstPhysicalMutationMs":0.6327,"dispatchToBlockedOrPreparationMs":411.3025,"firstNewGenerationToCleanupDrainedMs":237.287,"firstPhysicalMutationToFirstNewGenerationMs":112.4631,"presentationToStrictCompletionMs":182.1399}   |
| 28  | 810.42 / 920.99   | 860.95 / 782.78   | 50.54 / 0.00     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0429,"dispatchToBlockedOrPreparationMs":403.8252,"firstNewGenerationToCleanupDrainedMs":44.9347,"firstPhysicalMutationToFirstNewGenerationMs":284.9767,"presentationToStrictCompletionMs":0}         |
| 29  | 1218.38 / 1009.07 | 1356.00 / 1150.31 | 137.63 / 141.23  | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} | {"blockedOrPreparationToFirstPhysicalMutationMs":32.4908,"dispatchToBlockedOrPreparationMs":460.9697,"firstNewGenerationToCleanupDrainedMs":196.496,"firstPhysicalMutationToFirstNewGenerationMs":460.3491,"presentationToStrictCompletionMs":224.717}   |
| 30  | 492.15 / 582.21   | 681.04 / 780.67   | 188.88 / 198.46  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.0268,"dispatchToBlockedOrPreparationMs":393.313,"firstNewGenerationToCleanupDrainedMs":266.9159,"firstPhysicalMutationToFirstNewGenerationMs":73.4185,"presentationToStrictCompletionMs":198.4598}   |
| 31  | 577.39 / 636.91   | 577.39 / 636.91   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0217,"dispatchToBlockedOrPreparationMs":434.0515,"firstNewGenerationToCleanupDrainedMs":44.7224,"firstPhysicalMutationToFirstNewGenerationMs":110.1155,"presentationToStrictCompletionMs":0}         |
| 32  | 184.48 / 229.92   | 450.36 / 516.36   | 265.87 / 286.44  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":150.4586,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.4359}            |
| 33  | 276.26 / 261.97   | 276.26 / 261.97   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.9674,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 451.45 / 501.58      | 50.14    | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 13                  | 1            | 670.42 / 752.27      | 81.85    | 17 / 18           | 1            |
| 5   | 12 / 14                  | 2            | 904.85 / 952.10      | 47.25    | 17 / 19           | 2            |
| 6   | 16 / 18                  | 2            | 830.73 / 912.40      | 81.68    | 21 / 23           | 2            |
| 7   | 16 / 16                  | 0            | 1035.32 / 1058.76    | 23.45    | 21 / 21           | 0            |
| 8   | 16 / 17                  | 1            | 996.63 / 1096.03     | 99.40    | 21 / 22           | 1            |
| 9   | 16 / 16                  | 0            | 990.31 / 1031.04     | 40.73    | 21 / 21           | 0            |
| 10  | 10 / 10                  | 0            | 545.92 / 598.36      | 52.45    | 15 / 15           | 0            |
| 11  | 4 / 3                    | -1           | 195.94 / 170.78      | -25.16   | 11 / 9            | -2           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 10                   | 1            | 706.07 / 766.08      | 60.01    | 10 / 11           | 1            |
| 14  | 24 / 18                  | -6           | 1133.80 / 877.21     | -256.59  | 29 / 23           | -6           |
| 15  | 11 / 17                  | 6            | 561.66 / 793.48      | 231.82   | 19 / 20           | 1            |
| 16  | 12 / 17                  | 5            | 559.84 / 836.83      | 276.99   | 16 / 20           | 4            |
| 17  | 12 / 17                  | 5            | 545.16 / 825.70      | 280.54   | 16 / 22           | 6            |
| 18  | 12 / 17                  | 5            | 584.20 / 794.54      | 210.34   | 16 / 23           | 7            |
| 19  | 12 / 17                  | 5            | 606.13 / 798.84      | 192.71   | 16 / 21           | 5            |
| 20  | 10 / 16                  | 6            | 477.82 / 786.47      | 308.65   | 15 / 21           | 6            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 24  | 16 / 11                  | -5           | 935.37 / 624.70      | -310.67  | 21 / 15           | -6           |
| 25  | 29 / 18                  | -11          | 1603.95 / 956.95     | -646.99  | 34 / 23           | -11          |
| 26  | 13 / 18                  | 5            | 721.22 / 901.09      | 179.88   | 18 / 23           | 5            |
| 27  | 10 / 10                  | 0            | 490.10 / 524.40      | 34.30    | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 673.45 / 737.85      | 64.40    | 16 / 15           | -1           |
| 29  | 23 / 18                  | -5           | 1173.44 / 953.81     | -219.63  | 28 / 23           | -5           |
| 30  | 10 / 11                  | 1            | 445.61 / 513.76      | 68.15    | 15 / 17           | 2            |
| 31  | 9 / 10                   | 1            | 538.00 / 592.19      | 54.19    | 10 / 11           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C  | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | --------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 2              | 1     | 119.89 / 139.19 | 19.30    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 199.80 / 201.34 | 1.54     |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 199.32 / 199.52 | 0.20     |
| 6   | 1 / 1                | 0     | 6 / 5              | -1    | 371.55 / 339.45 | -32.10   |
| 7   | 1 / 1                | 0     | 6 / 5              | -1    | 376.75 / 342.12 | -34.63   |
| 8   | 1 / 1                | 0     | 6 / 5              | -1    | 366.69 / 364.07 | -2.62    |
| 9   | 1 / 1                | 0     | 6 / 5              | -1    | 370.25 / 354.19 | -16.05   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 5              | -1    | 268.92 / 221.16 | -47.77   |
| 15  | 1 / 1                | 0     | 3 / 7              | 4     | 142.70 / 343.05 | 200.35   |
| 16  | 1 / 1                | 0     | 3 / 7              | 4     | 160.24 / 373.10 | 212.86   |
| 17  | 1 / 1                | 0     | 3 / 7              | 4     | 140.16 / 362.82 | 222.66   |
| 18  | 1 / 1                | 0     | 3 / 7              | 4     | 148.35 / 345.79 | 197.44   |
| 19  | 1 / 1                | 0     | 3 / 7              | 4     | 154.85 / 338.28 | 183.44   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0.00 / 278.88   | 278.88   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 25  | 1 / 1                | 0     | 6 / 5              | -1    | 363.91 / 347.45 | -16.47   |
| 26  | 1 / 2                | 1     | 2 / 6              | 4     | 94.39 / 336.56  | 242.17   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 29  | 2 / 2                | 0     | 11 / 6             | -5    | 665.78 / 444.16 | -221.63  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 764.65 / 741.00   | -23.65   | -3.092  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 167.67 / 182.27   | 14.61    | 8.713   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 247.37 / 315.20   | 67.83    | 27.421  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1043.87 / 1027.93 | -15.94   | -1.527  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1038.18 / 1069.76 | 31.58    | 3.041   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1196.44 / 1207.43 | 10.99    | 0.919   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1202.54 / 1117.00 | -85.54   | -7.113  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1191.57 / 1135.64 | -55.93   | -4.694  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1123.08 / 1129.17 | 6.09     | 0.542   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 779.29 / 789.15   | 9.86     | 1.265   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 435.64 / 447.12   | 11.48    | 2.635   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 183.53 / 174.67   | -8.86    | -4.827  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 641.30 / 604.44   | -36.86   | -5.747  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.91 / 1097.57 | -280.35  | -20.346 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 896.17 / 977.10   | 80.93    | 9.030   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 702.99 / 1185.51  | 482.52   | 68.639  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 771.93 / 993.64   | 221.70   | 28.721  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 751.16 / 1087.06  | 335.89   | 44.717  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 727.32 / 981.55   | 254.23   | 34.954  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1078.03 / 1037.86 | -40.17   | -3.726  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 501.45 / 473.69   | -27.76   | -5.536  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 169.90 / 202.86   | 32.95    | 19.396  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 250.71 / 300.15   | 49.43    | 19.718  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1133.45 / 812.88  | -320.57  | -28.283 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1543.61 / 1260.14 | -283.47  | -18.364 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 901.36 / 1207.49  | 306.13   | 33.963  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 819.34 / 794.44   | -24.90   | -3.039  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 984.50 / 883.79   | -100.71  | -10.229 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1932.15 / 1215.21 | -716.94  | -37.106 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 696.61 / 779.04   | 82.42    | 11.832  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 660.50 / 621.55   | -38.95   | -5.897  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 467.90 / 536.61   | 68.72    | 14.686  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 253.03 / 296.06   | 43.03    | 17.005  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 524.71 / 554.96   | 764.65 / 741.00   | 239.94 / 186.05  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   | {"blockedOrPreparationToFirstPhysicalMutationMs":49.8744,"dispatchToBlockedOrPreparationMs":364.4052,"firstNewGenerationToCleanupDrainedMs":239.9261,"firstPhysicalMutationToFirstNewGenerationMs":86.7972,"presentationToStrictCompletionMs":186.0469}   |
| 2   | 167.67 / 182.27   | 167.67 / 182.27   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.2739,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 247.37 / 315.20   | 247.37 / 315.20   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":315.1964,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 816.59 / 852.97   | 956.84 / 941.80   | 140.25 / 88.82   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3988,"dispatchToBlockedOrPreparationMs":372.6052,"firstNewGenerationToCleanupDrainedMs":177.1048,"firstPhysicalMutationToFirstNewGenerationMs":344.6858,"presentationToStrictCompletionMs":174.9528}  |
| 5   | 812.14 / 848.64   | 952.99 / 982.76   | 140.85 / 134.12  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0827,"dispatchToBlockedOrPreparationMs":399.5427,"firstNewGenerationToCleanupDrainedMs":178.7136,"firstPhysicalMutationToFirstNewGenerationMs":345.4225,"presentationToStrictCompletionMs":221.1181}  |
| 6   | 963.68 / 985.97   | 1108.47 / 1122.60 | 144.79 / 136.62  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2644,"dispatchToBlockedOrPreparationMs":400.457,"firstNewGenerationToCleanupDrainedMs":180.7301,"firstPhysicalMutationToFirstNewGenerationMs":493.1433,"presentationToStrictCompletionMs":221.4575}   |
| 7   | 965.79 / 949.18   | 1113.35 / 1036.10 | 147.56 / 86.92   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    | {"blockedOrPreparationToFirstPhysicalMutationMs":48.8175,"dispatchToBlockedOrPreparationMs":344.1407,"firstNewGenerationToCleanupDrainedMs":173.8925,"firstPhysicalMutationToFirstNewGenerationMs":469.2451,"presentationToStrictCompletionMs":167.8205}  |
| 8   | 944.88 / 956.27   | 1097.89 / 1050.64 | 153.02 / 94.37   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    | {"blockedOrPreparationToFirstPhysicalMutationMs":48.4996,"dispatchToBlockedOrPreparationMs":350.9687,"firstNewGenerationToCleanupDrainedMs":189.0992,"firstPhysicalMutationToFirstNewGenerationMs":462.0671,"presentationToStrictCompletionMs":179.3663}  |
| 9   | 892.41 / 953.45   | 1033.09 / 1042.01 | 140.69 / 88.57   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6635,"dispatchToBlockedOrPreparationMs":354.4957,"firstNewGenerationToCleanupDrainedMs":176.6761,"firstPhysicalMutationToFirstNewGenerationMs":464.1759,"presentationToStrictCompletionMs":175.7231}  |
| 10  | 604.63 / 657.21   | 779.29 / 789.15   | 174.66 / 131.94  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   | {"blockedOrPreparationToFirstPhysicalMutationMs":45.5912,"dispatchToBlockedOrPreparationMs":354.5912,"firstNewGenerationToCleanupDrainedMs":175.6807,"firstPhysicalMutationToFirstNewGenerationMs":213.2819,"presentationToStrictCompletionMs":131.9389}  |
| 11  | 163.34 / 169.79   | 435.64 / 447.12   | 272.29 / 277.32  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0317,"dispatchToBlockedOrPreparationMs":126.896,"firstNewGenerationToCleanupDrainedMs":278.2949,"firstPhysicalMutationToFirstNewGenerationMs":37.8926,"presentationToStrictCompletionMs":277.3232}     |
| 12  | 183.53 / 174.67   | 183.53 / 174.67   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.6685,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 641.30 / 604.44   | 641.30 / 604.44   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2125,"dispatchToBlockedOrPreparationMs":415.7236,"firstNewGenerationToCleanupDrainedMs":42.9494,"firstPhysicalMutationToFirstNewGenerationMs":97.5588,"presentationToStrictCompletionMs":0}           |
| 14  | 1171.62 / 916.48  | 1323.72 / 1050.96 | 152.10 / 134.48  | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3492,"dispatchToBlockedOrPreparationMs":410.0617,"firstNewGenerationToCleanupDrainedMs":177.5564,"firstPhysicalMutationToFirstNewGenerationMs":415.9909,"presentationToStrictCompletionMs":181.0888}  |
| 15  | 896.17 / 977.10   | 558.99 / 883.84   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":57.7999,"dispatchToBlockedOrPreparationMs":411.0873,"firstNewGenerationToCleanupDrainedMs":43.4123,"firstPhysicalMutationToFirstNewGenerationMs":371.5409,"presentationToStrictCompletionMs":0}          |
| 16  | 702.99 / 1185.51  | 611.69 / 862.33   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":48.4907,"dispatchToBlockedOrPreparationMs":407.2276,"firstNewGenerationToCleanupDrainedMs":46.1316,"firstPhysicalMutationToFirstNewGenerationMs":360.4811,"presentationToStrictCompletionMs":0}          |
| 17  | 771.93 / 993.64   | 595.30 / 846.74   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0873,"dispatchToBlockedOrPreparationMs":402.1547,"firstNewGenerationToCleanupDrainedMs":44.3475,"firstPhysicalMutationToFirstNewGenerationMs":351.1463,"presentationToStrictCompletionMs":0}          |
| 18  | 751.16 / 1087.06  | 619.65 / 856.64   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0201,"dispatchToBlockedOrPreparationMs":417.3373,"firstNewGenerationToCleanupDrainedMs":43.4414,"firstPhysicalMutationToFirstNewGenerationMs":346.8383,"presentationToStrictCompletionMs":0}          |
| 19  | 727.32 / 981.55   | 556.32 / 805.27   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3015,"dispatchToBlockedOrPreparationMs":410.8789,"firstNewGenerationToCleanupDrainedMs":0.192,"firstPhysicalMutationToFirstNewGenerationMs":342.8972,"presentationToStrictCompletionMs":0}            |
| 20  | 901.84 / 847.75   | 1078.03 / 1037.86 | 176.19 / 190.10  | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} | {"blockedOrPreparationToFirstPhysicalMutationMs":233.7132,"dispatchToBlockedOrPreparationMs":449.8744,"firstNewGenerationToCleanupDrainedMs":244.8492,"firstPhysicalMutationToFirstNewGenerationMs":109.4186,"presentationToStrictCompletionMs":190.1038} |
| 21  | 202.83 / 201.67   | 501.45 / 473.69   | 298.62 / 272.01  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":127.2856,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.0126}             |
| 22  | 169.90 / 202.86   | 169.90 / 202.86   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":202.8551,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 250.71 / 300.15   | 250.71 / 300.15   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":300.146,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 955.47 / 662.75   | 1133.45 / 812.88  | 177.98 / 150.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} | {"blockedOrPreparationToFirstPhysicalMutationMs":46.9643,"dispatchToBlockedOrPreparationMs":425.2182,"firstNewGenerationToCleanupDrainedMs":192.5732,"firstPhysicalMutationToFirstNewGenerationMs":148.128,"presentationToStrictCompletionMs":150.1354}   |
| 25  | 1323.68 / 1072.85 | 1457.90 / 1164.55 | 134.22 / 91.70   | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} | {"blockedOrPreparationToFirstPhysicalMutationMs":32.6165,"dispatchToBlockedOrPreparationMs":477.7464,"firstNewGenerationToCleanupDrainedMs":178.5893,"firstPhysicalMutationToFirstNewGenerationMs":475.6018,"presentationToStrictCompletionMs":187.2885}  |
| 26  | 763.25 / 1063.64  | 901.36 / 1158.98  | 138.12 / 95.33   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   | {"blockedOrPreparationToFirstPhysicalMutationMs":48.818,"dispatchToBlockedOrPreparationMs":413.2857,"firstNewGenerationToCleanupDrainedMs":196.1877,"firstPhysicalMutationToFirstNewGenerationMs":500.6837,"presentationToStrictCompletionMs":143.8467}   |
| 27  | 559.83 / 605.96   | 819.34 / 794.44   | 259.51 / 188.48  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.7808,"dispatchToBlockedOrPreparationMs":427.7245,"firstNewGenerationToCleanupDrainedMs":240.4988,"firstPhysicalMutationToFirstNewGenerationMs":125.4306,"presentationToStrictCompletionMs":188.4753}   |
| 28  | 849.14 / 883.79   | 938.20 / 737.07   | 89.06 / 0.00     | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5143,"dispatchToBlockedOrPreparationMs":393.2081,"firstNewGenerationToCleanupDrainedMs":45.5102,"firstPhysicalMutationToFirstNewGenerationMs":251.8417,"presentationToStrictCompletionMs":0}          |
| 29  | 1762.50 / 996.73  | 1850.17 / 1134.19 | 87.67 / 137.46   | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} | {"blockedOrPreparationToFirstPhysicalMutationMs":35.3388,"dispatchToBlockedOrPreparationMs":465.2285,"firstNewGenerationToCleanupDrainedMs":180.5548,"firstPhysicalMutationToFirstNewGenerationMs":453.0657,"presentationToStrictCompletionMs":218.4772}  |
| 30  | 464.69 / 586.02   | 696.61 / 779.04   | 231.92 / 193.02  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    | {"blockedOrPreparationToFirstPhysicalMutationMs":46.7888,"dispatchToBlockedOrPreparationMs":407.8188,"firstNewGenerationToCleanupDrainedMs":250.6875,"firstPhysicalMutationToFirstNewGenerationMs":73.7414,"presentationToStrictCompletionMs":193.0212}   |
| 31  | 660.50 / 621.55   | 660.50 / 621.55   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4248,"dispatchToBlockedOrPreparationMs":428.7578,"firstNewGenerationToCleanupDrainedMs":43.0913,"firstPhysicalMutationToFirstNewGenerationMs":98.2738,"presentationToStrictCompletionMs":0}           |
| 32  | 190.74 / 215.19   | 467.90 / 536.61   | 277.16 / 321.42  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":133.0564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":321.4227}             |
| 33  | 253.03 / 296.06   | 253.03 / 296.06   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.0622,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 523.90 / 501.08      | -22.82   | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 4   | 14 / 14                  | 0            | 772.57 / 764.69      | -7.88    | 19 / 19           | 0            |
| 5   | 14 / 15                  | 1            | 761.83 / 804.05      | 42.22    | 19 / 20           | 1            |
| 6   | 18 / 18                  | 0            | 915.18 / 941.87      | 26.68    | 23 / 23           | 0            |
| 7   | 17 / 17                  | 0            | 917.60 / 862.20      | -55.39   | 22 / 22           | 0            |
| 8   | 17 / 17                  | 0            | 896.10 / 861.54      | -34.57   | 22 / 22           | 0            |
| 9   | 16 / 16                  | 0            | 848.56 / 865.34      | 16.78    | 21 / 21           | 0            |
| 10  | 9 / 10                   | 1            | 558.58 / 613.46      | 54.89    | 14 / 14           | 0            |
| 11  | 3 / 3                    | 0            | 162.58 / 168.82      | 6.24     | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 10                   | 1            | 594.11 / 561.50      | -32.62   | 10 / 11           | 1            |
| 14  | 23 / 17                  | -6           | 1120.36 / 873.40     | -246.96  | 28 / 22           | -6           |
| 15  | 12 / 17                  | 5            | 558.73 / 840.43      | 281.70   | 19 / 20           | 1            |
| 16  | 12 / 17                  | 5            | 568.09 / 816.20      | 248.11   | 16 / 25           | 9            |
| 17  | 12 / 17                  | 5            | 552.66 / 802.39      | 249.73   | 17 / 21           | 4            |
| 18  | 12 / 17                  | 5            | 576.08 / 813.20      | 237.11   | 16 / 23           | 7            |
| 19  | 12 / 17                  | 5            | 555.96 / 805.08      | 249.12   | 16 / 21           | 5            |
| 20  | 16 / 16                  | 0            | 853.30 / 793.01      | -60.30   | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 15 / 11                  | -4           | 912.05 / 620.31      | -291.74  | 20 / 15           | -5           |
| 25  | 23 / 18                  | -5           | 1279.76 / 985.97     | -293.79  | 28 / 23           | -5           |
| 26  | 12 / 18                  | 6            | 675.18 / 962.79      | 287.61   | 17 / 23           | 6            |
| 27  | 10 / 10                  | 0            | 558.58 / 553.94      | -4.64    | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 762.69 / 691.56      | -71.13   | 16 / 15           | -1           |
| 29  | 29 / 18                  | -11          | 1677.34 / 953.63     | -723.70  | 34 / 23           | -11          |
| 30  | 9 / 10                   | 1            | 464.19 / 528.35      | 64.16    | 15 / 16           | 1            |
| 31  | 9 / 10                   | 1            | 617.71 / 578.46      | -39.25   | 10 / 11           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 9            | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C   | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ---------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 237.46 / 209.82  | -27.65   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 232.35 / 208.59  | -23.75   |
| 6   | 1 / 1                | 0     | 6 / 5              | -1    | 400.83 / 357.84  | -42.99   |
| 7   | 1 / 1                | 0     | 6 / 5              | -1    | 401.92 / 339.43  | -62.49   |
| 8   | 1 / 1                | 0     | 6 / 5              | -1    | 401.94 / 325.26  | -76.69   |
| 9   | 1 / 1                | 0     | 6 / 5              | -1    | 390.03 / 328.11  | -61.92   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 5              | -1    | 307.78 / 215.47  | -92.31   |
| 15  | 1 / 1                | 0     | 3 / 7              | 4     | 143.90 / 371.79  | 227.89   |
| 16  | 1 / 1                | 0     | 3 / 7              | 4     | 141.18 / 360.69  | 219.51   |
| 17  | 1 / 1                | 0     | 3 / 7              | 4     | 144.86 / 351.10  | 206.24   |
| 18  | 1 / 1                | 0     | 3 / 7              | 4     | 144.35 / 346.98  | 202.63   |
| 19  | 1 / 1                | 0     | 3 / 7              | 4     | 143.96 / 343.25  | 199.29   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 334.87 / 296.10  | -38.77   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 25  | 1 / 1                | 0     | 6 / 5              | -1    | 395.52 / 342.10  | -53.43   |
| 26  | 1 / 2                | 1     | 2 / 6              | 4     | 93.46 / 374.76   | 281.30   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 29  | 3 / 2                | -1    | 16 / 6             | -10   | 1041.25 / 439.19 | -602.06  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |

### Cumulative gates and other health evidence

#### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":18078,"leftPath":"NativeOriginal","referenceFrame":18480,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":22718,"leftPath":"NativeOriginal","referenceFrame":23111,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### nv-pr73-269bded1-mtwz6pez / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":28860,"leftPath":"NativeOriginal","referenceFrame":29241,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### nv-pr73-269bded1-mtwz6pez / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":33440,"leftPath":"NativeOriginal","referenceFrame":33821,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16521.3203125 / 16407.21484375 / -114.10546875 | 16922.84765625 / 16564.875 / -357.97265625     | -243.867              |
| nvidia | 1    | systemCommitMiB   | 54414.625 / 54313.40234375 / -101.22265625     | 56564.03515625 / 56345.5859375 / -218.44921875 | -117.227              |
| nvidia | 1    | dxgiUsageMiB      | 4201.6171875 / 3527.0078125 / -674.609375      | 4479.46875 / 3604.31640625 / -875.15234375     | -200.543              |
| nvidia | 1    | liveTextures      | 0 / 256 / 256                                  | 0 / 213 / 213                                  | -43                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2424.0259971618652 / 2424.0259971618652    | 0 / 2271.0563163757324 / 2271.0563163757324    | -152.970              |
| nvidia | 2    | processPrivateMiB | 16831.234375 / 16617.78515625 / -213.44921875  | 16923.23046875 / 16653.37109375 / -269.859375  | -56.410               |
| nvidia | 2    | systemCommitMiB   | 54507.67578125 / 54547.8203125 / 40.14453125   | 56674.76953125 / 56488.5390625 / -186.23046875 | -226.375              |
| nvidia | 2    | dxgiUsageMiB      | 3889.8828125 / 3566.79296875 / -323.08984375   | 3819.49609375 / 3605.03515625 / -214.4609375   | 108.629               |
| nvidia | 2    | liveTextures      | 0 / 233 / 233                                  | 0 / 225 / 225                                  | -8                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0 / 2288.473041534424 / 2288.473041534424      | -60.959               |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2222        | 2204        | -18        |
| cpu/compactPresentationContract/reuses                  | 2200        | 2182        | -18        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 151         | 153         | 2          |
| cpu/generationResourceValidation/fullValidations        | 568         | 588         | 20         |
| cpu/generationResourceValidation/stableChecks           | 8397        | 8357        | -40        |
| cpu/generationResourceValidation/stableHits             | 8316        | 8342        | 26         |
| cpu/generationResourceValidation/stableMisses           | 81          | 15          | -66        |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4328        | 4210        | -118       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4328        | 4210        | -118       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4286        | 4168        | -118       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4328        | 4210        | -118       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4307        | 4189        | -118       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 30          | -4         |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4294        | 4180        | -114       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4238        | 4093        | -145       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 117         | 27         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4313        | 4195        | -118       |
| cpu/strongStereoPacket/captures                         | 4628        | 4553        | -75        |
| cpu/strongStereoPacket/commitAccepts                    | 4424        | 4391        | -33        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 63          | -3         |
| cpu/strongStereoPacket/commitValidations                | 4490        | 4454        | -36        |
| cpu/strongStereoPacket/cycleReuses                      | 2275        | 2238        | -37        |
| cpu/strongStereoPacket/fastSkips                        | 4028        | 3867        | -161       |
| cpu/strongStereoPacket/invalidations                    | 4882        | 4756        | -126       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 93          | -7         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2253        | 2222        | -31        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.855       | 1.629       | -0.226     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.100      | 19.600      | -48.500    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.147       | 0.140       | -0.007     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 11.900      | 2           | -9.900     |
| cpu/window/currentFrame                                 | 18480       | 29241       | 10761      |
| cpu/window/elapsedFrames                                | 4328        | 4210        | -118       |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 14152       | 25031       | 10879      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 18482       | 29243       | 10761      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6230998224  | 6072400224  | -158598000 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4911        | 4786        | -125       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12474725760 | 12157205760 | -317520000 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.307       | 0.001      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12047940400 | 11833391040 | -214549360 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27299138000 | 26726237760 | -572900240 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15490       | 15180       | -310       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 2045        | 2000        | -45        |
| gpu/item7EarlyHAM/executedClears                        | 2052        | 2000        | -52        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2052        | 2000        | -52        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4256        | 4182        | -74        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4330        | 4212        | -118       |
| gpu/startFrame                                          | 14152       | 25031       | 10879      |
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
| texture/createdCount                                    | 3974        | 3902        | -72        |
| texture/createdEstimatedBytes                           | 38786366328 | 38571404792 | -214961536 |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3718        | 3689        | -29        |
| texture/destroyedEstimatedBytes                         | 36244590844 | 36190029644 | -54561200  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 896         | 866         | -30        |
| texture/liveTextureRecordCount                          | 256         | 213         | -43        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 47          | 4           | -43        |
| texture/niSourceTextureMatchedEstimatedBytes            | 166123904   | 5723568     | -160400336 |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1473        | 1504        | 31         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 256         | 213         | -43        |
| texture/outstandingEstimatedBytes                       | 2541775484  | 2381375148  | -160400336 |
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
| cpu/compactPresentationContract/publishes               | 2189        | 2197        | 8         |
| cpu/compactPresentationContract/reuses                  | 2167        | 2175        | 8         |
| cpu/devBenchOnly                                        | true        | true        | n/a       |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0         |
| cpu/generationResourceValidation/contractPublishes      | 156         | 154         | -2        |
| cpu/generationResourceValidation/fullValidations        | 575         | 590         | 15        |
| cpu/generationResourceValidation/stableChecks           | 8303        | 8334        | 31        |
| cpu/generationResourceValidation/stableHits             | 8226        | 8321        | 95        |
| cpu/generationResourceValidation/stableMisses           | 77          | 13          | -64       |
| cpu/schemaVersion                                       | 1           | 1           | 0         |
| cpu/sessionId                                           | 2           | 2           | 0         |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0         |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4221        | 4193        | -28       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0         |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4221        | 4193        | -28       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4179        | 4151        | -28       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0         |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4221        | 4193        | -28       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0         |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4200        | 4172        | -28       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0         |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 30          | -4        |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4187        | 4163        | -24       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4130        | 4075        | -55       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 117         | 27        |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0         |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4206        | 4178        | -28       |
| cpu/strongStereoPacket/captures                         | 4548        | 4538        | -10       |
| cpu/strongStereoPacket/commitAccepts                    | 4359        | 4377        | 18        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 63          | -2        |
| cpu/strongStereoPacket/commitValidations                | 4424        | 4440        | 16        |
| cpu/strongStereoPacket/cycleReuses                      | 2233        | 2231        | -2        |
| cpu/strongStereoPacket/fastSkips                        | 3894        | 3848        | -46       |
| cpu/strongStereoPacket/invalidations                    | 4767        | 4739        | -28       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 92          | -11       |
| cpu/strongStereoPacket/lifetimeReuses                   | 2212        | 2215        | 3         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.879       | 1.617       | -0.261    |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 25.400      | 16.900      | -8.500    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.143       | 0.142       | -0.001    |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 1.300       | -0.300    |
| cpu/window/currentFrame                                 | 23112       | 33821       | 10709     |
| cpu/window/elapsedFrames                                | 4220        | 4192        | -28       |
| cpu/window/initialized                                  | true        | true        | n/a       |
| cpu/window/startFrame                                   | 18892       | 29629       | 10737     |
| gpu/active                                              | false       | false       | n/a       |
| gpu/currentFrame                                        | 23113       | 33823       | 10710     |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0         |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6071131440  | 6029261568  | -41869872 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4785        | 4752        | -33       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12154665600 | 12070840320 | -83825280 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0         |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.309       | 0.308       | -0.001    |
| gpu/item5ActiveFSRCopies/activePixels                   | 11865705520 | 11871721360 | 6015840   |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26516112080 | 26687907440 | 171795360 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15110       | 15180       | 70        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0         |
| gpu/item7EarlyHAM/directOutputSkips                     | 1991        | 2000        | 9         |
| gpu/item7EarlyHAM/executedClears                        | 2016        | 1986        | -30       |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2016        | 1986        | -30       |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4168        | 4172        | 4         |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0         |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0         |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0         |
| gpu/observedFrames                                      | 4221        | 4194        | -27       |
| gpu/startFrame                                          | 18892       | 29629       | 10737     |
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
| texture/createdCount                                    | 3968        | 3958        | -10       |
| texture/createdEstimatedBytes                           | 38826844112 | 38732018168 | -94825944 |
| texture/currentCohort                                   | 0           | 0           | 0         |
| texture/destroyedCount                                  | 3735        | 3733        | -2        |
| texture/destroyedEstimatedBytes                         | 36363286460 | 36332380260 | -30906200 |
| texture/droppedTextureRecords                           | 0           | 0           | 0         |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0         |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0         |
| texture/groupCount                                      | 887         | 892         | 5         |
| texture/liveTextureRecordCount                          | 233         | 225         | -8        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0         |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0         |
| texture/niSourceTextureMatchedCount                     | 26          | 18          | -8        |
| texture/niSourceTextureMatchedEstimatedBytes            | 87906272    | 23986528    | -63919744 |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0         |
| texture/niSourceTextureResourceCount                    | 1506        | 1515        | 9         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a       |
| texture/outstandingCount                                | 233         | 225         | -8        |
| texture/outstandingEstimatedBytes                       | 2463557652  | 2399637908  | -63919744 |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0         |
| texture/recordingFailures                               | 0           | 0           | 0         |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0         |
| texture/sessionID                                       | 2           | 2           | 0         |
| texture/supported                                       | true        | true        | n/a       |

</details>

### Context, memory, CPU/GPU and evidence

| Retained context matches | Result |
| ------------------------ | ------ |
| adapter                  | true   |
| scene                    | false  |
| foveation                | true   |
| toolchain                | false  |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.

## Complete comparison with measured prior PR73

## Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                              |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z                                                              | nv-pr73-269bded1-mtwz6pez                                                              |
| Renderer base           | c73bae9a776e67614bba84ee0cecf3d076de259f                                                                        | 269bded159c66f4d8b1a45a836078dfa6cdf3241                                               |
| Main-VR base/equivalent | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                                        | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                               |
| Compiled source         | c73bae9a776e67614bba84ee0cecf3d076de259f                                                                        | 269bded159c66f4d8b1a45a836078dfa6cdf3241                                               |
| Build ID                | d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f                                                | 9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b                       |
| DLL SHA-256             | 9a777935e69bbd3c4d7b6400c7291727d6bec15c65708495fbed6719d0b19581                                                | 086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a                       |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nv-pr73-269bded1-mtwz6pez |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

### Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C   | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | ------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 779.95/822.55 | 5.462        | 9/15        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 800.46/808.63 | 1.022        | 10/15       | 0/0          | 0/0                 | none             | MET/MET             |

### Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline | Candidate | Delta   | Delta % |
| ------ | ---- | -------------------------- | ----------- | -------- | --------- | ------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 724.72   | 774.29    | 49.57   | 6.839   |
| nvidia | 1    | Relatch proof mean         | frames      | 13.360   | 14.120    | 0.760   | 5.689   |
| nvidia | 1    | Relatch proof total        | ms          | 18118.06 | 19357.22  | 1239.16 | 6.839   |
| nvidia | 1    | Relatch proof total        | frames      | 334      | 353       | 19      | 5.689   |
| nvidia | 1    | Relatch proof samples      | transitions | 25       | 25        | 0       | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 779.95   | 822.55    | 42.60   | 5.462   |
| nvidia | 1    | Strict completion mean     | frames      | 15.030   | 15.455    | 0.424   | 2.823   |
| nvidia | 1    | Strict completion total    | ms          | 25738.25 | 27144.05  | 1405.80 | 5.462   |
| nvidia | 1    | Strict completion total    | frames      | 496      | 510       | 14      | 2.823   |
| nvidia | 1    | Stretch completed episodes | episodes    | 17       | 19        | 2       | 11.765  |
| nvidia | 1    | Stretch completed total    | frames      | 79       | 88        | 9       | 11.392  |
| nvidia | 1    | Stretch completed total    | ms          | 4675.40  | 5331.10   | 655.70  | 14.025  |
| nvidia | 1    | Stretch longest episode    | ms          | 620.31   | 373.10    | -247.21 | -39.852 |
| nvidia | 2    | Relatch proof mean         | ms          | 734.77   | 742.53    | 7.76    | 1.055   |
| nvidia | 2    | Relatch proof mean         | frames      | 13.880   | 14.120    | 0.240   | 1.729   |
| nvidia | 2    | Relatch proof total        | ms          | 18369.35 | 18563.23  | 193.89  | 1.055   |
| nvidia | 2    | Relatch proof total        | frames      | 347      | 353       | 6       | 1.729   |
| nvidia | 2    | Relatch proof samples      | transitions | 25       | 25        | 0       | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 800.46   | 808.63    | 8.18    | 1.022   |
| nvidia | 2    | Strict completion mean     | frames      | 15.545   | 15.636    | 0.091   | 0.585   |
| nvidia | 2    | Strict completion total    | ms          | 26415.05 | 26684.94  | 269.89  | 1.022   |
| nvidia | 2    | Strict completion total    | frames      | 513      | 516       | 3       | 0.585   |
| nvidia | 2    | Stretch completed episodes | episodes    | 17       | 18        | 1       | 5.882   |
| nvidia | 2    | Stretch completed total    | frames      | 93       | 86        | -7      | -7.527  |
| nvidia | 2    | Stretch completed total    | ms          | 5600.77  | 5210.46   | -390.31 | -6.969  |
| nvidia | 2    | Stretch longest episode    | ms          | 648.80   | 371.79    | -277.01 | -42.696 |

### nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 656.59 / 744.02   | 87.44    | 13.317  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 153.42 / 168.17   | 14.75    | 9.615   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 279.70 / 249.96   | -29.74   | -10.632 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 865.77 / 1009.43  | 143.66   | 16.594  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1120.62 / 1209.11 | 88.49    | 7.897   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1091.19 / 1171.55 | 80.36    | 7.364   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1271.04 / 1319.77 | 48.73    | 3.834   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1301.08 / 1356.82 | 55.74    | 4.284   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1209.84 / 1288.90 | 79.06    | 6.534   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 734.51 / 775.05   | 40.54    | 5.520   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 457.95 / 479.92   | 21.97    | 4.798   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 173.23 / 172.78   | -0.45    | -0.258  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 815.51 / 809.24   | -6.27    | -0.769  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1279.58 / 1098.56 | -181.02  | -14.147 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 727.24 / 926.43   | 199.18   | 27.389  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 923.93 / 983.36   | 59.42    | 6.432   | 0/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 689.07 / 1050.89  | 361.82   | 52.509  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 735.19 / 1079.17  | 343.97   | 46.787  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 1220.77 / 975.33  | -245.44  | -20.106 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 713.69 / 1028.87  | 315.17   | 44.161  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 481.17 / 481.52   | 0.35     | 0.073   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 167.50 / 170.78   | 3.29     | 1.961   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 246.58 / 301.22   | 54.64    | 22.157  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 871.39 / 808.22   | -63.17   | -7.250  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1469.69 / 1222.45 | -247.25  | -16.823 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 954.92 / 1150.18  | 195.26   | 20.448  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 702.90 / 761.69   | 58.78    | 8.363   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 881.77 / 920.99   | 39.21    | 4.447   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1468.51 / 1233.79 | -234.72  | -15.984 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 702.75 / 780.67   | 77.93    | 11.089  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 685.02 / 636.91   | -48.11   | -7.023  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 442.08 / 516.36   | 74.28    | 16.803  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 244.05 / 261.97   | 17.92    | 7.341   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                        |
| --- | ----------------- | ----------------- | ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 447.82 / 552.73   | 656.59 / 744.02   | 208.77 / 191.29  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6563,"dispatchToBlockedOrPreparationMs":335.0722,"firstNewGenerationToCleanupDrainedMs":209.0736,"firstPhysicalMutationToFirstNewGenerationMs":109.7841,"presentationToStrictCompletionMs":208.7666}  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.2207,"dispatchToBlockedOrPreparationMs":362.6825,"firstNewGenerationToCleanupDrainedMs":242.4389,"firstPhysicalMutationToFirstNewGenerationMs":92.6807,"presentationToStrictCompletionMs":191.2904}  |
| 2   | 153.42 / 168.17   | 153.42 / 168.17   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":153.4173,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":168.1689,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 3   | 279.70 / 249.96   | 279.70 / 249.96   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":279.7022,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.964,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 667.77 / 839.82   | 786.30 / 924.90   | 118.54 / 85.08   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0033,"dispatchToBlockedOrPreparationMs":320.4408,"firstNewGenerationToCleanupDrainedMs":158.6505,"firstPhysicalMutationToFirstNewGenerationMs":304.2086,"presentationToStrictCompletionMs":198.0014}  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.1203,"dispatchToBlockedOrPreparationMs":361.1041,"firstNewGenerationToCleanupDrainedMs":172.6253,"firstPhysicalMutationToFirstNewGenerationMs":345.0476,"presentationToStrictCompletionMs":169.6118} |
| 5   | 955.62 / 995.16   | 1038.21 / 1124.31 | 82.60 / 129.15   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8194,"dispatchToBlockedOrPreparationMs":329.09,"firstNewGenerationToCleanupDrainedMs":164.8594,"firstPhysicalMutationToFirstNewGenerationMs":541.4455,"presentationToStrictCompletionMs":165.0008}    | {"blockedOrPreparationToFirstPhysicalMutationMs":55.0599,"dispatchToBlockedOrPreparationMs":342.7084,"firstNewGenerationToCleanupDrainedMs":172.2143,"firstPhysicalMutationToFirstNewGenerationMs":554.3277,"presentationToStrictCompletionMs":213.9487} |
| 6   | 875.19 / 955.28   | 1008.00 / 1087.78 | 132.81 / 132.50  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2817,"dispatchToBlockedOrPreparationMs":347.7403,"firstNewGenerationToCleanupDrainedMs":174.1092,"firstPhysicalMutationToFirstNewGenerationMs":482.8658,"presentationToStrictCompletionMs":216.0018}  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.1022,"dispatchToBlockedOrPreparationMs":382.1632,"firstNewGenerationToCleanupDrainedMs":175.3795,"firstPhysicalMutationToFirstNewGenerationMs":483.1375,"presentationToStrictCompletionMs":216.2703} |
| 7   | 1060.67 / 1102.23 | 1186.78 / 1234.77 | 126.12 / 132.53  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1464,"dispatchToBlockedOrPreparationMs":341.1366,"firstNewGenerationToCleanupDrainedMs":168.5915,"firstPhysicalMutationToFirstNewGenerationMs":673.9075,"presentationToStrictCompletionMs":210.3715}  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.4041,"dispatchToBlockedOrPreparationMs":349.0306,"firstNewGenerationToCleanupDrainedMs":176.0055,"firstPhysicalMutationToFirstNewGenerationMs":662.3272,"presentationToStrictCompletionMs":217.5327} |
| 8   | 1095.13 / 1139.85 | 1220.96 / 1271.88 | 125.83 / 132.03  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2782,"dispatchToBlockedOrPreparationMs":350.8879,"firstNewGenerationToCleanupDrainedMs":166.2255,"firstPhysicalMutationToFirstNewGenerationMs":700.5718,"presentationToStrictCompletionMs":205.9462}  | {"blockedOrPreparationToFirstPhysicalMutationMs":49.8214,"dispatchToBlockedOrPreparationMs":344.7531,"firstNewGenerationToCleanupDrainedMs":175.8538,"firstPhysicalMutationToFirstNewGenerationMs":701.4561,"presentationToStrictCompletionMs":216.9671} |
| 9   | 1006.71 / 1074.13 | 1130.32 / 1209.27 | 123.62 / 135.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2557,"dispatchToBlockedOrPreparationMs":335.1845,"firstNewGenerationToCleanupDrainedMs":162.6084,"firstPhysicalMutationToFirstNewGenerationMs":629.2742,"presentationToStrictCompletionMs":203.1347}  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.0548,"dispatchToBlockedOrPreparationMs":349.1578,"firstNewGenerationToCleanupDrainedMs":178.2302,"firstPhysicalMutationToFirstNewGenerationMs":634.8221,"presentationToStrictCompletionMs":214.7668} |
| 10  | 558.21 / 642.71   | 734.51 / 775.05   | 176.29 / 132.34  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5139,"dispatchToBlockedOrPreparationMs":333.0484,"firstNewGenerationToCleanupDrainedMs":221.0865,"firstPhysicalMutationToFirstNewGenerationMs":176.8596,"presentationToStrictCompletionMs":176.2943}  | {"blockedOrPreparationToFirstPhysicalMutationMs":0.5962,"dispatchToBlockedOrPreparationMs":395.5892,"firstNewGenerationToCleanupDrainedMs":176.6887,"firstPhysicalMutationToFirstNewGenerationMs":202.1765,"presentationToStrictCompletionMs":132.3433}  |
| 11  | 168.00 / 172.00   | 457.95 / 479.92   | 289.95 / 307.93  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.388,"dispatchToBlockedOrPreparationMs":126.0806,"firstNewGenerationToCleanupDrainedMs":290.7759,"firstPhysicalMutationToFirstNewGenerationMs":37.7057,"presentationToStrictCompletionMs":289.9458}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5808,"dispatchToBlockedOrPreparationMs":128.4443,"firstNewGenerationToCleanupDrainedMs":309.1395,"firstPhysicalMutationToFirstNewGenerationMs":38.7575,"presentationToStrictCompletionMs":307.925}    |
| 12  | 173.23 / 172.78   | 173.23 / 172.78   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":173.229,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7824,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 13  | 815.51 / 809.24   | 815.51 / 809.24   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":210.5794,"dispatchToBlockedOrPreparationMs":443.6748,"firstNewGenerationToCleanupDrainedMs":39.6922,"firstPhysicalMutationToFirstNewGenerationMs":121.5584,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":197.745,"dispatchToBlockedOrPreparationMs":438.364,"firstNewGenerationToCleanupDrainedMs":43.1553,"firstPhysicalMutationToFirstNewGenerationMs":129.9709,"presentationToStrictCompletionMs":0}          |
| 14  | 1109.70 / 920.47  | 1233.62 / 1051.77 | 123.92 / 131.30  | {"blockedOrPreparationToFirstPhysicalMutationMs":256.5249,"dispatchToBlockedOrPreparationMs":387.052,"firstNewGenerationToCleanupDrainedMs":166.5031,"firstPhysicalMutationToFirstNewGenerationMs":423.5352,"presentationToStrictCompletionMs":169.8841} | {"blockedOrPreparationToFirstPhysicalMutationMs":53.0763,"dispatchToBlockedOrPreparationMs":398.3695,"firstNewGenerationToCleanupDrainedMs":174.5609,"firstPhysicalMutationToFirstNewGenerationMs":425.7673,"presentationToStrictCompletionMs":178.0895} |
| 15  | 727.24 / 926.43   | 591.69 / 836.45   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0031,"dispatchToBlockedOrPreparationMs":352.8401,"firstNewGenerationToCleanupDrainedMs":39.6052,"firstPhysicalMutationToFirstNewGenerationMs":195.2417,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":46.2245,"dispatchToBlockedOrPreparationMs":404.3924,"firstNewGenerationToCleanupDrainedMs":42.9698,"firstPhysicalMutationToFirstNewGenerationMs":342.8622,"presentationToStrictCompletionMs":0}         |
| 16  | 923.93 / 983.36   | 600.73 / 882.80   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0463,"dispatchToBlockedOrPreparationMs":335.5841,"firstNewGenerationToCleanupDrainedMs":41.8456,"firstPhysicalMutationToFirstNewGenerationMs":219.2555,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":48.5628,"dispatchToBlockedOrPreparationMs":415.6594,"firstNewGenerationToCleanupDrainedMs":45.9667,"firstPhysicalMutationToFirstNewGenerationMs":372.6063,"presentationToStrictCompletionMs":0}         |
| 17  | 689.07 / 1050.89  | 603.65 / 825.86   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5866,"dispatchToBlockedOrPreparationMs":368.0976,"firstNewGenerationToCleanupDrainedMs":42.1154,"firstPhysicalMutationToFirstNewGenerationMs":188.8525,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":53.6722,"dispatchToBlockedOrPreparationMs":409.8709,"firstNewGenerationToCleanupDrainedMs":0.1596,"firstPhysicalMutationToFirstNewGenerationMs":362.1534,"presentationToStrictCompletionMs":0}          |
| 18  | 735.19 / 1079.17  | 607.98 / 838.29   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":3.943,"dispatchToBlockedOrPreparationMs":363.053,"firstNewGenerationToCleanupDrainedMs":39.7992,"firstPhysicalMutationToFirstNewGenerationMs":201.1864,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":47.7944,"dispatchToBlockedOrPreparationMs":400.8995,"firstNewGenerationToCleanupDrainedMs":43.7485,"firstPhysicalMutationToFirstNewGenerationMs":345.8437,"presentationToStrictCompletionMs":0}         |
| 19  | 1220.77 / 975.33  | 1055.20 / 841.37  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":262.079,"dispatchToBlockedOrPreparationMs":395.007,"firstNewGenerationToCleanupDrainedMs":40.3851,"firstPhysicalMutationToFirstNewGenerationMs":357.732,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":47.6092,"dispatchToBlockedOrPreparationMs":413.012,"firstNewGenerationToCleanupDrainedMs":42.5309,"firstPhysicalMutationToFirstNewGenerationMs":338.2159,"presentationToStrictCompletionMs":0}          |
| 20  | 475.20 / 849.47   | 713.69 / 1028.87  | 238.50 / 179.39  | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0452,"dispatchToBlockedOrPreparationMs":340.1828,"firstNewGenerationToCleanupDrainedMs":239.0053,"firstPhysicalMutationToFirstNewGenerationMs":128.4599,"presentationToStrictCompletionMs":238.4978}  | {"blockedOrPreparationToFirstPhysicalMutationMs":254.0958,"dispatchToBlockedOrPreparationMs":442.0118,"firstNewGenerationToCleanupDrainedMs":242.3936,"firstPhysicalMutationToFirstNewGenerationMs":90.3663,"presentationToStrictCompletionMs":179.3937} |
| 21  | 206.07 / 214.61   | 481.17 / 481.52   | 275.10 / 266.91  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":137.6313,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":275.1007}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.5939,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":266.9111}            |
| 22  | 167.50 / 170.78   | 167.50 / 170.78   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.4957,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7807,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 23  | 246.58 / 301.22   | 246.58 / 301.22   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":246.5831,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":301.2181,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |
| 24  | 700.03 / 669.04   | 871.39 / 808.22   | 171.36 / 139.17  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5963,"dispatchToBlockedOrPreparationMs":500.3463,"firstNewGenerationToCleanupDrainedMs":216.5493,"firstPhysicalMutationToFirstNewGenerationMs":150.8989,"presentationToStrictCompletionMs":171.3567}  | {"blockedOrPreparationToFirstPhysicalMutationMs":54.8492,"dispatchToBlockedOrPreparationMs":415.4329,"firstNewGenerationToCleanupDrainedMs":183.5175,"firstPhysicalMutationToFirstNewGenerationMs":154.4173,"presentationToStrictCompletionMs":139.1725} |
| 25  | 1260.39 / 1002.37 | 1386.75 / 1138.75 | 126.36 / 136.38  | {"blockedOrPreparationToFirstPhysicalMutationMs":324.2224,"dispatchToBlockedOrPreparationMs":405.8027,"firstNewGenerationToCleanupDrainedMs":166.9843,"firstPhysicalMutationToFirstNewGenerationMs":489.745,"presentationToStrictCompletionMs":209.3018} | {"blockedOrPreparationToFirstPhysicalMutationMs":32.0675,"dispatchToBlockedOrPreparationMs":445.7745,"firstNewGenerationToCleanupDrainedMs":181.7968,"firstPhysicalMutationToFirstNewGenerationMs":479.1118,"presentationToStrictCompletionMs":220.0764} |
| 26  | 819.04 / 995.24   | 910.02 / 1099.66  | 90.98 / 104.42   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3084,"dispatchToBlockedOrPreparationMs":374.1965,"firstNewGenerationToCleanupDrainedMs":182.4897,"firstPhysicalMutationToFirstNewGenerationMs":349.0272,"presentationToStrictCompletionMs":135.8773}  | {"blockedOrPreparationToFirstPhysicalMutationMs":46.9174,"dispatchToBlockedOrPreparationMs":399.4277,"firstNewGenerationToCleanupDrainedMs":198.5605,"firstPhysicalMutationToFirstNewGenerationMs":454.749,"presentationToStrictCompletionMs":154.9414}  |
| 27  | 533.53 / 579.55   | 702.90 / 761.69   | 169.37 / 182.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4752,"dispatchToBlockedOrPreparationMs":346.9035,"firstNewGenerationToCleanupDrainedMs":236.7528,"firstPhysicalMutationToFirstNewGenerationMs":115.7706,"presentationToStrictCompletionMs":169.372}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.6327,"dispatchToBlockedOrPreparationMs":411.3025,"firstNewGenerationToCleanupDrainedMs":237.287,"firstPhysicalMutationToFirstNewGenerationMs":112.4631,"presentationToStrictCompletionMs":182.1399}   |
| 28  | 755.19 / 920.99   | 837.11 / 782.78   | 81.92 / 0.00     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3804,"dispatchToBlockedOrPreparationMs":382.759,"firstNewGenerationToCleanupDrainedMs":165.5979,"firstPhysicalMutationToFirstNewGenerationMs":285.3699,"presentationToStrictCompletionMs":126.5807}   | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0429,"dispatchToBlockedOrPreparationMs":403.8252,"firstNewGenerationToCleanupDrainedMs":44.9347,"firstPhysicalMutationToFirstNewGenerationMs":284.9767,"presentationToStrictCompletionMs":0}         |
| 29  | 1233.19 / 1009.07 | 1375.13 / 1150.31 | 141.94 / 141.23  | {"blockedOrPreparationToFirstPhysicalMutationMs":22.6826,"dispatchToBlockedOrPreparationMs":668.5188,"firstNewGenerationToCleanupDrainedMs":185.5666,"firstPhysicalMutationToFirstNewGenerationMs":498.362,"presentationToStrictCompletionMs":235.3212}  | {"blockedOrPreparationToFirstPhysicalMutationMs":32.4908,"dispatchToBlockedOrPreparationMs":460.9697,"firstNewGenerationToCleanupDrainedMs":196.496,"firstPhysicalMutationToFirstNewGenerationMs":460.3491,"presentationToStrictCompletionMs":224.717}   |
| 30  | 469.74 / 582.21   | 702.75 / 780.67   | 233.01 / 198.46  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1336,"dispatchToBlockedOrPreparationMs":353.8621,"firstNewGenerationToCleanupDrainedMs":233.7121,"firstPhysicalMutationToFirstNewGenerationMs":112.0376,"presentationToStrictCompletionMs":233.0102}  | {"blockedOrPreparationToFirstPhysicalMutationMs":47.0268,"dispatchToBlockedOrPreparationMs":393.313,"firstNewGenerationToCleanupDrainedMs":266.9159,"firstPhysicalMutationToFirstNewGenerationMs":73.4185,"presentationToStrictCompletionMs":198.4598}   |
| 31  | 685.02 / 636.91   | 685.02 / 636.91   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":47.443,"dispatchToBlockedOrPreparationMs":406.2882,"firstNewGenerationToCleanupDrainedMs":42.135,"firstPhysicalMutationToFirstNewGenerationMs":189.157,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":48.0217,"dispatchToBlockedOrPreparationMs":434.0515,"firstNewGenerationToCleanupDrainedMs":44.7224,"firstPhysicalMutationToFirstNewGenerationMs":110.1155,"presentationToStrictCompletionMs":0}         |
| 32  | 181.13 / 229.92   | 442.08 / 516.36   | 260.95 / 286.44  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":121.4218,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":260.9483}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":150.4586,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.4359}            |
| 33  | 244.05 / 261.97   | 244.05 / 261.97   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":244.0526,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.9674,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 447.51 / 501.58      | 54.07    | 14 / 15           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 4   | 12 / 13                  | 1            | 627.65 / 752.27      | 124.62   | 17 / 18           | 1            |
| 5   | 13 / 14                  | 1            | 873.36 / 952.10      | 78.74    | 18 / 19           | 1            |
| 6   | 17 / 18                  | 1            | 833.89 / 912.40      | 78.52    | 22 / 23           | 1            |
| 7   | 17 / 16                  | -1           | 1018.19 / 1058.76    | 40.57    | 22 / 21           | -1           |
| 8   | 17 / 17                  | 0            | 1054.74 / 1096.03    | 41.29    | 22 / 22           | 0            |
| 9   | 17 / 16                  | -1           | 967.71 / 1031.04     | 63.32    | 22 / 21           | -1           |
| 10  | 9 / 10                   | 1            | 513.42 / 598.36      | 84.94    | 14 / 15           | 1            |
| 11  | 3 / 3                    | 0            | 167.17 / 170.78      | 3.61     | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 10                   | 1            | 775.81 / 766.08      | -9.73    | 10 / 11           | 1            |
| 14  | 22 / 18                  | -4           | 1067.11 / 877.21     | -189.90  | 27 / 23           | -4           |
| 15  | 12 / 17                  | 5            | 552.09 / 793.48      | 241.39   | 16 / 20           | 4            |
| 16  | 12 / 17                  | 5            | 558.89 / 836.83      | 277.94   | 20 / 20           | 0            |
| 17  | 12 / 17                  | 5            | 561.54 / 825.70      | 264.16   | 15 / 22           | 7            |
| 18  | 12 / 17                  | 5            | 568.18 / 794.54      | 226.36   | 16 / 23           | 7            |
| 19  | 22 / 17                  | -5           | 1014.82 / 798.84     | -215.98  | 27 / 21           | -6           |
| 20  | 10 / 16                  | 6            | 474.69 / 786.47      | 311.79   | 15 / 21           | 6            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 10 / 11                  | 1            | 654.84 / 624.70      | -30.14   | 15 / 15           | 0            |
| 25  | 23 / 18                  | -5           | 1219.77 / 956.95     | -262.82  | 28 / 23           | -5           |
| 26  | 13 / 18                  | 5            | 727.53 / 901.09      | 173.56   | 18 / 23           | 5            |
| 27  | 10 / 10                  | 0            | 466.15 / 524.40      | 58.25    | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 671.51 / 737.85      | 66.34    | 16 / 15           | -1           |
| 29  | 23 / 18                  | -5           | 1189.56 / 953.81     | -235.75  | 28 / 23           | -5           |
| 30  | 9 / 11                   | 2            | 469.03 / 513.76      | 44.73    | 14 / 17           | 3            |
| 31  | 10 / 10                  | 0            | 642.89 / 592.19      | -50.70   | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C  | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | --------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 2              | 1     | 112.70 / 139.19 | 26.49    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 197.99 / 201.34 | 3.35     |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 206.39 / 199.52 | -6.88    |
| 6   | 1 / 1                | 0     | 6 / 5              | -1    | 380.29 / 339.45 | -40.84   |
| 7   | 1 / 1                | 0     | 6 / 5              | -1    | 375.72 / 342.12 | -33.60   |
| 8   | 1 / 1                | 0     | 6 / 5              | -1    | 386.74 / 364.07 | -22.68   |
| 9   | 1 / 1                | 0     | 6 / 5              | -1    | 373.36 / 354.19 | -19.17   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 5              | -1    | 276.05 / 221.16 | -54.89   |
| 15  | 1 / 1                | 0     | 3 / 7              | 4     | 139.40 / 343.05 | 203.65   |
| 16  | 1 / 1                | 0     | 3 / 7              | 4     | 156.80 / 373.10 | 216.30   |
| 17  | 1 / 1                | 0     | 3 / 7              | 4     | 139.60 / 362.82 | 223.21   |
| 18  | 1 / 1                | 0     | 3 / 7              | 4     | 141.29 / 345.79 | 204.50   |
| 19  | 1 / 1                | 0     | 13 / 7             | -6    | 620.31 / 338.28 | -282.03  |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0.00 / 278.88   | 278.88   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 25  | 1 / 1                | 0     | 6 / 5              | -1    | 366.61 / 347.45 | -19.16   |
| 26  | 1 / 2                | 1     | 2 / 6              | 4     | 114.71 / 336.56 | 221.85   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 29  | 2 / 2                | 0     | 11 / 6             | -5    | 687.44 / 444.16 | -243.28  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |

### nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 697.75 / 741.00   | 43.26    | 6.199   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 177.48 / 182.27   | 4.79     | 2.699   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 257.49 / 315.20   | 57.70    | 22.410  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 990.08 / 1027.93  | 37.84    | 3.822   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 987.03 / 1069.76  | 82.73    | 8.382   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1125.32 / 1207.43 | 82.11    | 7.296   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1148.39 / 1117.00 | -31.39   | -2.734  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.79 / 1135.64 | -60.16   | -5.031  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1128.61 / 1129.17 | 0.56     | 0.049   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 760.69 / 789.15   | 28.46    | 3.741   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 425.13 / 447.12   | 21.99    | 5.172   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 165.31 / 174.67   | 9.36     | 5.663   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 582.54 / 604.44   | 21.90    | 3.760   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1370.68 / 1097.57 | -273.11  | -19.925 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1256.79 / 977.10  | -279.69  | -22.254 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 795.10 / 1185.51  | 390.41   | 49.103  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 844.20 / 993.64   | 149.44   | 17.702  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1493.80 / 1087.06 | -406.74  | -27.229 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 771.31 / 981.55   | 210.24   | 27.257  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 763.53 / 1037.86  | 274.32   | 35.928  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 519.99 / 473.69   | -46.31   | -8.905  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 182.08 / 202.86   | 20.78    | 11.413  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 258.24 / 300.15   | 41.90    | 16.226  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 830.24 / 812.88   | -17.36   | -2.091  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 957.41 / 1260.14  | 302.73   | 31.620  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1468.30 / 1207.49 | -260.81  | -17.763 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 781.30 / 794.44   | 13.13    | 1.681   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 905.80 / 883.79   | -22.00   | -2.429  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1520.56 / 1215.21 | -305.35  | -20.081 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 730.67 / 779.04   | 48.36    | 6.619   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 594.05 / 621.55   | 27.50    | 4.629   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 479.11 / 536.61   | 57.50    | 12.002  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 250.27 / 296.06   | 45.79    | 18.296  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 472.52 / 554.96   | 697.75 / 741.00   | 225.23 / 186.05  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0479,"dispatchToBlockedOrPreparationMs":350.0684,"firstNewGenerationToCleanupDrainedMs":225.9804,"firstPhysicalMutationToFirstNewGenerationMs":118.6504,"presentationToStrictCompletionMs":225.225}    | {"blockedOrPreparationToFirstPhysicalMutationMs":49.8744,"dispatchToBlockedOrPreparationMs":364.4052,"firstNewGenerationToCleanupDrainedMs":239.9261,"firstPhysicalMutationToFirstNewGenerationMs":86.7972,"presentationToStrictCompletionMs":186.0469}   |
| 2   | 177.48 / 182.27   | 177.48 / 182.27   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.4832,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.2739,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 257.49 / 315.20   | 257.49 / 315.20   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":257.4931,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":315.1964,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 758.80 / 852.97   | 904.95 / 941.80   | 146.14 / 88.82   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9947,"dispatchToBlockedOrPreparationMs":360.3242,"firstNewGenerationToCleanupDrainedMs":193.0035,"firstPhysicalMutationToFirstNewGenerationMs":348.6226,"presentationToStrictCompletionMs":231.2829}   | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3988,"dispatchToBlockedOrPreparationMs":372.6052,"firstNewGenerationToCleanupDrainedMs":177.1048,"firstPhysicalMutationToFirstNewGenerationMs":344.6858,"presentationToStrictCompletionMs":174.9528}  |
| 5   | 762.04 / 848.64   | 898.02 / 982.76   | 135.98 / 134.12  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.76,"dispatchToBlockedOrPreparationMs":358.469,"firstNewGenerationToCleanupDrainedMs":181.2246,"firstPhysicalMutationToFirstNewGenerationMs":354.5673,"presentationToStrictCompletionMs":224.9864}      | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0827,"dispatchToBlockedOrPreparationMs":399.5427,"firstNewGenerationToCleanupDrainedMs":178.7136,"firstPhysicalMutationToFirstNewGenerationMs":345.4225,"presentationToStrictCompletionMs":221.1181}  |
| 6   | 908.79 / 985.97   | 1041.35 / 1122.60 | 132.56 / 136.62  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7777,"dispatchToBlockedOrPreparationMs":371.3996,"firstNewGenerationToCleanupDrainedMs":174.592,"firstPhysicalMutationToFirstNewGenerationMs":491.5763,"presentationToStrictCompletionMs":216.5307}    | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2644,"dispatchToBlockedOrPreparationMs":400.457,"firstNewGenerationToCleanupDrainedMs":180.7301,"firstPhysicalMutationToFirstNewGenerationMs":493.1433,"presentationToStrictCompletionMs":221.4575}   |
| 7   | 924.31 / 949.18   | 1065.55 / 1036.10 | 141.23 / 86.92   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6605,"dispatchToBlockedOrPreparationMs":392.5265,"firstNewGenerationToCleanupDrainedMs":182.9429,"firstPhysicalMutationToFirstNewGenerationMs":486.4149,"presentationToStrictCompletionMs":224.0807}   | {"blockedOrPreparationToFirstPhysicalMutationMs":48.8175,"dispatchToBlockedOrPreparationMs":344.1407,"firstNewGenerationToCleanupDrainedMs":173.8925,"firstPhysicalMutationToFirstNewGenerationMs":469.2451,"presentationToStrictCompletionMs":167.8205}  |
| 8   | 963.67 / 956.27   | 1100.54 / 1050.64 | 136.88 / 94.37   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0819,"dispatchToBlockedOrPreparationMs":399.8201,"firstNewGenerationToCleanupDrainedMs":182.6845,"firstPhysicalMutationToFirstNewGenerationMs":514.9579,"presentationToStrictCompletionMs":232.1241}   | {"blockedOrPreparationToFirstPhysicalMutationMs":48.4996,"dispatchToBlockedOrPreparationMs":350.9687,"firstNewGenerationToCleanupDrainedMs":189.0992,"firstPhysicalMutationToFirstNewGenerationMs":462.0671,"presentationToStrictCompletionMs":179.3663}  |
| 9   | 921.52 / 953.45   | 1048.49 / 1042.01 | 126.96 / 88.57   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7925,"dispatchToBlockedOrPreparationMs":406.9526,"firstNewGenerationToCleanupDrainedMs":168.8756,"firstPhysicalMutationToFirstNewGenerationMs":467.8669,"presentationToStrictCompletionMs":207.0885}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.6635,"dispatchToBlockedOrPreparationMs":354.4957,"firstNewGenerationToCleanupDrainedMs":176.6761,"firstPhysicalMutationToFirstNewGenerationMs":464.1759,"presentationToStrictCompletionMs":175.7231}  |
| 10  | 591.58 / 657.21   | 760.69 / 789.15   | 169.11 / 131.94  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7673,"dispatchToBlockedOrPreparationMs":353.5765,"firstNewGenerationToCleanupDrainedMs":217.3099,"firstPhysicalMutationToFirstNewGenerationMs":185.0336,"presentationToStrictCompletionMs":169.1063}   | {"blockedOrPreparationToFirstPhysicalMutationMs":45.5912,"dispatchToBlockedOrPreparationMs":354.5912,"firstNewGenerationToCleanupDrainedMs":175.6807,"firstPhysicalMutationToFirstNewGenerationMs":213.2819,"presentationToStrictCompletionMs":131.9389}  |
| 11  | 165.34 / 169.79   | 425.13 / 447.12   | 259.79 / 277.32  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0572,"dispatchToBlockedOrPreparationMs":123.3176,"firstNewGenerationToCleanupDrainedMs":260.6446,"firstPhysicalMutationToFirstNewGenerationMs":37.1097,"presentationToStrictCompletionMs":259.7866}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0317,"dispatchToBlockedOrPreparationMs":126.896,"firstNewGenerationToCleanupDrainedMs":278.2949,"firstPhysicalMutationToFirstNewGenerationMs":37.8926,"presentationToStrictCompletionMs":277.3232}     |
| 12  | 165.31 / 174.67   | 165.31 / 174.67   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":165.307,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.6685,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 582.54 / 604.44   | 582.54 / 604.44   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":50.8977,"dispatchToBlockedOrPreparationMs":390.5072,"firstNewGenerationToCleanupDrainedMs":45.281,"firstPhysicalMutationToFirstNewGenerationMs":95.8559,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":48.2125,"dispatchToBlockedOrPreparationMs":415.7236,"firstNewGenerationToCleanupDrainedMs":42.9494,"firstPhysicalMutationToFirstNewGenerationMs":97.5588,"presentationToStrictCompletionMs":0}           |
| 14  | 1370.68 / 916.48  | 1324.61 / 1050.96 | 0.00 / 134.48    | {"blockedOrPreparationToFirstPhysicalMutationMs":305.751,"dispatchToBlockedOrPreparationMs":424.6356,"firstNewGenerationToCleanupDrainedMs":165.0926,"firstPhysicalMutationToFirstNewGenerationMs":429.132,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":47.3492,"dispatchToBlockedOrPreparationMs":410.0617,"firstNewGenerationToCleanupDrainedMs":177.5564,"firstPhysicalMutationToFirstNewGenerationMs":415.9909,"presentationToStrictCompletionMs":181.0888}  |
| 15  | 1256.79 / 977.10  | 1083.70 / 883.84  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":281.994,"dispatchToBlockedOrPreparationMs":394.7142,"firstNewGenerationToCleanupDrainedMs":41.5369,"firstPhysicalMutationToFirstNewGenerationMs":365.4539,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":57.7999,"dispatchToBlockedOrPreparationMs":411.0873,"firstNewGenerationToCleanupDrainedMs":43.4123,"firstPhysicalMutationToFirstNewGenerationMs":371.5409,"presentationToStrictCompletionMs":0}          |
| 16  | 795.10 / 1185.51  | 604.08 / 862.33   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4423,"dispatchToBlockedOrPreparationMs":344.7541,"firstNewGenerationToCleanupDrainedMs":44.7263,"firstPhysicalMutationToFirstNewGenerationMs":211.1566,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":48.4907,"dispatchToBlockedOrPreparationMs":407.2276,"firstNewGenerationToCleanupDrainedMs":46.1316,"firstPhysicalMutationToFirstNewGenerationMs":360.4811,"presentationToStrictCompletionMs":0}          |
| 17  | 844.20 / 993.64   | 702.35 / 846.74   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.6416,"dispatchToBlockedOrPreparationMs":419.1894,"firstNewGenerationToCleanupDrainedMs":47.4633,"firstPhysicalMutationToFirstNewGenerationMs":230.0514,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0873,"dispatchToBlockedOrPreparationMs":402.1547,"firstNewGenerationToCleanupDrainedMs":44.3475,"firstPhysicalMutationToFirstNewGenerationMs":351.1463,"presentationToStrictCompletionMs":0}          |
| 18  | 1493.80 / 1087.06 | 1126.68 / 856.64  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":275.2212,"dispatchToBlockedOrPreparationMs":429.5467,"firstNewGenerationToCleanupDrainedMs":48.8497,"firstPhysicalMutationToFirstNewGenerationMs":373.0666,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0201,"dispatchToBlockedOrPreparationMs":417.3373,"firstNewGenerationToCleanupDrainedMs":43.4414,"firstPhysicalMutationToFirstNewGenerationMs":346.8383,"presentationToStrictCompletionMs":0}          |
| 19  | 771.31 / 981.55   | 640.71 / 805.27   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5632,"dispatchToBlockedOrPreparationMs":388.3442,"firstNewGenerationToCleanupDrainedMs":44.5207,"firstPhysicalMutationToFirstNewGenerationMs":203.2858,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":51.3015,"dispatchToBlockedOrPreparationMs":410.8789,"firstNewGenerationToCleanupDrainedMs":0.192,"firstPhysicalMutationToFirstNewGenerationMs":342.8972,"presentationToStrictCompletionMs":0}            |
| 20  | 560.51 / 847.75   | 763.53 / 1037.86  | 203.03 / 190.10  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8874,"dispatchToBlockedOrPreparationMs":354.7334,"firstNewGenerationToCleanupDrainedMs":254.5382,"firstPhysicalMutationToFirstNewGenerationMs":149.3737,"presentationToStrictCompletionMs":203.0274}   | {"blockedOrPreparationToFirstPhysicalMutationMs":233.7132,"dispatchToBlockedOrPreparationMs":449.8744,"firstNewGenerationToCleanupDrainedMs":244.8492,"firstPhysicalMutationToFirstNewGenerationMs":109.4186,"presentationToStrictCompletionMs":190.1038} |
| 21  | 222.93 / 201.67   | 519.99 / 473.69   | 297.07 / 272.01  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":143.4389,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":297.0673}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":127.2856,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.0126}             |
| 22  | 182.08 / 202.86   | 182.08 / 202.86   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":182.0753,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":202.8551,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 258.24 / 300.15   | 258.24 / 300.15   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.243,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":300.146,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 656.73 / 662.75   | 830.24 / 812.88   | 173.51 / 150.14  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9576,"dispatchToBlockedOrPreparationMs":464.0565,"firstNewGenerationToCleanupDrainedMs":215.4267,"firstPhysicalMutationToFirstNewGenerationMs":147.8015,"presentationToStrictCompletionMs":173.5111}   | {"blockedOrPreparationToFirstPhysicalMutationMs":46.9643,"dispatchToBlockedOrPreparationMs":425.2182,"firstNewGenerationToCleanupDrainedMs":192.5732,"firstPhysicalMutationToFirstNewGenerationMs":148.128,"presentationToStrictCompletionMs":150.1354}   |
| 25  | 742.12 / 1072.85  | 873.56 / 1164.55  | 131.44 / 91.70   | {"blockedOrPreparationToFirstPhysicalMutationMs":26.7315,"dispatchToBlockedOrPreparationMs":349.6094,"firstNewGenerationToCleanupDrainedMs":173.2963,"firstPhysicalMutationToFirstNewGenerationMs":323.9192,"presentationToStrictCompletionMs":215.2918}  | {"blockedOrPreparationToFirstPhysicalMutationMs":32.6165,"dispatchToBlockedOrPreparationMs":477.7464,"firstNewGenerationToCleanupDrainedMs":178.5893,"firstPhysicalMutationToFirstNewGenerationMs":475.6018,"presentationToStrictCompletionMs":187.2885}  |
| 26  | 1333.26 / 1063.64 | 1420.79 / 1158.98 | 87.52 / 95.33    | {"blockedOrPreparationToFirstPhysicalMutationMs":284.7751,"dispatchToBlockedOrPreparationMs":436.6673,"firstNewGenerationToCleanupDrainedMs":178.4377,"firstPhysicalMutationToFirstNewGenerationMs":520.9055,"presentationToStrictCompletionMs":135.0364} | {"blockedOrPreparationToFirstPhysicalMutationMs":48.818,"dispatchToBlockedOrPreparationMs":413.2857,"firstNewGenerationToCleanupDrainedMs":196.1877,"firstPhysicalMutationToFirstNewGenerationMs":500.6837,"presentationToStrictCompletionMs":143.8467}   |
| 27  | 550.55 / 605.96   | 781.30 / 794.44   | 230.76 / 188.48  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4852,"dispatchToBlockedOrPreparationMs":401.9437,"firstNewGenerationToCleanupDrainedMs":231.4649,"firstPhysicalMutationToFirstNewGenerationMs":144.4103,"presentationToStrictCompletionMs":230.7545}   | {"blockedOrPreparationToFirstPhysicalMutationMs":0.7808,"dispatchToBlockedOrPreparationMs":427.7245,"firstNewGenerationToCleanupDrainedMs":240.4988,"firstPhysicalMutationToFirstNewGenerationMs":125.4306,"presentationToStrictCompletionMs":188.4753}   |
| 28  | 816.04 / 883.79   | 859.56 / 737.07   | 43.52 / 0.00     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3751,"dispatchToBlockedOrPreparationMs":397.3447,"firstNewGenerationToCleanupDrainedMs":171.9637,"firstPhysicalMutationToFirstNewGenerationMs":286.8724,"presentationToStrictCompletionMs":89.7583}    | {"blockedOrPreparationToFirstPhysicalMutationMs":46.5143,"dispatchToBlockedOrPreparationMs":393.2081,"firstNewGenerationToCleanupDrainedMs":45.5102,"firstPhysicalMutationToFirstNewGenerationMs":251.8417,"presentationToStrictCompletionMs":0}          |
| 29  | 1282.28 / 996.73  | 1438.32 / 1134.19 | 156.04 / 137.46  | {"blockedOrPreparationToFirstPhysicalMutationMs":23.5635,"dispatchToBlockedOrPreparationMs":703.5446,"firstNewGenerationToCleanupDrainedMs":202.6941,"firstPhysicalMutationToFirstNewGenerationMs":508.5164,"presentationToStrictCompletionMs":238.2744}  | {"blockedOrPreparationToFirstPhysicalMutationMs":35.3388,"dispatchToBlockedOrPreparationMs":465.2285,"firstNewGenerationToCleanupDrainedMs":180.5548,"firstPhysicalMutationToFirstNewGenerationMs":453.0657,"presentationToStrictCompletionMs":218.4772}  |
| 30  | 503.87 / 586.02   | 730.67 / 779.04   | 226.81 / 193.02  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2535,"dispatchToBlockedOrPreparationMs":391.7451,"firstNewGenerationToCleanupDrainedMs":227.109,"firstPhysicalMutationToFirstNewGenerationMs":108.566,"presentationToStrictCompletionMs":226.8046}     | {"blockedOrPreparationToFirstPhysicalMutationMs":46.7888,"dispatchToBlockedOrPreparationMs":407.8188,"firstNewGenerationToCleanupDrainedMs":250.6875,"firstPhysicalMutationToFirstNewGenerationMs":73.7414,"presentationToStrictCompletionMs":193.0212}   |
| 31  | 594.05 / 621.55   | 594.05 / 621.55   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":49.0265,"dispatchToBlockedOrPreparationMs":404.5485,"firstNewGenerationToCleanupDrainedMs":50.1418,"firstPhysicalMutationToFirstNewGenerationMs":90.333,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4248,"dispatchToBlockedOrPreparationMs":428.7578,"firstNewGenerationToCleanupDrainedMs":43.0913,"firstPhysicalMutationToFirstNewGenerationMs":98.2738,"presentationToStrictCompletionMs":0}           |
| 32  | 192.35 / 215.19   | 479.11 / 536.61   | 286.76 / 321.42  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.1379,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":286.7583}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":133.0564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":321.4227}             |
| 33  | 250.27 / 296.06   | 250.27 / 296.06   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2714,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.0622,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 471.77 / 501.08      | 29.31    | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 4   | 12 / 14                  | 2            | 711.94 / 764.69      | 52.75    | 17 / 19           | 2            |
| 5   | 13 / 15                  | 2            | 716.80 / 804.05      | 87.25    | 18 / 20           | 2            |
| 6   | 17 / 18                  | 1            | 866.75 / 941.87      | 75.11    | 22 / 23           | 1            |
| 7   | 17 / 17                  | 0            | 882.60 / 862.20      | -20.40   | 22 / 22           | 0            |
| 8   | 18 / 17                  | -1           | 917.86 / 861.54      | -56.33   | 23 / 22           | -1           |
| 9   | 18 / 16                  | -2           | 879.61 / 865.34      | -14.28   | 23 / 21           | -2           |
| 10  | 10 / 10                  | 0            | 543.38 / 613.46      | 70.09    | 16 / 14           | -2           |
| 11  | 3 / 3                    | 0            | 164.48 / 168.82      | 4.34     | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 10                   | 1            | 537.26 / 561.50      | 24.23    | 10 / 11           | 1            |
| 14  | 22 / 17                  | -5           | 1159.52 / 873.40     | -286.12  | 27 / 22           | -5           |
| 15  | 22 / 17                  | -5           | 1042.16 / 840.43     | -201.73  | 27 / 20           | -7           |
| 16  | 12 / 17                  | 5            | 559.35 / 816.20      | 256.85   | 17 / 25           | 8            |
| 17  | 12 / 17                  | 5            | 654.88 / 802.39      | 147.51   | 16 / 21           | 5            |
| 18  | 22 / 17                  | -5           | 1077.83 / 813.20     | -264.64  | 31 / 23           | -8           |
| 19  | 12 / 17                  | 5            | 596.19 / 805.08      | 208.88   | 16 / 21           | 5            |
| 20  | 9 / 16                   | 7            | 509.00 / 793.01      | 284.01   | 14 / 21           | 7            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 10 / 11                  | 1            | 614.82 / 620.31      | 5.50     | 15 / 15           | 0            |
| 25  | 12 / 18                  | 6            | 700.26 / 985.97      | 285.71   | 17 / 23           | 6            |
| 26  | 23 / 18                  | -5           | 1242.35 / 962.79     | -279.56  | 28 / 23           | -5           |
| 27  | 11 / 10                  | -1           | 549.84 / 553.94      | 4.10     | 17 / 16           | -1           |
| 28  | 11 / 11                  | 0            | 687.59 / 691.56      | 3.97     | 16 / 15           | -1           |
| 29  | 23 / 18                  | -5           | 1235.62 / 953.63     | -281.99  | 28 / 23           | -5           |
| 30  | 10 / 10                  | 0            | 503.57 / 528.35      | 24.78    | 16 / 16           | 0            |
| 31  | 9 / 10                   | 1            | 543.91 / 578.46      | 34.55    | 10 / 11           | 1            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 9            | -2           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C  | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | --------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 228.56 / 209.82 | -18.74   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 242.75 / 208.59 | -34.16   |
| 6   | 1 / 1                | 0     | 6 / 5              | -1    | 385.64 / 357.84 | -27.80   |
| 7   | 1 / 1                | 0     | 6 / 5              | -1    | 376.35 / 339.43 | -36.92   |
| 8   | 1 / 1                | 0     | 6 / 5              | -1    | 404.97 / 325.26 | -79.71   |
| 9   | 1 / 1                | 0     | 6 / 5              | -1    | 363.20 / 328.11 | -35.09   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 5              | -1    | 275.83 / 215.47 | -60.36   |
| 15  | 1 / 1                | 0     | 13 / 7             | -6    | 647.94 / 371.79 | -276.16  |
| 16  | 1 / 1                | 0     | 3 / 7              | 4     | 157.09 / 360.69 | 203.59   |
| 17  | 1 / 1                | 0     | 3 / 7              | 4     | 171.02 / 351.10 | 180.08   |
| 18  | 1 / 1                | 0     | 13 / 7             | -6    | 648.80 / 346.98 | -301.83  |
| 19  | 1 / 1                | 0     | 3 / 7              | 4     | 149.21 / 343.25 | 194.04   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0.00 / 296.10   | 296.10   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 25  | 1 / 1                | 0     | 2 / 5              | 3     | 211.05 / 342.10 | 131.05   |
| 26  | 2 / 2                | 0     | 11 / 6             | -5    | 638.03 / 374.76 | -263.27  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 29  | 2 / 2                | 0     | 11 / 6             | -5    | 700.34 / 439.19 | -261.15  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00     | 0.00     |

### Cumulative gates and other health evidence

#### renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z / nvidia / pass 1

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

#### renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z / nvidia / pass 2

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

#### nv-pr73-269bded1-mtwz6pez / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":28860,"leftPath":"NativeOriginal","referenceFrame":29241,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

#### nv-pr73-269bded1-mtwz6pez / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":7,"maximumObservedFrames":7}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":33440,"leftPath":"NativeOriginal","referenceFrame":33821,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                            | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | --------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16733.71484375 / 16496.359375 / -237.35546875 | 16922.84765625 / 16564.875 / -357.97265625     | -120.617              |
| nvidia | 1    | systemCommitMiB   | 55395.53125 / 55048.33984375 / -347.19140625  | 56564.03515625 / 56345.5859375 / -218.44921875 | 128.742               |
| nvidia | 1    | dxgiUsageMiB      | 4285.1640625 / 3534.4765625 / -750.6875       | 4479.46875 / 3604.31640625 / -875.15234375     | -124.465              |
| nvidia | 1    | liveTextures      | 0 / 263 / 263                                 | 0 / 213 / 213                                  | -50                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2402.765727996826 / 2402.765727996826     | 0 / 2271.0563163757324 / 2271.0563163757324    | -131.709              |
| nvidia | 2    | processPrivateMiB | 16822.87109375 / 16603.328125 / -219.54296875 | 16923.23046875 / 16653.37109375 / -269.859375  | -50.316               |
| nvidia | 2    | systemCommitMiB   | 55520.37890625 / 55103.39453125 / -416.984375 | 56674.76953125 / 56488.5390625 / -186.23046875 | 230.754               |
| nvidia | 2    | dxgiUsageMiB      | 3822.21484375 / 3647.90234375 / -174.3125     | 3819.49609375 / 3605.03515625 / -214.4609375   | -40.148               |
| nvidia | 2    | liveTextures      | 0 / 236 / 236                                 | 0 / 225 / 225                                  | -11                   |
| nvidia | 2    | liveTextureMiB    | 0 / 2358.7650413513184 / 2358.7650413513184   | 0 / 2288.473041534424 / 2288.473041534424      | -70.292               |

### Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2294        | 2204        | -90         |
| cpu/compactPresentationContract/reuses                  | 2272        | 2182        | -90         |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 71          | 70          | -1          |
| cpu/generationResourceValidation/contractPublishes      | 145         | 153         | 8           |
| cpu/generationResourceValidation/fullValidations        | 567         | 588         | 21          |
| cpu/generationResourceValidation/stableChecks           | 8647        | 8357        | -290        |
| cpu/generationResourceValidation/stableHits             | 8568        | 8342        | -226        |
| cpu/generationResourceValidation/stableMisses           | 79          | 15          | -64         |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 1           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4457        | 4210        | -247        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4457        | 4210        | -247        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4415        | 4168        | -247        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4457        | 4210        | -247        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4436        | 4189        | -247        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 35          | 30          | -5          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4422        | 4180        | -242        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4362        | 4093        | -269        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 117         | 23          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4442        | 4195        | -247        |
| cpu/strongStereoPacket/captures                         | 4749        | 4553        | -196        |
| cpu/strongStereoPacket/commitAccepts                    | 4570        | 4391        | -179        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 63          | -1          |
| cpu/strongStereoPacket/commitValidations                | 4634        | 4454        | -180        |
| cpu/strongStereoPacket/cycleReuses                      | 2335        | 2238        | -97         |
| cpu/strongStereoPacket/fastSkips                        | 4165        | 3867        | -298        |
| cpu/strongStereoPacket/invalidations                    | 5009        | 4756        | -253        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 101         | 93          | -8          |
| cpu/strongStereoPacket/lifetimeReuses                   | 2313        | 2222        | -91         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.630       | 1.629       | -0.001      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 26.800      | 19.600      | -7.200      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.108       | 0.140       | 0.032       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.400       | 2           | 0.600       |
| cpu/window/currentFrame                                 | 17085       | 29241       | 12156       |
| cpu/window/elapsedFrames                                | 4456        | 4210        | -246        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 12629       | 25031       | 12402       |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 17086       | 29243       | 12157       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6408627984  | 6072400224  | -336227760  |
| gpu/item10PeripheryTAAHistory/dispatches                | 5051        | 4786        | -265        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12830348160 | 12157205760 | -673142400  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.308       | 0.307       | -0.001      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12453511360 | 11833391040 | -620120320  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 28036639040 | 26726237760 | -1310401280 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15940       | 15180       | -760        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2105        | 2000        | -105        |
| gpu/item7EarlyHAM/executedClears                        | 2116        | 2000        | -116        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2116        | 2000        | -116        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4380        | 4182        | -198        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4457        | 4212        | -245        |
| gpu/startFrame                                          | 12629       | 25031       | 12402       |
| profiler/available                                      | true        | true        | n/a         |
| profiler/capabilities                                   | 63          | 63          | 0           |
| profiler/capturing                                      | false       | false       | n/a         |
| profiler/enabled                                        | true        | true        | n/a         |
| profiler/frame/acquiredSlots                            | 0           | 0           | 0           |
| profiler/frame/captured                                 | 0           | 0           | 0           |
| profiler/frame/peakAcquiredSlots                        | 0           | 0           | 0           |
| profiler/frame/slotRefusals                             | 0           | 0           | 0           |
| profiler/limits/frameLatency                            | 3           | 3           | 0           |
| profiler/limits/historyCapacity                         | 300         | 300         | 0           |
| profiler/limits/maximumTimers                           | 128         | 128         | 0           |
| profiler/timerCount                                     | 0           | 0           | 0           |
| profiler/totalsMs/cpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/gpu                                   | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedCpu                           | n/a         | n/a         | n/a         |
| profiler/totalsMs/resolvedGpu                           | n/a         | n/a         | n/a         |
| texture/active                                          | false       | false       | n/a         |
| texture/attachFailures                                  | 0           | 0           | 0           |
| texture/createdCount                                    | 3962        | 3902        | -60         |
| texture/createdEstimatedBytes                           | 38741091672 | 38571404792 | -169686880  |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3699        | 3689        | -10         |
| texture/destroyedEstimatedBytes                         | 36221609196 | 36190029644 | -31579552   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 893         | 866         | -27         |
| texture/liveTextureRecordCount                          | 263         | 213         | -50         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 54          | 4           | -50         |
| texture/niSourceTextureMatchedEstimatedBytes            | 143830896   | 5723568     | -138107328  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1504        | 1504        | 0           |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 263         | 213         | -50         |
| texture/outstandingEstimatedBytes                       | 2519482476  | 2381375148  | -138107328  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 1           | 1           | 0           |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2249        | 2197        | -52        |
| cpu/compactPresentationContract/reuses                  | 2227        | 2175        | -52        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 151         | 154         | 3          |
| cpu/generationResourceValidation/fullValidations        | 584         | 590         | 6          |
| cpu/generationResourceValidation/stableChecks           | 8533        | 8334        | -199       |
| cpu/generationResourceValidation/stableHits             | 8456        | 8321        | -135       |
| cpu/generationResourceValidation/stableMisses           | 77          | 13          | -64        |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4313        | 4193        | -120       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4313        | 4193        | -120       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4271        | 4151        | -120       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4313        | 4193        | -120       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4292        | 4172        | -120       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 30          | -4         |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4279        | 4163        | -116       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4215        | 4075        | -140       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 98          | 117         | 19         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4298        | 4178        | -120       |
| cpu/strongStereoPacket/captures                         | 4646        | 4538        | -108       |
| cpu/strongStereoPacket/commitAccepts                    | 4479        | 4377        | -102       |
| cpu/strongStereoPacket/commitRejects                    | 65          | 63          | -2         |
| cpu/strongStereoPacket/commitValidations                | 4544        | 4440        | -104       |
| cpu/strongStereoPacket/cycleReuses                      | 2283        | 2231        | -52        |
| cpu/strongStereoPacket/fastSkips                        | 3980        | 3848        | -132       |
| cpu/strongStereoPacket/invalidations                    | 4854        | 4739        | -115       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 102         | 92          | -10        |
| cpu/strongStereoPacket/lifetimeReuses                   | 2261        | 2215        | -46        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.673       | 1.617       | -0.055     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 64.200      | 16.900      | -47.300    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.106       | 0.142       | 0.036      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.200       | 1.300       | 0.100      |
| cpu/window/currentFrame                                 | 21805       | 33821       | 12016      |
| cpu/window/elapsedFrames                                | 4313        | 4192        | -121       |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 17492       | 29629       | 12137      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 21807       | 33823       | 12016      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6208160112  | 6029261568  | -178898544 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4893        | 4752        | -141       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12429002880 | 12070840320 | -358162560 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.308       | 0.001      |
| gpu/item5ActiveFSRCopies/activePixels                   | 12004040680 | 11871721360 | -132319320 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27063620120 | 26687907440 | -375712680 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15380       | 15180       | -200       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 2027        | 2000        | -27        |
| gpu/item7EarlyHAM/executedClears                        | 2072        | 1986        | -86        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2072        | 1986        | -86        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4260        | 4172        | -88        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4315        | 4194        | -121       |
| gpu/startFrame                                          | 17492       | 29629       | 12137      |
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
| texture/createdCount                                    | 3965        | 3958        | -7         |
| texture/createdEstimatedBytes                           | 38833321536 | 38732018168 | -101303368 |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3729        | 3733        | 4          |
| texture/destroyedEstimatedBytes                         | 36359977124 | 36332380260 | -27596864  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 887         | 892         | 5          |
| texture/liveTextureRecordCount                          | 236         | 225         | -11        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 29          | 18          | -11        |
| texture/niSourceTextureMatchedEstimatedBytes            | 97693032    | 23986528    | -73706504  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1509        | 1515        | 6          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 236         | 225         | -11        |
| texture/outstandingEstimatedBytes                       | 2473344412  | 2399637908  | -73706504  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 2           | 2           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

### Context, memory, CPU/GPU and evidence

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
