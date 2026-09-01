# NVIDIA render-scale tuning: PR66 review merge, 10 September 2026

The requested NVIDIA assay completed both 33-transition passes in the
same Skyrim VR process. All 66 terminal render checks passed; per-row
Task 2 counts are 66 PASS, 0 FAIL, and 0 INCONCLUSIVE. Full-history
health contains recovered failures, and the change assessment is
**DOES_NOT_MEET_STANDARD**. Reporting is **INCOMPLETE** because detailed
retry telemetry is not exposed by this compiled build.

## Build and measurement identity

Run: `renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z`; game PID `42052`.
Compiled source: `89e3964582730b92c1885eaef8f865e2d1d02ea9` (clean Release).
Runtime Build ID: `880b0ef46dd92fc519bc53f577a21d2986b438133952822f350f18d11360169e`.

This is the local PR66 review merge of main-VR
`348803c1831c8cd71ceb75a58143ad0bcb00abb2` and PR66 head
`85ee7d4a572544410fa29463d5d704b50984610f`. Its renderer base is the
compiled merge `89e3964582730b92c1885eaef8f865e2d1d02ea9`; no older
renderer or current checkout identity is substituted for that source.

The reference is the previous relevant measured main-VR run
`nvidia-2026-09-10T07-19-50-405Z`, compiled from `348803c18`.
The current checkout and the older `390fdea25` comparison renderer were
not selected as this run's reference.

The physical 27,829,248-byte DLL has SHA-256
`6c7886e8e9420354c30e976edaf3b059227fa262a52092c0c729819852ce218f`.
Its adjacent manifest, retained AIO receipt, and runtime Build ID match.
The AIO archive hash/size also match the receipt. The selected profile
has exactly one enabled loose DLL provider, and neither Overwrite nor
unmanaged Data contains another CommunityShaders DLL.
[Physical deployment verification](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z/raw/offline/physical-aio-verification.json).

Both builds used the RTX 5070 Ti Laptop GPU, Dragonsreach position
(-448, -128, -318), Helios_SkyrimClearTU weather, 1512 x 1680 output
per eye, DLSS preset K, explicit FSR3, and the 0.3/0.3/0.7 foveation
fixture. Toolchain, dependencies and shader compiler identities match.
Game hour differs (reference 1.958, candidate 9.957), so the retained
scene fingerprints are unequal. Exact driver version, headset refresh,
power state and full modlist/cache equivalence are not proven.
No versioned tolerance policy was supplied. Timing differences below
are descriptive and do not establish a causal runtime regression.

## Per-pass results

| Metric                                        |     Pass 1 |     Pass 2 |
| --------------------------------------------- | ---------: | ---------: |
| Strict mean (ms)                              |    865.774 |    835.988 |
| Strict median (ms)                            |    810.162 |    787.806 |
| Strict p95 (ms)                               |   1565.481 |   1505.742 |
| Strict maximum (ms)                           |   1598.030 |   1866.656 |
| Strict total (ms)                             |  28570.534 |  27587.604 |
| Presentation mean (ms)                        |    733.617 |    701.617 |
| Cleanup-tail mean (ms)                        |    106.626 |    107.462 |
| Cleanup-tail maximum (ms)                     |    308.591 |    311.765 |
| Relatch proof mean (ms; 25 measured rows)     |    812.873 |    782.504 |
| Relatch proof mean (frames; 25 measured rows) |     13.880 |     14.000 |
| Strict mean (frames; 33 measured rows)        |     15.515 |     15.455 |
| Terminal PASS / FAIL                          |     33 / 0 |     33 / 0 |
| Task 2 PASS / FAIL / INCONCLUSIVE             | 33 / 0 / 0 | 33 / 0 / 0 |
| Owned retries                                 |         10 |         11 |
| Fidelity mismatch observations                |          4 |          4 |
| Vendor-failure stretch eye observations       |          2 |          2 |
| Device-loss failures                          |          0 |          0 |
| OOM failures                                  |          0 |          0 |
| Producer terminal failures                    |          0 |          0 |
| Vendor-native qualification failures          |          0 |          0 |
| Credible liveness timeouts                    |          0 |          0 |

Strict completion was 12.842% / 14.438% slower than the reference.
Pass 1 had 26 slower and 7 faster routes; pass 2 had 31 slower and 2
faster routes. The largest increases were pass-1 row 25 (+635.994 ms,
FSR3 Native AA to DLSS Hoshipa) and pass-2 row 20 (+400.086 ms,
FSR3 Ultra Performance to FSR3 Native AA). Row 20 was slower in both
passes (+375.766 / +400.086 ms). All paired routes and deltas follow.

Rows 26 (DLSS Hoshipa to FSR3 Hoshipa) and 28 (None to FSR3 Ultra
Performance) each recorded two fidelity mismatches and one vendor-failure
stretch eye observation in each pass, then recovered to terminal PASS.
The same findings exist in the reference: no new or resolved affected
failure routes were observed. Observation counts are not unique crashes
or independent frames. No recovery apply was required.

The applicable cumulative fidelity and presentation-fallback gates
remain unmet in both builds. The two-frame stretch cutoff is retained
as DIAGNOSTIC_ONLY because settling imposes stretch. The scaled
presentation-recovered gate is CONTRACT_MISMATCH after the proven native
terminal target. Neither excluded gate is used as a health penalty.

Allowed stretch totaled 19 / 18 completed episodes, 83 / 82 frames and
5324.710 / 5291.078 ms. Both captures ended without active stretch.
The 33 transitions selecting stretch at first mutation all recovered.
The full per-pass and per-transition baseline/candidate stretch tables
below retain episode counts, frame counts, durations and deltas.

Detailed retry reasons, viewport waits and guard/promotion intervals
are unavailable; the 10 / 11 retry counts come from owned stress metrics.
Missing detailed events are not interpreted as zero retries or zero wait.

## Reporting limitation in the historical comparison

The generated comparison carries the older baseline summary's COMPLETE
reporting label, although all 66 baseline rows lack detailed retry
telemetry and retainedRetry is null. This label does not satisfy the
current retry-detail contract. Treat detailed retry reporting as
incomplete for both builds. The candidate finalizer correctly reports
retry_telemetry_incomplete. Original evidence and generated outputs are
preserved unchanged; this qualification accompanies their tables.

Automation feedback submission was attempted with the bundled controller.
The existing local queue denied access to its write lock, so the defect
is noted here without claiming a recorded AUTO receipt. Expected behavior:
derive both inputs' reporting completeness against the current contract
while retaining the original historical classification separately.

## Same-process repeat by transition group

Each table keeps pass 1 and pass 2 separate. Timing tuples are strict /
presentation / cleanup / cleanup tail in milliseconds, measured from
qualification_dispatch except the explicit cleanup tail. The five-second
pre-dispatch wait is excluded. Vendor-crossing rows also appear in their
destination-method table; these views must not be summed as extra rows.

### NVIDIA DLSS and DLAA destinations

| Row | Route                                            | P1 render / Task 2 | P1 timing tuple (ms)                     | P2 render / Task 2 | P2 timing tuple (ms)                     | Retries P1/P2 | Recovered fidelity/vendor finding |
| --- | ------------------------------------------------ | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ------------- | --------------------------------- |
| 3   | TAA -> DLAA                                      | PASS / PASS        | 405.023 / 405.023 / 405.023 / 0.000      | PASS / PASS        | 254.211 / 254.211 / 254.211 / 0.000      | 0/0           | none observed                     |
| 4   | DLAA -> DLSS Hoshipa                             | PASS / PASS        | 1268.476 / 996.312 / 1167.408 / 171.095  | PASS / PASS        | 961.099 / 831.380 / 874.609 / 43.229     | 0/0           | none observed                     |
| 5   | DLSS Hoshipa -> DLSS Ultra Quality               | PASS / PASS        | 1292.094 / 1063.253 / 1206.841 / 143.587 | PASS / PASS        | 1035.564 / 784.654 / 929.039 / 144.385   | 0/0           | none observed                     |
| 6   | DLSS Ultra Quality -> DLSS Quality               | PASS / PASS        | 1161.243 / 936.847 / 1071.864 / 135.017  | PASS / PASS        | 1312.792 / 1057.241 / 1216.011 / 158.771 | 1/1           | none observed                     |
| 7   | DLSS Quality -> DLSS Balanced                    | PASS / PASS        | 1472.065 / 1253.468 / 1388.281 / 134.813 | PASS / PASS        | 1285.890 / 1046.583 / 1190.100 / 143.517 | 1/1           | none observed                     |
| 8   | DLSS Balanced -> DLSS Performance                | PASS / PASS        | 1598.030 / 1369.837 / 1512.719 / 142.882 | PASS / PASS        | 1179.722 / 949.191 / 1089.830 / 140.640  | 1/1           | none observed                     |
| 9   | DLSS Performance -> DLSS Ultra Performance       | PASS / PASS        | 1279.377 / 1067.192 / 1198.749 / 131.558 | PASS / PASS        | 1216.345 / 989.364 / 1127.928 / 138.565  | 1/1           | none observed                     |
| 10  | DLSS Ultra Performance -> DLAA                   | PASS / PASS        | 810.162 / 639.622 / 810.162 / 170.540    | PASS / PASS        | 812.303 / 630.486 / 812.303 / 181.817    | 0/0           | none observed                     |
| 23  | NONE -> DLAA                                     | PASS / PASS        | 265.124 / 265.124 / 265.124 / 0.000      | PASS / PASS        | 326.185 / 326.185 / 326.185 / 0.000      | 0/0           | none observed                     |
| 25  | FSR3 Native AA -> DLSS Hoshipa                   | PASS / PASS        | 1570.129 / 1349.836 / 1482.977 / 133.140 | PASS / PASS        | 1866.656 / 1646.770 / 1780.080 / 133.311 | 1/2           | none observed                     |
| 29  | FSR3 Ultra Performance -> DLSS Ultra Performance | PASS / PASS        | 1562.382 / 1324.502 / 1469.657 / 145.155 | PASS / PASS        | 1443.906 / 1232.101 / 1358.859 / 126.758 | 1/1           | none observed                     |
| 33  | NONE -> DLAA                                     | PASS / PASS        | 288.747 / 288.747 / 288.747 / 0.000      | PASS / PASS        | 246.959 / 246.959 / 246.959 / 0.000      | 0/0           | none observed                     |

### NVIDIA FSR3 destinations

| Row | Route                                      | P1 render / Task 2 | P1 timing tuple (ms)                     | P2 render / Task 2 | P2 timing tuple (ms)                     | Retries P1/P2 | Recovered fidelity/vendor finding |
| --- | ------------------------------------------ | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ------------- | --------------------------------- |
| 13  | NONE -> FSR3 Native AA                     | PASS / PASS        | 848.372 / 848.372 / 848.372 / 0.000      | PASS / PASS        | 645.721 / 645.721 / 645.721 / 0.000      | 0/0           | none observed                     |
| 14  | FSR3 Native AA -> FSR3 Hoshipa             | PASS / PASS        | 1329.689 / 1156.328 / 1284.480 / 128.152 | PASS / PASS        | 1556.222 / 1405.433 / 1505.063 / 99.630  | 1/1           | none observed                     |
| 15  | FSR3 Hoshipa -> FSR3 Ultra Quality         | PASS / PASS        | 826.292 / 826.292 / 608.558 / 0.000      | PASS / PASS        | 736.960 / 736.960 / 607.865 / 0.000      | 0/0           | none observed                     |
| 16  | FSR3 Ultra Quality -> FSR3 Quality         | PASS / PASS        | 807.254 / 807.254 / 591.877 / 0.000      | PASS / PASS        | 770.705 / 770.705 / 597.030 / 0.000      | 0/0           | none observed                     |
| 17  | FSR3 Quality -> FSR3 Balanced              | PASS / PASS        | 797.497 / 797.497 / 596.797 / 0.000      | PASS / PASS        | 808.174 / 808.174 / 671.634 / 0.000      | 0/0           | none observed                     |
| 18  | FSR3 Balanced -> FSR3 Performance          | PASS / PASS        | 722.354 / 722.354 / 588.984 / 0.000      | PASS / PASS        | 727.223 / 727.223 / 594.022 / 0.000      | 0/0           | none observed                     |
| 19  | FSR3 Performance -> FSR3 Ultra Performance | PASS / PASS        | 693.695 / 693.695 / 603.176 / 0.000      | PASS / PASS        | 737.751 / 737.751 / 605.478 / 0.000      | 0/0           | none observed                     |
| 20  | FSR3 Ultra Performance -> FSR3 Native AA   | PASS / PASS        | 1031.523 / 858.895 / 1031.523 / 172.627  | PASS / PASS        | 1043.912 / 853.402 / 1043.912 / 190.510  | 1/1           | none observed                     |
| 24  | DLAA -> FSR3 Native AA                     | PASS / PASS        | 1134.703 / 954.281 / 1134.703 / 180.422  | PASS / PASS        | 1123.809 / 932.785 / 1123.809 / 191.024  | 1/1           | none observed                     |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa               | PASS / PASS        | 1456.417 / 1319.876 / 1409.535 / 89.659  | PASS / PASS        | 1472.089 / 1268.873 / 1416.550 / 147.677 | 1/1           | both passes; also in reference    |
| 28  | NONE -> FSR3 Ultra Performance             | PASS / PASS        | 904.465 / 773.440 / 859.338 / 85.898     | PASS / PASS        | 889.014 / 712.601 / 843.261 / 130.660    | 0/0           | both passes; also in reference    |
| 31  | TAA -> FSR3 Native AA                      | PASS / PASS        | 666.674 / 666.674 / 666.674 / 0.000      | PASS / PASS        | 880.277 / 880.277 / 880.277 / 0.000      | 0/0           | none observed                     |

### NVIDIA provider crossings

| Row | Route                                            | P1 render / Task 2 | P1 timing tuple (ms)                     | P2 render / Task 2 | P2 timing tuple (ms)                     | Retries P1/P2 | Recovered fidelity/vendor finding |
| --- | ------------------------------------------------ | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ------------- | --------------------------------- |
| 24  | DLAA -> FSR3 Native AA                           | PASS / PASS        | 1134.703 / 954.281 / 1134.703 / 180.422  | PASS / PASS        | 1123.809 / 932.785 / 1123.809 / 191.024  | 1/1           | none observed                     |
| 25  | FSR3 Native AA -> DLSS Hoshipa                   | PASS / PASS        | 1570.129 / 1349.836 / 1482.977 / 133.140 | PASS / PASS        | 1866.656 / 1646.770 / 1780.080 / 133.311 | 1/2           | none observed                     |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa                     | PASS / PASS        | 1456.417 / 1319.876 / 1409.535 / 89.659  | PASS / PASS        | 1472.089 / 1268.873 / 1416.550 / 147.677 | 1/1           | both passes; also in reference    |
| 29  | FSR3 Ultra Performance -> DLSS Ultra Performance | PASS / PASS        | 1562.382 / 1324.502 / 1469.657 / 145.155 | PASS / PASS        | 1443.906 / 1232.101 / 1358.859 / 126.758 | 1/1           | none observed                     |

### NVIDIA TAA and None destinations

| Row | Route                         | P1 render / Task 2 | P1 timing tuple (ms)                  | P2 render / Task 2 | P2 timing tuple (ms)                  | Retries P1/P2 | Recovered fidelity/vendor finding |
| --- | ----------------------------- | ------------------ | ------------------------------------- | ------------------ | ------------------------------------- | ------------- | --------------------------------- |
| 1   | DLSS Hoshipa -> NONE          | PASS / PASS        | 815.555 / 540.542 / 815.555 / 275.013 | PASS / PASS        | 787.806 / 533.107 / 787.806 / 254.699 | 0/0           | none observed                     |
| 2   | NONE -> TAA                   | PASS / PASS        | 242.947 / 242.947 / 242.947 / 0.000   | PASS / PASS        | 178.046 / 178.046 / 178.046 / 0.000   | 0/0           | none observed                     |
| 11  | DLAA -> TAA                   | PASS / PASS        | 445.232 / 170.061 / 445.232 / 275.170 | PASS / PASS        | 482.447 / 188.792 / 482.447 / 293.654 | 0/0           | none observed                     |
| 12  | TAA -> NONE                   | PASS / PASS        | 184.956 / 184.956 / 184.956 / 0.000   | PASS / PASS        | 199.177 / 199.177 / 199.177 / 0.000   | 0/0           | none observed                     |
| 21  | FSR3 Native AA -> TAA         | PASS / PASS        | 471.628 / 190.745 / 471.628 / 280.883 | PASS / PASS        | 510.523 / 198.758 / 510.523 / 311.765 | 0/0           | none observed                     |
| 22  | TAA -> NONE                   | PASS / PASS        | 187.135 / 187.135 / 187.135 / 0.000   | PASS / PASS        | 180.607 / 180.607 / 180.607 / 0.000   | 0/0           | none observed                     |
| 27  | FSR3 Hoshipa -> NONE          | PASS / PASS        | 791.266 / 563.509 / 791.266 / 227.757 | PASS / PASS        | 745.257 / 510.004 / 745.257 / 235.253 | 0/0           | none observed                     |
| 30  | DLSS Ultra Performance -> TAA | PASS / PASS        | 732.473 / 545.773 / 732.473 / 186.700 | PASS / PASS        | 721.981 / 499.009 / 721.981 / 222.972 | 0/0           | none observed                     |
| 32  | FSR3 Native AA -> NONE        | PASS / PASS        | 507.556 / 198.965 / 507.556 / 308.591 | PASS / PASS        | 448.269 / 190.846 / 448.269 / 257.423 | 0/0           | none observed                     |

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    17472.367 |  17136.305 |     -336.063 |       17488.93 |     17485.57 |         -3.359 |    17500.285 |  17155.918 |     -344.367 |                   n.d. |
| System commit MiB                  |    55035.711 |  54834.793 |     -200.918 |      55202.566 |    55124.773 |        -77.793 |    55262.551 |   54832.98 |      -429.57 |                   n.d. |
| DXGI process usage MiB             |     4677.531 |   3641.859 |    -1035.672 |       3899.422 |     3952.309 |         52.887 |     4012.234 |   3659.516 |     -352.719 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        232 |          232 |            232 |          232 |              0 |            0 |        207 |          207 |                  0.892 |
| Estimated live tracked texture MiB |            0 |   2325.432 |     2325.432 |       2325.432 |     2325.432 |              0 |            0 |   2265.598 |     2265.598 |                  0.974 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -336.0625,
        "systemCommitMiB": -200.91796875,
        "dxgiUsageMiB": -1035.671875,
        "liveTextures": 232,
        "liveTextureMiB": 2325.431926727295
    },
    "pass2": {
        "processPrivateMiB": -344.3671875,
        "systemCommitMiB": -429.5703125,
        "dxgiUsageMiB": -352.71875,
        "liveTextures": 207,
        "liveTextureMiB": 2265.5977058410645
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

The classification is inconclusive. Process-private and system-commit
deltas are negative in both passes, so positive-pass-1 growth ratios are
unavailable. Fresh per-pass texture trackers do not prove leak freedom.

## Evidence and validation

-   `pwsh ./tools/git.ps1 diff --check -- docs/development/vr-render-scale-comparison-ledger.csv docs/development/vr-render-scale-iteration.md` passed. The new report also passed whitespace and local-link validation.
-   [Canonical ledger](vr-render-scale-ledger.md): all 1056 selected numeric timing cells verified (528 per build), with every historical cell intact.
-   The appended column also retains all 66 full timing objects, including relatch boundaries, milestone frames/QPC, source/destination, build/source and units.
-   [Finalizer summary](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z/summary.json): 66 terminal receipts; 24/24 required DLSS trace windows complete; 444 journal records and 4,416,326 scalar/null/empty values exported without filtering.
-   [Lossless scalar evidence](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z/evidence-values.csv), [receipt index](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z/receipt-index.json), and [transition table](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z/transitions.csv).
-   [Worker status](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z/worker-status.json): COMPLETE, cleanup verified inactive, pending evidence zero. Maximum client gap 49.806 ms against the 250 ms diagnostic budget.
-   [Comparison JSON](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z-comparison/comparison.json), [comparison CSV](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z-comparison/comparison.csv), [ledger audit](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z-comparison/ledger-validation.json), and [stage timings](../../artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z-comparison/reporting-performance.json).
-   Comparison generation took 2.618 seconds: tool identity 85.651 ms, comparison 2328.967 ms, ledger audit/update 67.498 ms. No regeneration or measurement replay was used.
-   Offline finalization ran with the exact physical DLL and adjacent manifest through tools/renderscale-tuning-finalizer/finalizer.js --variant nvidia --expected-rows 66 and the run/Build IDs above.
-   The maintained tools/compare-render-scale-ledger.py command used the named baseline/candidate, provenance and prepared ledger, validating the original ledger SHA-256 812955327b83634468ca9d5263f90c948095ce324258178548876674cf4d1d2b before publishing.
-   A first comparison invocation rejected an output directory nested inside the candidate run before generation; the successful invocation used a separate sibling directory.
-   No compilation, deployment, restart, persistence or third pass was performed. This is the tuning assay, not the separate release-qualification protocol.

PR inclusion remains the user's decision. The following detailed tables
are copied from the single generated comparison; its historical reporting
label is subject to the limitation above.

## Detailed baseline comparison

Change assessment: **DOES_NOT_MEET_STANDARD**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                     | Candidate                                                                                                       |
| ----------------------- | -------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nvidia-2026-09-10T07-19-50-405Z                                                              | renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z                                                              |
| Renderer base           | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                     | 89e3964582730b92c1885eaef8f865e2d1d02ea9                                                                        |
| Main-VR base/equivalent | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                     | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                                        |
| Compiled source         | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                                                     | 89e3964582730b92c1885eaef8f865e2d1d02ea9                                                                        |
| Build ID                | 07238b1fe36024d2c33081f023973c34887ce8a0fa4bd4743bf8a1c85b5ec78d                             | 880b0ef46dd92fc519bc53f577a21d2986b438133952822f350f18d11360169e                                                |
| DLL SHA-256             | 2101a4f7faa417b415ebb28dfa7b93a4ef39abf32ab9879f549f25da09bbef38                             | 6c7886e8e9420354c30e976edaf3b059227fa262a52092c0c729819852ce218f                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-2026-09-10T07-19-50-405Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z |

Assessment limits: candidate_health_standard_not_met; retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; incomplete_reporting; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 767.246/865.774 | 12.842       | 8/10        | 4/4          | 2/2                 | none             | NOT_MET/NOT_MET     |
| nvidia | 2    | 33/33    | 730.514/835.988 | 14.438       | 10/11       | 4/4          | 2/2                 | none             | NOT_MET/NOT_MET     |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 708.580   | 812.873   | 104.292  | 14.718  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.160    | 13.880    | 0.720    | 5.471   |
| nvidia | 1    | Relatch proof total        | ms          | 17714.506 | 20321.815 | 2607.309 | 14.718  |
| nvidia | 1    | Relatch proof total        | frames      | 329       | 347       | 18       | 5.471   |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 767.246   | 865.774   | 98.528   | 12.842  |
| nvidia | 1    | Strict completion mean     | frames      | 14.970    | 15.515    | 0.545    | 3.644   |
| nvidia | 1    | Strict completion total    | ms          | 25319.115 | 28570.534 | 3251.419 | 12.842  |
| nvidia | 1    | Strict completion total    | frames      | 494       | 512       | 18       | 3.644   |
| nvidia | 1    | Stretch completed episodes | episodes    | 18        | 19        | 1        | 5.556   |
| nvidia | 1    | Stretch completed total    | frames      | 74        | 83        | 9        | 12.162  |
| nvidia | 1    | Stretch completed total    | ms          | 4578.021  | 5324.710  | 746.690  | 16.310  |
| nvidia | 1    | Stretch longest episode    | ms          | 426.722   | 423.789   | -2.933   | -0.687  |
| nvidia | 2    | Relatch proof mean         | ms          | 682.641   | 782.504   | 99.863   | 14.629  |
| nvidia | 2    | Relatch proof mean         | frames      | 14.080    | 14        | -0.080   | -0.568  |
| nvidia | 2    | Relatch proof total        | ms          | 17066.032 | 19562.601 | 2496.569 | 14.629  |
| nvidia | 2    | Relatch proof total        | frames      | 352       | 350       | -2       | -0.568  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 730.514   | 835.988   | 105.475  | 14.438  |
| nvidia | 2    | Strict completion mean     | frames      | 15.394    | 15.455    | 0.061    | 0.394   |
| nvidia | 2    | Strict completion total    | ms          | 24106.946 | 27587.604 | 3480.659 | 14.438  |
| nvidia | 2    | Strict completion total    | frames      | 508       | 510       | 2        | 0.394   |
| nvidia | 2    | Stretch completed episodes | episodes    | 17        | 18        | 1        | 5.882   |
| nvidia | 2    | Stretch completed total    | frames      | 77        | 82        | 5        | 6.494   |
| nvidia | 2    | Stretch completed total    | ms          | 4310.798  | 5291.078  | 980.280  | 22.740  |
| nvidia | 2    | Stretch longest episode    | ms          | 417.844   | 487.150   | 69.306   | 16.587  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 669.500 / 815.555   | 146.055  | 21.816  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 181.499 / 242.947   | 61.449   | 33.856  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 289.827 / 405.023   | 115.196  | 39.746  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 920.565 / 1268.476  | 347.911  | 37.793  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 981.793 / 1292.094  | 310.301  | 31.606  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1099.691 / 1161.243 | 61.551   | 5.597   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1392.319 / 1472.065 | 79.746   | 5.728   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1343.752 / 1598.030 | 254.278  | 18.923  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1221.973 / 1279.377 | 57.404   | 4.698   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 740.453 / 810.162   | 69.709   | 9.414   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 486.216 / 445.232   | -40.985  | -8.429  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 155.001 / 184.956   | 29.954   | 19.325  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 937.889 / 848.372   | -89.517  | -9.545  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1339.594 / 1329.689 | -9.905   | -0.739  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 802.999 / 826.292   | 23.293   | 2.901   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 803.849 / 807.254   | 3.405    | 0.424   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 732.811 / 797.497   | 64.686   | 8.827   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 840.052 / 722.354   | -117.699 | -14.011 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 708.567 / 693.695   | -14.872  | -2.099  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 655.757 / 1031.523  | 375.766  | 57.303  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 475.970 / 471.628   | -4.342   | -0.912  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 174.895 / 187.135   | 12.239   | 6.998   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 270.911 / 265.124   | -5.787   | -2.136  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1024.970 / 1134.703 | 109.733  | 10.706  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 934.134 / 1570.129  | 635.994  | 68.084  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1378.302 / 1456.417 | 78.114   | 5.667   | 1/1         | 2/1 -> 2/1 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 665.093 / 791.266   | 126.173  | 18.971  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 841.685 / 904.465   | 62.780   | 7.459   | 0/0         | 2/1 -> 2/1 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1387.084 / 1562.382 | 175.298  | 12.638  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 644.207 / 732.473   | 88.266   | 13.701  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 562.726 / 666.674   | 103.948  | 18.472  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 417.950 / 507.556   | 89.606   | 21.440  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 237.079 / 288.747   | 51.668   | 21.794  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                        | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.294 / 540.542   | 669.500 / 815.555   | 168.206 / 275.013 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6851,"dispatchToBlockedOrPreparationMs":327.2972,"firstNewGenerationToCleanupDrainedMs":228.3937,"firstPhysicalMutationToFirstNewGenerationMs":111.124,"presentationToStrictCompletionMs":168.2062}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1056,"dispatchToBlockedOrPreparationMs":382.6022,"firstNewGenerationToCleanupDrainedMs":276.7581,"firstPhysicalMutationToFirstNewGenerationMs":153.0894,"presentationToStrictCompletionMs":275.0132}   |
| 2   | 181.499 / 242.947   | 181.499 / 242.947   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":181.4987,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":242.9473,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 289.827 / 405.023   | 289.827 / 405.023   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":289.8271,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":405.0232,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 695.179 / 996.312   | 836.902 / 1167.408  | 141.723 / 171.095 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.351,"dispatchToBlockedOrPreparationMs":338.413,"firstNewGenerationToCleanupDrainedMs":184.9359,"firstPhysicalMutationToFirstNewGenerationMs":310.2019,"presentationToStrictCompletionMs":225.3857}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7688,"dispatchToBlockedOrPreparationMs":515.7934,"firstNewGenerationToCleanupDrainedMs":222.7289,"firstPhysicalMutationToFirstNewGenerationMs":425.1168,"presentationToStrictCompletionMs":272.1635}   |
| 5   | 768.274 / 1063.253  | 896.303 / 1206.841  | 128.030 / 143.587 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3558,"dispatchToBlockedOrPreparationMs":397.8904,"firstNewGenerationToCleanupDrainedMs":172.3257,"firstPhysicalMutationToFirstNewGenerationMs":321.7316,"presentationToStrictCompletionMs":213.5189}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5042,"dispatchToBlockedOrPreparationMs":402.0202,"firstNewGenerationToCleanupDrainedMs":186.5747,"firstPhysicalMutationToFirstNewGenerationMs":614.7417,"presentationToStrictCompletionMs":228.8406}   |
| 6   | 898.584 / 936.847   | 1017.670 / 1071.864 | 119.086 / 135.017 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0717,"dispatchToBlockedOrPreparationMs":369.5131,"firstNewGenerationToCleanupDrainedMs":158.8355,"firstPhysicalMutationToFirstNewGenerationMs":486.2494,"presentationToStrictCompletionMs":201.1075}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9529,"dispatchToBlockedOrPreparationMs":404.8169,"firstNewGenerationToCleanupDrainedMs":180.1891,"firstPhysicalMutationToFirstNewGenerationMs":482.905,"presentationToStrictCompletionMs":224.3955}    |
| 7   | 1184.415 / 1253.468 | 1309.015 / 1388.281 | 124.600 / 134.813 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1909,"dispatchToBlockedOrPreparationMs":395.1179,"firstNewGenerationToCleanupDrainedMs":167.299,"firstPhysicalMutationToFirstNewGenerationMs":742.407,"presentationToStrictCompletionMs":207.9042}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.362,"dispatchToBlockedOrPreparationMs":444.5729,"firstNewGenerationToCleanupDrainedMs":178.7555,"firstPhysicalMutationToFirstNewGenerationMs":760.5905,"presentationToStrictCompletionMs":218.5966}    |
| 8   | 1159.017 / 1369.837 | 1256.318 / 1512.719 | 97.301 / 142.882  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8169,"dispatchToBlockedOrPreparationMs":323.115,"firstNewGenerationToCleanupDrainedMs":210.1568,"firstPhysicalMutationToFirstNewGenerationMs":720.2293,"presentationToStrictCompletionMs":184.7348}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4567,"dispatchToBlockedOrPreparationMs":482.7752,"firstNewGenerationToCleanupDrainedMs":187.4661,"firstPhysicalMutationToFirstNewGenerationMs":838.0212,"presentationToStrictCompletionMs":228.1928}   |
| 9   | 1000.332 / 1067.192 | 1135.000 / 1198.749 | 134.668 / 131.558 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2676,"dispatchToBlockedOrPreparationMs":324.6854,"firstNewGenerationToCleanupDrainedMs":180.084,"firstPhysicalMutationToFirstNewGenerationMs":626.9629,"presentationToStrictCompletionMs":221.6411}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5806,"dispatchToBlockedOrPreparationMs":356.1158,"firstNewGenerationToCleanupDrainedMs":173.2629,"firstPhysicalMutationToFirstNewGenerationMs":665.79,"presentationToStrictCompletionMs":212.1854}     |
| 10  | 566.234 / 639.622   | 740.453 / 810.162   | 174.220 / 170.540 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8785,"dispatchToBlockedOrPreparationMs":334.9253,"firstNewGenerationToCleanupDrainedMs":218.8826,"firstPhysicalMutationToFirstNewGenerationMs":183.7671,"presentationToStrictCompletionMs":174.2198}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0368,"dispatchToBlockedOrPreparationMs":392.9554,"firstNewGenerationToCleanupDrainedMs":218.7315,"firstPhysicalMutationToFirstNewGenerationMs":195.4387,"presentationToStrictCompletionMs":170.54}     |
| 11  | 195.936 / 170.061   | 486.216 / 445.232   | 290.280 / 275.170 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1218,"dispatchToBlockedOrPreparationMs":142.8027,"firstNewGenerationToCleanupDrainedMs":291.184,"firstPhysicalMutationToFirstNewGenerationMs":48.108,"presentationToStrictCompletionMs":290.2801}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.325,"dispatchToBlockedOrPreparationMs":126.9217,"firstNewGenerationToCleanupDrainedMs":275.9304,"firstPhysicalMutationToFirstNewGenerationMs":39.0545,"presentationToStrictCompletionMs":275.1701}     |
| 12  | 155.001 / 184.956   | 155.001 / 184.956   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":155.0011,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":184.9555,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 937.889 / 848.372   | 937.889 / 848.372   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":218.4206,"dispatchToBlockedOrPreparationMs":454.3682,"firstNewGenerationToCleanupDrainedMs":43.5433,"firstPhysicalMutationToFirstNewGenerationMs":221.5573,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":207.4184,"dispatchToBlockedOrPreparationMs":452.755,"firstNewGenerationToCleanupDrainedMs":46.1726,"firstPhysicalMutationToFirstNewGenerationMs":142.0262,"presentationToStrictCompletionMs":0}          |
| 14  | 1339.594 / 1156.328 | 1292.678 / 1284.480 | 0 / 128.152       | {"blockedOrPreparationToFirstPhysicalMutationMs":269.7296,"dispatchToBlockedOrPreparationMs":416.8972,"firstNewGenerationToCleanupDrainedMs":173.5496,"firstPhysicalMutationToFirstNewGenerationMs":432.5019,"presentationToStrictCompletionMs":0}       | {"blockedOrPreparationToFirstPhysicalMutationMs":271.8131,"dispatchToBlockedOrPreparationMs":409.0383,"firstNewGenerationToCleanupDrainedMs":169.9327,"firstPhysicalMutationToFirstNewGenerationMs":433.6959,"presentationToStrictCompletionMs":173.3605} |
| 15  | 802.999 / 826.292   | 622.719 / 608.558   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7832,"dispatchToBlockedOrPreparationMs":379.7732,"firstNewGenerationToCleanupDrainedMs":42.6334,"firstPhysicalMutationToFirstNewGenerationMs":196.5295,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4128,"dispatchToBlockedOrPreparationMs":350.5605,"firstNewGenerationToCleanupDrainedMs":42.1222,"firstPhysicalMutationToFirstNewGenerationMs":211.463,"presentationToStrictCompletionMs":0}            |
| 16  | 803.849 / 807.254   | 615.393 / 591.877   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7894,"dispatchToBlockedOrPreparationMs":359.0321,"firstNewGenerationToCleanupDrainedMs":44.8407,"firstPhysicalMutationToFirstNewGenerationMs":206.7308,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5944,"dispatchToBlockedOrPreparationMs":349.8263,"firstNewGenerationToCleanupDrainedMs":43.2092,"firstPhysicalMutationToFirstNewGenerationMs":195.2473,"presentationToStrictCompletionMs":0}           |
| 17  | 732.811 / 797.497   | 607.464 / 596.797   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3609,"dispatchToBlockedOrPreparationMs":375.1355,"firstNewGenerationToCleanupDrainedMs":41.1615,"firstPhysicalMutationToFirstNewGenerationMs":187.8058,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":6.069,"dispatchToBlockedOrPreparationMs":357.1323,"firstNewGenerationToCleanupDrainedMs":0.3813,"firstPhysicalMutationToFirstNewGenerationMs":233.2139,"presentationToStrictCompletionMs":0}             |
| 18  | 840.052 / 722.354   | 676.686 / 588.984   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":7.0764,"dispatchToBlockedOrPreparationMs":373.4732,"firstNewGenerationToCleanupDrainedMs":60.3653,"firstPhysicalMutationToFirstNewGenerationMs":235.7712,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9735,"dispatchToBlockedOrPreparationMs":349.1794,"firstNewGenerationToCleanupDrainedMs":42.9785,"firstPhysicalMutationToFirstNewGenerationMs":192.8523,"presentationToStrictCompletionMs":0}           |
| 19  | 708.567 / 693.695   | 585.888 / 603.176   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0818,"dispatchToBlockedOrPreparationMs":355.2031,"firstNewGenerationToCleanupDrainedMs":39.1574,"firstPhysicalMutationToFirstNewGenerationMs":187.446,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1032,"dispatchToBlockedOrPreparationMs":351.5923,"firstNewGenerationToCleanupDrainedMs":42.7802,"firstPhysicalMutationToFirstNewGenerationMs":204.7007,"presentationToStrictCompletionMs":0}           |
| 20  | 493.734 / 858.895   | 655.757 / 1031.523  | 162.023 / 172.627 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.628,"dispatchToBlockedOrPreparationMs":324.3054,"firstNewGenerationToCleanupDrainedMs":209.0784,"firstPhysicalMutationToFirstNewGenerationMs":117.7452,"presentationToStrictCompletionMs":162.0231}   | {"blockedOrPreparationToFirstPhysicalMutationMs":268.9147,"dispatchToBlockedOrPreparationMs":416.8007,"firstNewGenerationToCleanupDrainedMs":220.8636,"firstPhysicalMutationToFirstNewGenerationMs":124.9436,"presentationToStrictCompletionMs":172.6271} |
| 21  | 194.486 / 190.745   | 475.970 / 471.628   | 281.484 / 280.883 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.1194,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":281.4839}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":126.9013,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":280.8834}             |
| 22  | 174.895 / 187.135   | 174.895 / 187.135   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":174.8952,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":187.1346,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 270.911 / 265.124   | 270.911 / 265.124   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":270.9109,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                   | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":265.1242,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 866.555 / 954.281   | 1024.970 / 1134.703 | 158.415 / 180.422 | {"blockedOrPreparationToFirstPhysicalMutationMs":256.0541,"dispatchToBlockedOrPreparationMs":436.8501,"firstNewGenerationToCleanupDrainedMs":197.0475,"firstPhysicalMutationToFirstNewGenerationMs":135.0184,"presentationToStrictCompletionMs":158.415} | {"blockedOrPreparationToFirstPhysicalMutationMs":288.1015,"dispatchToBlockedOrPreparationMs":475.0288,"firstNewGenerationToCleanupDrainedMs":222.9675,"firstPhysicalMutationToFirstNewGenerationMs":148.6052,"presentationToStrictCompletionMs":180.4218} |
| 25  | 739.348 / 1349.836  | 856.854 / 1482.977  | 117.506 / 133.140 | {"blockedOrPreparationToFirstPhysicalMutationMs":30.9018,"dispatchToBlockedOrPreparationMs":355.0593,"firstNewGenerationToCleanupDrainedMs":156.1911,"firstPhysicalMutationToFirstNewGenerationMs":314.7019,"presentationToStrictCompletionMs":194.7865} | {"blockedOrPreparationToFirstPhysicalMutationMs":368.7944,"dispatchToBlockedOrPreparationMs":411.6404,"firstNewGenerationToCleanupDrainedMs":175.9802,"firstPhysicalMutationToFirstNewGenerationMs":526.5619,"presentationToStrictCompletionMs":220.2924} |
| 26  | 1378.302 / 1319.876 | 1326.476 / 1409.535 | 0 / 89.659        | {"blockedOrPreparationToFirstPhysicalMutationMs":257.0673,"dispatchToBlockedOrPreparationMs":387.3346,"firstNewGenerationToCleanupDrainedMs":189.5145,"firstPhysicalMutationToFirstNewGenerationMs":492.5595,"presentationToStrictCompletionMs":0}       | {"blockedOrPreparationToFirstPhysicalMutationMs":280.8923,"dispatchToBlockedOrPreparationMs":440.9256,"firstNewGenerationToCleanupDrainedMs":207.9769,"firstPhysicalMutationToFirstNewGenerationMs":479.7401,"presentationToStrictCompletionMs":136.5404} |
| 27  | 446.145 / 563.509   | 665.093 / 791.266   | 218.948 / 227.757 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3501,"dispatchToBlockedOrPreparationMs":313.9309,"firstNewGenerationToCleanupDrainedMs":219.6107,"firstPhysicalMutationToFirstNewGenerationMs":128.2013,"presentationToStrictCompletionMs":218.9481}  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7544,"dispatchToBlockedOrPreparationMs":394.3948,"firstNewGenerationToCleanupDrainedMs":228.209,"firstPhysicalMutationToFirstNewGenerationMs":164.9074,"presentationToStrictCompletionMs":227.7571}    |
| 28  | 759.193 / 773.440   | 799.619 / 859.338   | 40.425 / 85.898   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.5958,"dispatchToBlockedOrPreparationMs":373.1481,"firstNewGenerationToCleanupDrainedMs":170.9405,"firstPhysicalMutationToFirstNewGenerationMs":252.9344,"presentationToStrictCompletionMs":82.4913}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0701,"dispatchToBlockedOrPreparationMs":413.753,"firstNewGenerationToCleanupDrainedMs":169.7135,"firstPhysicalMutationToFirstNewGenerationMs":272.8013,"presentationToStrictCompletionMs":131.0248}    |
| 29  | 1190.855 / 1324.502 | 1310.255 / 1469.657 | 119.400 / 145.155 | {"blockedOrPreparationToFirstPhysicalMutationMs":21.0974,"dispatchToBlockedOrPreparationMs":631.0741,"firstNewGenerationToCleanupDrainedMs":160.4408,"firstPhysicalMutationToFirstNewGenerationMs":497.6428,"presentationToStrictCompletionMs":196.2287} | {"blockedOrPreparationToFirstPhysicalMutationMs":23.4924,"dispatchToBlockedOrPreparationMs":759.4005,"firstNewGenerationToCleanupDrainedMs":193.7827,"firstPhysicalMutationToFirstNewGenerationMs":492.9816,"presentationToStrictCompletionMs":237.8805}  |
| 30  | 428.091 / 545.773   | 644.207 / 732.473   | 216.117 / 186.700 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9414,"dispatchToBlockedOrPreparationMs":316.259,"firstNewGenerationToCleanupDrainedMs":216.9443,"firstPhysicalMutationToFirstNewGenerationMs":108.0628,"presentationToStrictCompletionMs":216.1168}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0496,"dispatchToBlockedOrPreparationMs":378.6991,"firstNewGenerationToCleanupDrainedMs":236.3111,"firstPhysicalMutationToFirstNewGenerationMs":114.4137,"presentationToStrictCompletionMs":186.7001}   |
| 31  | 562.726 / 666.674   | 562.726 / 666.674   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":43.7834,"dispatchToBlockedOrPreparationMs":395.4822,"firstNewGenerationToCleanupDrainedMs":40.431,"firstPhysicalMutationToFirstNewGenerationMs":83.0294,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":68.1274,"dispatchToBlockedOrPreparationMs":434.8908,"firstNewGenerationToCleanupDrainedMs":51.6082,"firstPhysicalMutationToFirstNewGenerationMs":112.0478,"presentationToStrictCompletionMs":0}          |
| 32  | 174.944 / 198.965   | 417.950 / 507.556   | 243.005 / 308.591 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":117.7198,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":243.0051}            | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":134.9108,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":308.591}              |
| 33  | 237.079 / 288.747   | 237.079 / 288.747   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":237.079,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":288.7469,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 441.106 / 538.797    | 97.691   | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 4             | -1           |
| 4   | 12 / 12                  | 0            | 651.966 / 944.679    | 292.713  | 17 / 17           | 0            |
| 5   | 14 / 12                  | -2           | 723.978 / 1020.266   | 296.288  | 19 / 17           | -2           |
| 6   | 18 / 18                  | 0            | 858.834 / 891.675    | 32.841   | 23 / 23           | 0            |
| 7   | 17 / 18                  | 1            | 1141.716 / 1209.525  | 67.810   | 22 / 23           | 1            |
| 8   | 16 / 16                  | 0            | 1046.161 / 1325.253  | 279.092  | 21 / 21           | 0            |
| 9   | 16 / 17                  | 1            | 954.916 / 1025.486   | 70.571   | 21 / 22           | 1            |
| 10  | 9 / 10                   | 1            | 521.571 / 591.431    | 69.860   | 14 / 16           | 2            |
| 11  | 3 / 4                    | 1            | 195.032 / 169.301    | -25.731  | 10 / 11           | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 10 / 9                   | -1           | 894.346 / 802.200    | -92.146  | 11 / 10           | -1           |
| 14  | 23 / 22                  | -1           | 1119.129 / 1114.547  | -4.581   | 28 / 27           | -1           |
| 15  | 11 / 12                  | 1            | 580.086 / 566.436    | -13.650  | 16 / 18           | 2            |
| 16  | 12 / 12                  | 0            | 570.552 / 548.668    | -21.884  | 17 / 19           | 2            |
| 17  | 12 / 12                  | 0            | 566.302 / 596.415    | 30.113   | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 616.321 / 546.005    | -70.316  | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 546.731 / 560.396    | 13.665   | 16 / 15           | -1           |
| 20  | 10 / 16                  | 6            | 446.679 / 810.659    | 363.980  | 15 / 22           | 7            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 15 / 15                  | 0            | 827.923 / 911.736    | 83.813   | 20 / 20           | 0            |
| 25  | 13 / 23                  | 10           | 700.663 / 1306.997   | 606.334  | 18 / 28           | 10           |
| 26  | 23 / 23                  | 0            | 1136.961 / 1201.558  | 64.597   | 28 / 28           | 0            |
| 27  | 10 / 10                  | 0            | 445.482 / 563.057    | 117.574  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 628.678 / 689.624    | 60.946   | 16 / 16           | 0            |
| 29  | 23 / 23                  | 0            | 1149.814 / 1275.874  | 126.060  | 28 / 28           | 0            |
| 30  | 9 / 9                    | 0            | 427.263 / 496.162    | 68.899   | 15 / 15           | 0            |
| 31  | 9 / 9                    | 0            | 522.295 / 615.066    | 92.771   | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 114.697 / 157.157 | 42.460   |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 201.515 / 269.710 | 68.195   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 200.370 / 215.206 | 14.836   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.828 / 376.870 | -3.957   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 393.856 / 417.014 | 23.158   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 426.722 / 423.789 | -2.933   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 371.096 / 396.885 | 25.790   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 278.286 / 275.546 | -2.740   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 141.276 / 147.452 | 6.177    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 149.235 / 140.080 | -9.155   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 138.085 / 162.759 | 24.674   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 176.326 / 142.369 | -33.958  |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 137.756 / 154.947 | 17.191   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0 / 340.786       | 340.786  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 199.863 / 385.139 | 185.276  |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 588.339 / 598.351 | 10.011   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 11 / 11            | 0     | 679.770 / 720.650 | 40.880   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 734.886 / 787.806   | 52.920   | 7.201   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 177.782 / 178.046   | 0.264    | 0.148   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 260.525 / 254.211   | -6.314   | -2.424  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 906.681 / 961.099   | 54.419   | 6.002   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 904.298 / 1035.564  | 131.266  | 14.516  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1039.935 / 1312.792 | 272.857  | 26.238  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1064.043 / 1285.890 | 221.847  | 20.849  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1205.586 / 1179.722 | -25.863  | -2.145  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1112.825 / 1216.345 | 103.520  | 9.302   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 729.996 / 812.303   | 82.307   | 11.275  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 431.684 / 482.447   | 50.763   | 11.759  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 154.506 / 199.177   | 44.671   | 28.912  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 567.794 / 645.721   | 77.927   | 13.724  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1214.111 / 1556.222 | 342.110  | 28.178  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 722.015 / 736.960   | 14.945   | 2.070   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 623.909 / 770.705   | 146.796  | 23.528  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 699.393 / 808.174   | 108.781  | 15.554  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 670.129 / 727.223   | 57.094   | 8.520   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 698.477 / 737.751   | 39.273   | 5.623   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 643.825 / 1043.912  | 400.086  | 62.142  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 400.298 / 510.523   | 110.225  | 27.536  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 154.918 / 180.607   | 25.689   | 16.583  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 230.219 / 326.185   | 95.966   | 41.685  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1039.184 / 1123.809 | 84.625   | 8.143   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1658.352 / 1866.656 | 208.304  | 12.561  | 2/2         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1227.931 / 1472.089 | 244.158  | 19.884  | 1/1         | 2/1 -> 2/1 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 696.952 / 745.257   | 48.305   | 6.931   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 842.921 / 889.014   | 46.094   | 5.468   | 0/0         | 2/1 -> 2/1 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1330.059 / 1443.906 | 113.847  | 8.560   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 618.367 / 721.981   | 103.614  | 16.756  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 697.904 / 880.277   | 182.372  | 26.131  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 412.420 / 448.269   | 35.849   | 8.692   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 235.021 / 246.959   | 11.939   | 5.080   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 502.328 / 533.107   | 734.886 / 787.806   | 232.558 / 254.699 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1491,"dispatchToBlockedOrPreparationMs":385.6548,"firstNewGenerationToCleanupDrainedMs":233.271,"firstPhysicalMutationToFirstNewGenerationMs":112.8113,"presentationToStrictCompletionMs":232.5584}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0158,"dispatchToBlockedOrPreparationMs":388.7269,"firstNewGenerationToCleanupDrainedMs":255.4223,"firstPhysicalMutationToFirstNewGenerationMs":140.6413,"presentationToStrictCompletionMs":254.6989}   |
| 2   | 177.782 / 178.046   | 177.782 / 178.046   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.7818,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":178.0457,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 260.525 / 254.211   | 260.525 / 254.211   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":260.5254,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.2114,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 716.662 / 831.380   | 829.553 / 874.609   | 112.891 / 43.229  | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8646,"dispatchToBlockedOrPreparationMs":380.6282,"firstNewGenerationToCleanupDrainedMs":151.6239,"firstPhysicalMutationToFirstNewGenerationMs":294.4361,"presentationToStrictCompletionMs":190.0182}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4396,"dispatchToBlockedOrPreparationMs":371.0893,"firstNewGenerationToCleanupDrainedMs":173.0905,"firstPhysicalMutationToFirstNewGenerationMs":326.9899,"presentationToStrictCompletionMs":129.7192}   |
| 5   | 700.205 / 784.654   | 819.578 / 929.039   | 119.374 / 144.385 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3144,"dispatchToBlockedOrPreparationMs":356.8476,"firstNewGenerationToCleanupDrainedMs":157.9589,"firstPhysicalMutationToFirstNewGenerationMs":301.4574,"presentationToStrictCompletionMs":204.093}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9368,"dispatchToBlockedOrPreparationMs":373.8561,"firstNewGenerationToCleanupDrainedMs":191.7222,"firstPhysicalMutationToFirstNewGenerationMs":359.5244,"presentationToStrictCompletionMs":250.9099}   |
| 6   | 813.263 / 1057.241  | 953.048 / 1216.011  | 139.784 / 158.771 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.8019,"dispatchToBlockedOrPreparationMs":323.5516,"firstNewGenerationToCleanupDrainedMs":179.079,"firstPhysicalMutationToFirstNewGenerationMs":447.6151,"presentationToStrictCompletionMs":226.6718}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5368,"dispatchToBlockedOrPreparationMs":396.3659,"firstNewGenerationToCleanupDrainedMs":212.473,"firstPhysicalMutationToFirstNewGenerationMs":603.6356,"presentationToStrictCompletionMs":255.5518}    |
| 7   | 859.186 / 1046.583  | 980.442 / 1190.100  | 121.255 / 143.517 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6249,"dispatchToBlockedOrPreparationMs":346.5762,"firstNewGenerationToCleanupDrainedMs":161.8545,"firstPhysicalMutationToFirstNewGenerationMs":468.3859,"presentationToStrictCompletionMs":204.8563}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3246,"dispatchToBlockedOrPreparationMs":438.7591,"firstNewGenerationToCleanupDrainedMs":192.2026,"firstPhysicalMutationToFirstNewGenerationMs":555.8133,"presentationToStrictCompletionMs":239.3072}   |
| 8   | 985.673 / 949.191   | 1120.593 / 1089.830 | 134.920 / 140.640 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6332,"dispatchToBlockedOrPreparationMs":408.503,"firstNewGenerationToCleanupDrainedMs":178.6233,"firstPhysicalMutationToFirstNewGenerationMs":529.8337,"presentationToStrictCompletionMs":219.9124}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1974,"dispatchToBlockedOrPreparationMs":388.3029,"firstNewGenerationToCleanupDrainedMs":184.9788,"firstPhysicalMutationToFirstNewGenerationMs":513.3513,"presentationToStrictCompletionMs":230.5318}   |
| 9   | 911.436 / 989.364   | 1028.647 / 1127.928 | 117.211 / 138.565 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.061,"dispatchToBlockedOrPreparationMs":366.7606,"firstNewGenerationToCleanupDrainedMs":155.644,"firstPhysicalMutationToFirstNewGenerationMs":503.1813,"presentationToStrictCompletionMs":201.3885}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4132,"dispatchToBlockedOrPreparationMs":406.4985,"firstNewGenerationToCleanupDrainedMs":184.314,"firstPhysicalMutationToFirstNewGenerationMs":533.7026,"presentationToStrictCompletionMs":226.9813}    |
| 10  | 576.899 / 630.486   | 729.996 / 812.303   | 153.098 / 181.817 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.7523,"dispatchToBlockedOrPreparationMs":344.4053,"firstNewGenerationToCleanupDrainedMs":198.0434,"firstPhysicalMutationToFirstNewGenerationMs":184.7953,"presentationToStrictCompletionMs":153.0977}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8616,"dispatchToBlockedOrPreparationMs":369.8292,"firstNewGenerationToCleanupDrainedMs":231.7567,"firstPhysicalMutationToFirstNewGenerationMs":206.8557,"presentationToStrictCompletionMs":181.8168}   |
| 11  | 180.330 / 188.792   | 431.684 / 482.447   | 251.354 / 293.654 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1532,"dispatchToBlockedOrPreparationMs":135.7273,"firstNewGenerationToCleanupDrainedMs":252.1991,"firstPhysicalMutationToFirstNewGenerationMs":40.6043,"presentationToStrictCompletionMs":251.3542}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.383,"dispatchToBlockedOrPreparationMs":143.1466,"firstNewGenerationToCleanupDrainedMs":294.4679,"firstPhysicalMutationToFirstNewGenerationMs":41.4491,"presentationToStrictCompletionMs":293.6542}     |
| 12  | 154.506 / 199.177   | 154.506 / 199.177   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.5059,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":199.1772,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 567.794 / 645.721   | 567.794 / 645.721   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.0735,"dispatchToBlockedOrPreparationMs":380.9779,"firstNewGenerationToCleanupDrainedMs":43.6037,"firstPhysicalMutationToFirstNewGenerationMs":92.1392,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":58.073,"dispatchToBlockedOrPreparationMs":435.5755,"firstNewGenerationToCleanupDrainedMs":46.9898,"firstPhysicalMutationToFirstNewGenerationMs":105.0828,"presentationToStrictCompletionMs":0}           |
| 14  | 1082.938 / 1405.433 | 1166.144 / 1505.063 | 83.207 / 99.630   | {"blockedOrPreparationToFirstPhysicalMutationMs":253.1325,"dispatchToBlockedOrPreparationMs":360.6436,"firstNewGenerationToCleanupDrainedMs":164.0358,"firstPhysicalMutationToFirstNewGenerationMs":388.3325,"presentationToStrictCompletionMs":131.1736} | {"blockedOrPreparationToFirstPhysicalMutationMs":291.6339,"dispatchToBlockedOrPreparationMs":463.3182,"firstNewGenerationToCleanupDrainedMs":213.8723,"firstPhysicalMutationToFirstNewGenerationMs":536.2383,"presentationToStrictCompletionMs":150.7889} |
| 15  | 722.015 / 736.960   | 567.055 / 607.865   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.478,"dispatchToBlockedOrPreparationMs":330.129,"firstNewGenerationToCleanupDrainedMs":44.9991,"firstPhysicalMutationToFirstNewGenerationMs":187.4488,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9892,"dispatchToBlockedOrPreparationMs":360.8936,"firstNewGenerationToCleanupDrainedMs":43.1927,"firstPhysicalMutationToFirstNewGenerationMs":199.7891,"presentationToStrictCompletionMs":0}           |
| 16  | 623.909 / 770.705   | 542.328 / 597.030   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0964,"dispatchToBlockedOrPreparationMs":322.2486,"firstNewGenerationToCleanupDrainedMs":36.6532,"firstPhysicalMutationToFirstNewGenerationMs":179.3302,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4847,"dispatchToBlockedOrPreparationMs":357.6653,"firstNewGenerationToCleanupDrainedMs":41.5441,"firstPhysicalMutationToFirstNewGenerationMs":193.3364,"presentationToStrictCompletionMs":0}           |
| 17  | 699.393 / 808.174   | 557.760 / 671.634   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4787,"dispatchToBlockedOrPreparationMs":331.5922,"firstNewGenerationToCleanupDrainedMs":39.6883,"firstPhysicalMutationToFirstNewGenerationMs":183.0012,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.8271,"dispatchToBlockedOrPreparationMs":397.4119,"firstNewGenerationToCleanupDrainedMs":44.2419,"firstPhysicalMutationToFirstNewGenerationMs":224.1534,"presentationToStrictCompletionMs":0}           |
| 18  | 670.129 / 727.223   | 552.224 / 594.022   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2756,"dispatchToBlockedOrPreparationMs":331.165,"firstNewGenerationToCleanupDrainedMs":37.7262,"firstPhysicalMutationToFirstNewGenerationMs":180.0575,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0523,"dispatchToBlockedOrPreparationMs":354.4465,"firstNewGenerationToCleanupDrainedMs":42.9673,"firstPhysicalMutationToFirstNewGenerationMs":192.5558,"presentationToStrictCompletionMs":0}           |
| 19  | 698.477 / 737.751   | 579.160 / 605.478   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.791,"dispatchToBlockedOrPreparationMs":355.396,"firstNewGenerationToCleanupDrainedMs":37.6891,"firstPhysicalMutationToFirstNewGenerationMs":182.2836,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1227,"dispatchToBlockedOrPreparationMs":351.7231,"firstNewGenerationToCleanupDrainedMs":41.8478,"firstPhysicalMutationToFirstNewGenerationMs":207.7844,"presentationToStrictCompletionMs":0}           |
| 20  | 486.824 / 853.402   | 643.825 / 1043.912  | 157.002 / 190.510 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5842,"dispatchToBlockedOrPreparationMs":325.8008,"firstNewGenerationToCleanupDrainedMs":201.587,"firstPhysicalMutationToFirstNewGenerationMs":112.8534,"presentationToStrictCompletionMs":157.0017}    | {"blockedOrPreparationToFirstPhysicalMutationMs":268.5745,"dispatchToBlockedOrPreparationMs":411.4681,"firstNewGenerationToCleanupDrainedMs":236.4395,"firstPhysicalMutationToFirstNewGenerationMs":127.4297,"presentationToStrictCompletionMs":190.5097} |
| 21  | 168.741 / 198.758   | 400.298 / 510.523   | 231.557 / 311.765 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":114.5782,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":231.557}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":131.6147,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":311.7646}             |
| 22  | 154.918 / 180.607   | 154.918 / 180.607   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.9181,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":180.6075,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 230.219 / 326.185   | 230.219 / 326.185   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":230.2187,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":326.185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 24  | 861.456 / 932.785   | 1039.184 / 1123.809 | 177.728 / 191.024 | {"blockedOrPreparationToFirstPhysicalMutationMs":253.7354,"dispatchToBlockedOrPreparationMs":412.073,"firstNewGenerationToCleanupDrainedMs":218.2977,"firstPhysicalMutationToFirstNewGenerationMs":155.0781,"presentationToStrictCompletionMs":177.7282}  | {"blockedOrPreparationToFirstPhysicalMutationMs":263.7682,"dispatchToBlockedOrPreparationMs":478.1888,"firstNewGenerationToCleanupDrainedMs":234.4865,"firstPhysicalMutationToFirstNewGenerationMs":147.3659,"presentationToStrictCompletionMs":191.0243} |
| 25  | 1461.034 / 1646.770 | 1578.508 / 1780.080 | 117.475 / 133.311 | {"blockedOrPreparationToFirstPhysicalMutationMs":592.2031,"dispatchToBlockedOrPreparationMs":370.4295,"firstNewGenerationToCleanupDrainedMs":157.0104,"firstPhysicalMutationToFirstNewGenerationMs":458.8654,"presentationToStrictCompletionMs":197.3187} | {"blockedOrPreparationToFirstPhysicalMutationMs":648.6257,"dispatchToBlockedOrPreparationMs":420.2399,"firstNewGenerationToCleanupDrainedMs":177.794,"firstPhysicalMutationToFirstNewGenerationMs":533.4205,"presentationToStrictCompletionMs":219.8867}  |
| 26  | 1227.931 / 1268.873 | 1186.869 / 1416.550 | 0 / 147.677       | {"blockedOrPreparationToFirstPhysicalMutationMs":250.7248,"dispatchToBlockedOrPreparationMs":358.9416,"firstNewGenerationToCleanupDrainedMs":151.131,"firstPhysicalMutationToFirstNewGenerationMs":426.0714,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":281.6308,"dispatchToBlockedOrPreparationMs":456.0929,"firstNewGenerationToCleanupDrainedMs":194.1874,"firstPhysicalMutationToFirstNewGenerationMs":484.6393,"presentationToStrictCompletionMs":203.216}  |
| 27  | 529.267 / 510.004   | 696.952 / 745.257   | 167.684 / 235.253 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4841,"dispatchToBlockedOrPreparationMs":353.5913,"firstNewGenerationToCleanupDrainedMs":235.3675,"firstPhysicalMutationToFirstNewGenerationMs":104.5086,"presentationToStrictCompletionMs":167.6843}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8529,"dispatchToBlockedOrPreparationMs":360.6471,"firstNewGenerationToCleanupDrainedMs":235.9051,"firstPhysicalMutationToFirstNewGenerationMs":143.8518,"presentationToStrictCompletionMs":235.2528}   |
| 28  | 842.921 / 712.601   | 795.130 / 843.261   | 0 / 130.660       | {"blockedOrPreparationToFirstPhysicalMutationMs":2.6177,"dispatchToBlockedOrPreparationMs":359.1764,"firstNewGenerationToCleanupDrainedMs":166.7212,"firstPhysicalMutationToFirstNewGenerationMs":266.6143,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8771,"dispatchToBlockedOrPreparationMs":387.4098,"firstNewGenerationToCleanupDrainedMs":174.3788,"firstPhysicalMutationToFirstNewGenerationMs":277.5953,"presentationToStrictCompletionMs":176.4132}   |
| 29  | 1139.899 / 1232.101 | 1253.666 / 1358.859 | 113.767 / 126.758 | {"blockedOrPreparationToFirstPhysicalMutationMs":22.3652,"dispatchToBlockedOrPreparationMs":652.6467,"firstNewGenerationToCleanupDrainedMs":151.9158,"firstPhysicalMutationToFirstNewGenerationMs":426.7381,"presentationToStrictCompletionMs":190.1601}  | {"blockedOrPreparationToFirstPhysicalMutationMs":22.252,"dispatchToBlockedOrPreparationMs":701.3946,"firstNewGenerationToCleanupDrainedMs":168.8035,"firstPhysicalMutationToFirstNewGenerationMs":466.4084,"presentationToStrictCompletionMs":211.8051}   |
| 30  | 407.171 / 499.009   | 618.367 / 721.981   | 211.196 / 222.972 | {"blockedOrPreparationToFirstPhysicalMutationMs":2.7039,"dispatchToBlockedOrPreparationMs":306.8064,"firstNewGenerationToCleanupDrainedMs":211.8148,"firstPhysicalMutationToFirstNewGenerationMs":97.0419,"presentationToStrictCompletionMs":211.1963}    | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9278,"dispatchToBlockedOrPreparationMs":387.9142,"firstNewGenerationToCleanupDrainedMs":223.5259,"firstPhysicalMutationToFirstNewGenerationMs":107.6132,"presentationToStrictCompletionMs":222.9723}   |
| 31  | 697.904 / 880.277   | 697.904 / 880.277   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":58.6527,"dispatchToBlockedOrPreparationMs":406.124,"firstNewGenerationToCleanupDrainedMs":38.7281,"firstPhysicalMutationToFirstNewGenerationMs":194.3997,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":62.8058,"dispatchToBlockedOrPreparationMs":500.5994,"firstNewGenerationToCleanupDrainedMs":47.6674,"firstPhysicalMutationToFirstNewGenerationMs":269.2039,"presentationToStrictCompletionMs":0}          |
| 32  | 177.159 / 190.846   | 412.420 / 448.269   | 235.261 / 257.423 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.6117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":235.2609}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":128.5633,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.4234}             |
| 33  | 235.021 / 246.959   | 235.021 / 246.959   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":235.0208,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":246.9594,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 11 / 9                   | -2           | 501.615 / 532.384    | 30.769   | 16 / 14           | -2           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 13 / 12                  | -1           | 677.929 / 701.519    | 23.590   | 18 / 17           | -1           |
| 5   | 13 / 13                  | 0            | 661.619 / 737.317    | 75.698   | 18 / 18           | 0            |
| 6   | 16 / 16                  | 0            | 773.969 / 1003.538   | 229.570  | 21 / 21           | 0            |
| 7   | 16 / 16                  | 0            | 818.587 / 997.897    | 179.310  | 21 / 21           | 0            |
| 8   | 18 / 16                  | -2           | 941.970 / 904.852    | -37.118  | 23 / 21           | -2           |
| 9   | 17 / 17                  | 0            | 873.003 / 943.614    | 70.611   | 22 / 22           | 0            |
| 10  | 10 / 10                  | 0            | 531.953 / 580.547    | 48.594   | 15 / 16           | 1            |
| 11  | 4 / 3                    | -1           | 179.485 / 187.979    | 8.494    | 11 / 9            | -2           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 9                    | 0            | 524.191 / 598.731    | 74.541   | 10 / 10           | 0            |
| 14  | 23 / 23                  | 0            | 1002.109 / 1291.190  | 289.082  | 28 / 28           | 0            |
| 15  | 12 / 12                  | 0            | 522.056 / 564.672    | 42.616   | 17 / 16           | -1           |
| 16  | 12 / 12                  | 0            | 505.675 / 555.486    | 49.811   | 15 / 17           | 2            |
| 17  | 12 / 11                  | -1           | 518.072 / 627.392    | 109.320  | 16 / 15           | -1           |
| 18  | 12 / 12                  | 0            | 514.498 / 551.055    | 36.557   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 541.471 / 563.630    | 22.160   | 16 / 16           | 0            |
| 20  | 10 / 16                  | 6            | 442.238 / 807.472    | 365.234  | 15 / 22           | 7            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 16 / 15                  | -1           | 820.886 / 889.323    | 68.436   | 21 / 20           | -1           |
| 25  | 29 / 29                  | 0            | 1421.498 / 1602.286  | 180.788  | 34 / 34           | 0            |
| 26  | 22 / 23                  | 1            | 1035.738 / 1222.363  | 186.625  | 27 / 28           | 1            |
| 27  | 11 / 10                  | -1           | 461.584 / 509.352    | 47.768   | 17 / 16           | -1           |
| 28  | 11 / 11                  | 0            | 628.408 / 668.882    | 40.474   | 16 / 16           | 0            |
| 29  | 24 / 23                  | -1           | 1101.750 / 1190.055  | 88.305   | 29 / 28           | -1           |
| 30  | 9 / 10                   | 1            | 406.552 / 498.455    | 91.903   | 14 / 16           | 2            |
| 31  | 10 / 10                  | 0            | 659.176 / 832.609    | 173.433  | 12 / 11           | -1           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 193.586 / 214.796 | 21.209   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 193.446 / 237.649 | 44.204   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 350.457 / 487.150 | 136.693  |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 366.733 / 418.467 | 51.734   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 417.844 / 404.370 | -13.474  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 388.059 / 407.732 | 19.673   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 247.670 / 361.023 | 113.353  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 132.586 / 145.276 | 12.690   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 128.082 / 138.187 | 10.105   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 133.435 / 156.954 | 23.519   |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 131.632 / 141.348 | 9.716    |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 128.674 / 153.561 | 24.886   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0 / 345.476       | 345.476  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 353.264 / 409.900 | 56.636   |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 533.118 / 603.836 | 70.718   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 2 / 2                | 0     | 11 / 11            | 0     | 612.211 / 665.353 | 53.142   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## Cumulative gates and other health evidence

### nvidia-2026-09-10T07-19-50-405Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":74,"allowedPresentationStretchEpisodes":18,"allowedPresentationStretchEyeObservations":161,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":127406,"leftPath":"NativeOriginal","referenceFrame":127829,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                         | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### nvidia-2026-09-10T07-19-50-405Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":77,"allowedPresentationStretchEpisodes":17,"allowedPresentationStretchEyeObservations":167,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":132453,"leftPath":"NativeOriginal","referenceFrame":132888,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                         | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":83,"allowedPresentationStretchEpisodes":19,"allowedPresentationStretchEyeObservations":180,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":126823,"leftPath":"NativeOriginal","referenceFrame":127196,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                         | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":82,"allowedPresentationStretchEpisodes":18,"allowedPresentationStretchEyeObservations":178,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":131342,"leftPath":"NativeOriginal","referenceFrame":131738,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                         | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

## Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 17082.26953125 / 16608.77734375 / -473.4921875 | 17472.3671875 / 17136.3046875 / -336.0625      | 137.430               |
| nvidia | 1    | systemCommitMiB   | 49931.01953125 / 49272.2421875 / -658.77734375 | 55035.7109375 / 54834.79296875 / -200.91796875 | 457.859               |
| nvidia | 1    | dxgiUsageMiB      | 4083.84375 / 3340.59375 / -743.25              | 4677.53125 / 3641.859375 / -1035.671875        | -292.422              |
| nvidia | 1    | liveTextures      | 0 / 217 / 217                                  | 0 / 232 / 232                                  | 15                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2281.5979194641113 / 2281.5979194641113    | 0 / 2325.431926727295 / 2325.431926727295      | 43.834                |
| nvidia | 2    | processPrivateMiB | 16990.12890625 / 16624.8671875 / -365.26171875 | 17500.28515625 / 17155.91796875 / -344.3671875 | 20.895                |
| nvidia | 2    | systemCommitMiB   | 49759.61328125 / 49175.984375 / -583.62890625  | 55262.55078125 / 54832.98046875 / -429.5703125 | 154.059               |
| nvidia | 2    | dxgiUsageMiB      | 3657.33203125 / 3244.27734375 / -413.0546875   | 4012.234375 / 3659.515625 / -352.71875         | 60.336                |
| nvidia | 2    | liveTextures      | 0 / 207 / 207                                  | 0 / 207 / 207                                  | 0                     |
| nvidia | 2    | liveTextureMiB    | 0 / 2265.5977058410645 / 2265.5977058410645    | 0 / 2265.5977058410645 / 2265.5977058410645    | 0                     |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2284        | 2143        | -141        |
| cpu/compactPresentationContract/reuses                  | 2262        | 2121        | -141        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 71          | 70          | -1          |
| cpu/generationResourceValidation/contractPublishes      | 152         | 154         | 2           |
| cpu/generationResourceValidation/fullValidations        | 559         | 577         | 18          |
| cpu/generationResourceValidation/stableChecks           | 8618        | 8128        | -490        |
| cpu/generationResourceValidation/stableHits             | 8541        | 8049        | -492        |
| cpu/generationResourceValidation/stableMisses           | 77          | 79          | 2           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 1           | -1          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4450        | 4055        | -395        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4450        | 4055        | -395        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4408        | 4013        | -395        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4450        | 4055        | -395        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4429        | 4034        | -395        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 35          | 34          | -1          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4415        | 4021        | -394        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4360        | 3961        | -399        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 94          | 4           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4435        | 4040        | -395        |
| cpu/strongStereoPacket/captures                         | 4726        | 4454        | -272        |
| cpu/strongStereoPacket/commitAccepts                    | 4549        | 4266        | -283        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 66          | 1           |
| cpu/strongStereoPacket/commitValidations                | 4614        | 4332        | -282        |
| cpu/strongStereoPacket/cycleReuses                      | 2323        | 2186        | -137        |
| cpu/strongStereoPacket/fastSkips                        | 4174        | 3656        | -518        |
| cpu/strongStereoPacket/invalidations                    | 5002        | 4601        | -401        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 103         | 3           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2303        | 2165        | -138        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.872       | 1.907       | 0.035       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 17.200      | 24.200      | 7           |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.136       | 0.138       | 0.001       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.200       | 2.100       | 0.900       |
| cpu/window/currentFrame                                 | 127830      | 127196      | -634        |
| cpu/window/elapsedFrames                                | 4450        | 4055        | -395        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 123380      | 123141      | -239        |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 127830      | 127197      | -633        |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6418778256  | 5842750320  | -576027936  |
| gpu/item10PeripheryTAAHistory/dispatches                | 5059        | 4605        | -454        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12850669440 | 11697436800 | -1153232640 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.307       | 0.000       |
| gpu/item5ActiveFSRCopies/activePixels                   | 12352231120 | 11730673160 | -621557960  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27883903280 | 26473333240 | -1410570040 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15840       | 15040       | -800        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2075        | 1989        | -86         |
| gpu/item7EarlyHAM/executedClears                        | 2134        | 1918        | -216        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2134        | 1918        | -216        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4368        | 4066        | -302        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4450        | 4056        | -394        |
| gpu/startFrame                                          | 123380      | 123141      | -239        |
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
| texture/createdCount                                    | 3922        | 3962        | 40          |
| texture/createdEstimatedBytes                           | 38663664176 | 38813826760 | 150162584   |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3705        | 3730        | 25          |
| texture/destroyedEstimatedBytes                         | 36271235356 | 36375434652 | 104199296   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 868         | 886         | 18          |
| texture/liveTextureRecordCount                          | 217         | 232         | 15          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 10          | 21          | 11          |
| texture/niSourceTextureMatchedEstimatedBytes            | 16777440    | 62740328    | 45962888    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1484        | 1507        | 23          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 217         | 232         | 15          |
| texture/outstandingEstimatedBytes                       | 2392428820  | 2438392108  | 45963288    |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 1           | -1          |
| texture/supported                                       | true        | true        | n/a         |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2393        | 2157        | -236        |
| cpu/compactPresentationContract/reuses                  | 2371        | 2135        | -236        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 71          | 71          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 157         | 154         | -3          |
| cpu/generationResourceValidation/fullValidations        | 578         | 575         | -3          |
| cpu/generationResourceValidation/stableChecks           | 9007        | 8207        | -800        |
| cpu/generationResourceValidation/stableHits             | 8928        | 8126        | -802        |
| cpu/generationResourceValidation/stableMisses           | 79          | 81          | 2           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 3           | 2           | -1          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4641        | 4164        | -477        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4641        | 4164        | -477        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4599        | 4122        | -477        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4641        | 4164        | -477        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4620        | 4143        | -477        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 35          | 35          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4606        | 4129        | -477        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4547        | 4070        | -477        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4626        | 4149        | -477        |
| cpu/strongStereoPacket/captures                         | 4970        | 4501        | -469        |
| cpu/strongStereoPacket/commitAccepts                    | 4768        | 4296        | -472        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 64          | 0           |
| cpu/strongStereoPacket/commitValidations                | 4832        | 4360        | -472        |
| cpu/strongStereoPacket/cycleReuses                      | 2445        | 2209        | -236        |
| cpu/strongStereoPacket/fastSkips                        | 4312        | 3827        | -485        |
| cpu/strongStereoPacket/invalidations                    | 5212        | 4726        | -486        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 104         | 1           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2422        | 2188        | -234        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.817       | 1.929       | 0.112       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 20.300      | 97.900      | 77.600      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.141       | 0.137       | -0.003      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 20.300      | 8.900       | -11.400     |
| cpu/window/currentFrame                                 | 132889      | 131740      | -1149       |
| cpu/window/elapsedFrames                                | 4641        | 4164        | -477        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 128248      | 127576      | -672        |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 132890      | 131740      | -1150       |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6700448304  | 5964553584  | -735894720  |
| gpu/item10PeripheryTAAHistory/dispatches                | 5281        | 4701        | -580        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 13414584960 | 11941292160 | -1473292800 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.303       | -0.002      |
| gpu/item5ActiveFSRCopies/activePixels                   | 13157283320 | 11725575560 | -1431707760 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 29873027080 | 26910258040 | -2962769040 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 16940       | 15210       | -1730       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2243        | 2017        | -226        |
| gpu/item7EarlyHAM/executedClears                        | 2176        | 1918        | -258        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2176        | 1918        | -258        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4580        | 4096        | -484        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4642        | 4164        | -478        |
| gpu/startFrame                                          | 128248      | 127576      | -672        |
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
| texture/createdCount                                    | 3896        | 3910        | 14          |
| texture/createdEstimatedBytes                           | 38604943312 | 38650023448 | 45080136    |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3689        | 3703        | 14          |
| texture/destroyedEstimatedBytes                         | 36229291932 | 36274372068 | 45080136    |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 860         | 867         | 7           |
| texture/liveTextureRecordCount                          | 207         | 207         | 0           |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 0           | 0           | 0           |
| texture/niSourceTextureMatchedEstimatedBytes            | 0           | 0           | 0           |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1474        | 1499        | 25          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 207         | 207         | 0           |
| texture/outstandingEstimatedBytes                       | 2375651380  | 2375651380  | 0           |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 3           | 2           | -1          |
| texture/supported                                       | true        | true        | n/a         |

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
