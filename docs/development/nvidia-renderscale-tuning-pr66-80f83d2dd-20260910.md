# NVIDIA render-scale tuning: PR66 source 80f83d2dd, 10 September 2026

The NVIDIA assay completed both 33-transition passes in one Skyrim VR
process. All 66 terminal render checks passed; Task 2 counts are
66 PASS / 0 FAIL / 0 INCONCLUSIVE. Evidence and retry reporting are
COMPLETE. Recovered fidelity and vendor-fallback findings remain, so
the improvement-or-neutral assessment is **DOES_NOT_MEET_STANDARD**.

Mean strict completion was 871.699 / 809.922 ms in passes 1/2.
Changes from the pinned main-VR reference are +0.657% / -2.780%.
The direction does not repeat. Scene time and toolchain fingerprints
differ, and no versioned tolerance policy was supplied. These descriptive
transition deltas do not establish a runtime performance improvement.

## Complete ledger coverage

The same run column now contains 211 additional structured detail rows.
They retain all 66 complete transition records and every field of both
pass health records, including retries, recoveries, counters, raw gate
results and applicability, memory, resources, profiler evidence and gaps.
All 66 transition comparisons and both pass comparisons, provenance,
context differences and assessment limitations are stored in the ledger.

Decoding these cells reconstructs the entire finalized summary exactly;
false, zero, null and empty values are preserved. The supplement preserved
every existing cell and column. The comparison was reused after its hashes
matched, and all 1,056 paired numeric timing cells were audited again.
The supplement took 2.544 seconds, including the 1.946-second reporting
workflow. Historical runs' new detail rows are explicitly unpopulated;
this coverage result applies to the current run.

[Complete ledger coverage audit](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/ledger-detail-supplement/validation.json).

## Identity and comparison scope

Run `nvidia-2026-09-10T13-36-15-291Z`; game PID `40884`.
Compiled source and renderer base: `80f83d2ddad9a5c3c5274cfe5454b3c9b2af287c`.
Main-VR base: `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`.
Build ID: `3a3898d47e65a1b692aaec78e7af2d4fa5f1511ec229ab0b7e7d42c670196417`; clean Release, DevBench enabled.

The reference is the previous relevant measured main-VR run
`nvidia-20260910T124329625Z`, source `7c8e3e656`.
Git confirms that base is an ancestor of the compiled candidate. The
candidate contains three PR66 commits and no reporting bridge backport.

Physical DLL: 28,065,792 bytes, SHA-256
`1b968c66f7f962b594744425137886db7012171e9a50d09807f4119a9c6d60c5`. Its adjacent manifest, AIO package receipt
and captured runtime producer agree. The archive hash and size also
match the receipt. Exactly one enabled loose DLL provider was found;
Overwrite and unmanaged Data have no competing CommunityShaders DLL.
[Physical AIO verification and compile identity](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/raw/offline/physical-aio-verification.json).

Both runs use the RTX 5070 Ti Laptop GPU, Dragonsreach, DLSS profile K,
explicit FSR3, 1512 x 1680 output per eye, and foveation 0.3/0.3/0.7.
Adapter, foveation, dependencies and shader compiler match. Position
and weather match; retained game hours are
9.532 / 9.717 (reference/candidate).
The toolchain mismatch is the vcpkg toolchain-file SHA-256; the retained
compiler hash/version, SDK and generator match. Exact hashes are retained
in both manifests and the comparison JSON. Driver version, headset
refresh/power state, and complete modlist/cache equivalence are unproven.

The run used one positioning COC, two runtime-only baselines, 66 public
API transitions with five-second server pacing, and one ten-second
inter-pass cooldown. No recovery apply was required.

## Per-pass latency and failure counts

| Metric                                  |    Pass 1 |    Pass 2 |
| --------------------------------------- | --------: | --------: |
| Strict mean (ms)                        |   871.699 |   809.922 |
| Strict median (ms)                      |   835.988 |   824.432 |
| Strict p95 (ms)                         |  1630.789 |  1268.807 |
| Strict maximum (ms)                     |  1831.879 |  1615.870 |
| Strict total (ms)                       | 28766.076 | 26727.441 |
| Presentation mean (ms)                  |   743.209 |   666.426 |
| Cleanup-tail mean (ms)                  |   103.997 |   116.061 |
| Cleanup-tail maximum (ms)               |   282.738 |   341.483 |
| Device-loss failures                    |         0 |         0 |
| OOM failures                            |         0 |         0 |
| Producer terminal failures              |         0 |         0 |
| Fidelity mismatch observations          |         4 |         4 |
| Vendor-failure stretch eye observations |         2 |         2 |
| Vendor-native qualification failures    |         0 |         0 |
| Credible liveness timeouts              |         0 |         0 |

Pass 1: 17 routes slower and 16 faster than the reference.
Largest increase: row 25, FSR3 Native AA -> DLSS Hoshipa, +624.485 ms.

Pass 2: 19 routes slower and 14 faster than the reference.
Largest increase: row 25, FSR3 Native AA -> DLSS Hoshipa, +234.551 ms.

Rows 26 (DLSS Hoshipa to FSR3 Hoshipa) and 28 (None to FSR3 Ultra
Performance) each recorded two fidelity mismatches and one vendor-failure
stretch eye observation per pass before recovering to terminal PASS.
The reference has the same affected routes and counts; no new failure
route was observed. All five terminal failure categories above are zero.

Raw cumulative fidelity and presentation-fallback gates remain unmet.
The fixed stretch cutoff is DIAGNOSTIC_ONLY because settling imposes
stretch. The scaled-presentation gate after proven native output is a
CONTRACT_MISMATCH. These interpretations preserve the raw producer gates.

## Retry detail and timing limits

Owned stress retries total 11 / 6; the reference has 10 / 10.
All 66 candidate rows have complete schema-v1 retry diagnostics. Both
runs retain detailed reasons, viewport waits and guard/promotion intervals.
Overlapping full-eye and center waits are not summed. Ready-to-candidate
also includes settling/stereo qualification, not isolated retry overhead.
Unavailable or inapplicable intervals remain n.d. Both candidate passes
lack resolved profiler timing samples; zero totals are unavailable GPU
cost evidence. CPU/GPU counters remain in the detailed comparison.

## Same-process repeat by transition group

Times are milliseconds. Retry cells list status, count, reasons, observed
per-role waits and ready-to-candidate time immediately after identity.
Timing tuples are strict / presentation / cleanup / cleanup tail.
Provider crossings also appear in destination groups; do not sum views.

### DLSS and DLAA destinations

| Row | Route                                            | P1 retry diagnostics                                                                                             | P2 retry diagnostics                                                                                             | P1 render / Task 2 | P1 timing tuple                          | P2 render / Task 2 | P2 timing tuple                          | Recovered findings |
| --- | ------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ------------------ |
| 3   | TAA -> DLAA                                      | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | PASS / PASS        | 250.637 / 250.637 / 250.637 / 0.000      | PASS / PASS        | 278.698 / 278.698 / 278.698 / 0.000      | none observed      |
| 4   | DLAA -> DLSS Hoshipa                             | complete; 0; none; none observed; ready-to-candidate 113.225                                                     | complete; 0; none; none observed; ready-to-candidate 145.681                                                     | PASS / PASS        | 987.322 / 771.492 / 902.207 / 130.715    | PASS / PASS        | 1122.734 / 878.213 / 1019.028 / 140.815  | none observed      |
| 5   | DLSS Hoshipa -> DLSS Ultra Quality               | complete; 0; none; none observed; ready-to-candidate 105.569                                                     | complete; 0; none; none observed; ready-to-candidate 126.781                                                     | PASS / PASS        | 1272.445 / 1033.769 / 1181.184 / 147.415 | PASS / PASS        | 1107.595 / 921.790 / 1017.211 / 95.421   | none observed      |
| 6   | DLSS Ultra Quality -> DLSS Quality               | complete; 1; dlss_viewport_recycle; FullEye 49.888; SubmitStageFoveatedCenter 49.813; ready-to-candidate 225.341 | complete; 1; dlss_viewport_recycle; FullEye 64.867; SubmitStageFoveatedCenter 64.790; ready-to-candidate 237.367 | PASS / PASS        | 1120.965 / 907.649 / 1038.381 / 130.733  | PASS / PASS        | 1230.876 / 989.108 / 1136.356 / 147.248  | none observed      |
| 7   | DLSS Quality -> DLSS Balanced                    | complete; 1; dlss_viewport_recycle; FullEye 55.365; SubmitStageFoveatedCenter 55.353; ready-to-candidate 221.734 | complete; 1; dlss_viewport_recycle; FullEye 64.366; SubmitStageFoveatedCenter 64.321; ready-to-candidate 265.212 | PASS / PASS        | 1342.139 / 1129.226 / 1259.246 / 130.020 | PASS / PASS        | 1277.435 / 1043.352 / 1187.033 / 143.681 | none observed      |
| 8   | DLSS Balanced -> DLSS Performance                | complete; 1; dlss_viewport_recycle; FullEye 54.886; SubmitStageFoveatedCenter 54.876; ready-to-candidate 223.251 | complete; 1; dlss_viewport_recycle; FullEye 61.524; SubmitStageFoveatedCenter 61.464; ready-to-candidate 252.319 | PASS / PASS        | 1384.985 / 1209.061 / 1299.049 / 89.987  | PASS / PASS        | 1193.610 / 969.401 / 1107.534 / 138.133  | none observed      |
| 9   | DLSS Performance -> DLSS Ultra Performance       | complete; 1; dlss_viewport_recycle; FullEye 55.963; SubmitStageFoveatedCenter 55.892; ready-to-candidate 220.731 | complete; 1; dlss_viewport_recycle; FullEye 2.095; SubmitStageFoveatedCenter 2.023; ready-to-candidate 297.419   | PASS / PASS        | 1310.241 / 1070.238 / 1217.579 / 147.341 | PASS / PASS        | 1246.341 / 1009.518 / 1150.578 / 141.060 | none observed      |
| 10  | DLSS Ultra Performance -> DLAA                   | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | PASS / PASS        | 840.730 / 645.302 / 840.730 / 195.429    | PASS / PASS        | 891.766 / 683.840 / 891.766 / 207.926    | none observed      |
| 23  | NONE -> DLAA                                     | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | PASS / PASS        | 315.845 / 315.845 / 315.845 / 0.000      | PASS / PASS        | 318.899 / 318.899 / 318.899 / 0.000      | none observed      |
| 25  | FSR3 Native AA -> DLSS Hoshipa                   | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate 298.288                           | complete; 0; none; none observed; ready-to-candidate 133.406                                                     | PASS / PASS        | 1601.183 / 1342.657 / 1503.941 / 161.284 | PASS / PASS        | 1263.055 / 985.077 / 1155.156 / 170.079  | none observed      |
| 29  | FSR3 Ultra Performance -> DLSS Ultra Performance | complete; 2; render_target_relatch_requeued; none observed; ready-to-candidate 267.917                           | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate 308.012                           | PASS / PASS        | 1831.879 / 1620.772 / 1751.752 / 130.980 | PASS / PASS        | 1615.870 / 1379.208 / 1527.516 / 148.307 | none observed      |
| 33  | NONE -> DLAA                                     | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | complete; 0; none; none observed; ready-to-candidate n.d.                                                        | PASS / PASS        | 260.297 / 260.297 / 260.297 / 0.000      | PASS / PASS        | 309.080 / 309.080 / 309.080 / 0.000      | none observed      |

### FSR3 destinations

| Row | Route                                      | P1 retry diagnostics                                                                | P2 retry diagnostics                                                                | P1 render / Task 2 | P1 timing tuple                          | P2 render / Task 2 | P2 timing tuple                         | Recovered findings             |
| --- | ------------------------------------------ | ----------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ------------------ | ---------------------------------------- | ------------------ | --------------------------------------- | ------------------------------ |
| 13  | NONE -> FSR3 Native AA                     | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 803.566 / 803.566 / 803.566 / 0.000      | PASS / PASS        | 645.000 / 645.000 / 645.000 / 0.000     | none observed                  |
| 14  | FSR3 Native AA -> FSR3 Hoshipa             | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 1675.199 / 1495.806 / 1610.378 / 114.573 | PASS / PASS        | 1028.630 / 837.473 / 978.195 / 140.722  | none observed                  |
| 15  | FSR3 Hoshipa -> FSR3 Ultra Quality         | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 835.988 / 835.988 / 696.409 / 0.000      | PASS / PASS        | 898.082 / 898.082 / 655.861 / 0.000     | none observed                  |
| 16  | FSR3 Ultra Quality -> FSR3 Quality         | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 795.593 / 795.593 / 603.429 / 0.000      | PASS / PASS        | 763.098 / 763.098 / 670.344 / 0.000     | none observed                  |
| 17  | FSR3 Quality -> FSR3 Balanced              | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 824.891 / 824.891 / 666.818 / 0.000      | PASS / PASS        | 746.341 / 746.341 / 606.583 / 0.000     | none observed                  |
| 18  | FSR3 Balanced -> FSR3 Performance          | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 858.227 / 858.227 / 710.462 / 0.000      | PASS / PASS        | 774.013 / 774.013 / 589.539 / 0.000     | none observed                  |
| 19  | FSR3 Performance -> FSR3 Ultra Performance | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 911.733 / 911.733 / 770.756 / 0.000      | PASS / PASS        | 758.939 / 758.939 / 625.553 / 0.000     | none observed                  |
| 20  | FSR3 Ultra Performance -> FSR3 Native AA   | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d. | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d. | PASS / PASS        | 1165.675 / 909.017 / 1165.675 / 256.659  | PASS / PASS        | 1099.831 / 883.470 / 1099.831 / 216.361 | none observed                  |
| 24  | DLAA -> FSR3 Native AA                     | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 1131.334 / 938.922 / 1131.334 / 192.411  | PASS / PASS        | 863.544 / 660.640 / 863.544 / 202.904   | none observed                  |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa               | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 1409.403 / 1317.015 / 1363.769 / 46.753  | PASS / PASS        | 1024.143 / 866.625 / 974.481 / 107.856  | both passes; also in reference |
| 28  | NONE -> FSR3 Ultra Performance             | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 942.343 / 942.343 / 891.403 / 0.000      | PASS / PASS        | 990.633 / 801.738 / 942.457 / 140.720   | both passes; also in reference |
| 31  | TAA -> FSR3 Native AA                      | complete; 0; none; none observed; ready-to-candidate n.d.                           | complete; 0; none; none observed; ready-to-candidate n.d.                           | PASS / PASS        | 639.541 / 639.541 / 639.541 / 0.000      | PASS / PASS        | 661.757 / 661.757 / 661.757 / 0.000     | none observed                  |

### Provider crossings

| Row | Route                                            | P1 retry diagnostics                                                                   | P2 retry diagnostics                                                                   | P1 render / Task 2 | P1 timing tuple                          | P2 render / Task 2 | P2 timing tuple                          | Recovered findings             |
| --- | ------------------------------------------------ | -------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- | ------------------ | ---------------------------------------- | ------------------ | ---------------------------------------- | ------------------------------ |
| 24  | DLAA -> FSR3 Native AA                           | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d.    | complete; 0; none; none observed; ready-to-candidate n.d.                              | PASS / PASS        | 1131.334 / 938.922 / 1131.334 / 192.411  | PASS / PASS        | 863.544 / 660.640 / 863.544 / 202.904    | none observed                  |
| 25  | FSR3 Native AA -> DLSS Hoshipa                   | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate 298.288 | complete; 0; none; none observed; ready-to-candidate 133.406                           | PASS / PASS        | 1601.183 / 1342.657 / 1503.941 / 161.284 | PASS / PASS        | 1263.055 / 985.077 / 1155.156 / 170.079  | none observed                  |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa                     | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate n.d.    | complete; 0; none; none observed; ready-to-candidate n.d.                              | PASS / PASS        | 1409.403 / 1317.015 / 1363.769 / 46.753  | PASS / PASS        | 1024.143 / 866.625 / 974.481 / 107.856   | both passes; also in reference |
| 29  | FSR3 Ultra Performance -> DLSS Ultra Performance | complete; 2; render_target_relatch_requeued; none observed; ready-to-candidate 267.917 | complete; 1; render_target_relatch_requeued; none observed; ready-to-candidate 308.012 | PASS / PASS        | 1831.879 / 1620.772 / 1751.752 / 130.980 | PASS / PASS        | 1615.870 / 1379.208 / 1527.516 / 148.307 | none observed                  |

### TAA and None boundary routes

| Row | Route                          | P1 retry diagnostics                                      | P2 retry diagnostics                                      | P1 render / Task 2 | P1 timing tuple                       | P2 render / Task 2 | P2 timing tuple                       | Recovered findings             |
| --- | ------------------------------ | --------------------------------------------------------- | --------------------------------------------------------- | ------------------ | ------------------------------------- | ------------------ | ------------------------------------- | ------------------------------ |
| 1   | DLSS Hoshipa -> NONE           | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 709.577 / 487.396 / 709.577 / 222.181 | PASS / PASS        | 826.841 / 629.980 / 826.841 / 196.861 | none observed                  |
| 2   | NONE -> TAA                    | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 169.520 / 169.520 / 169.520 / 0.000   | PASS / PASS        | 185.004 / 185.004 / 185.004 / 0.000   | none observed                  |
| 3   | TAA -> DLAA                    | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 250.637 / 250.637 / 250.637 / 0.000   | PASS / PASS        | 278.698 / 278.698 / 278.698 / 0.000   | none observed                  |
| 11  | DLAA -> TAA                    | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 483.915 / 202.036 / 483.915 / 281.880 | PASS / PASS        | 526.423 / 184.940 / 526.423 / 341.483 | none observed                  |
| 12  | TAA -> NONE                    | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 172.530 / 172.530 / 172.530 / 0.000   | PASS / PASS        | 177.907 / 177.907 / 177.907 / 0.000   | none observed                  |
| 13  | NONE -> FSR3 Native AA         | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 803.566 / 803.566 / 803.566 / 0.000   | PASS / PASS        | 645.000 / 645.000 / 645.000 / 0.000   | none observed                  |
| 21  | FSR3 Native AA -> TAA          | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 514.630 / 231.891 / 514.630 / 282.738 | PASS / PASS        | 546.920 / 235.492 / 546.920 / 311.428 | none observed                  |
| 22  | TAA -> NONE                    | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 213.438 / 213.438 / 213.438 / 0.000   | PASS / PASS        | 188.521 / 188.521 / 188.521 / 0.000   | none observed                  |
| 23  | NONE -> DLAA                   | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 315.845 / 315.845 / 315.845 / 0.000   | PASS / PASS        | 318.899 / 318.899 / 318.899 / 0.000   | none observed                  |
| 27  | FSR3 Hoshipa -> NONE           | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 771.969 / 509.729 / 771.969 / 262.240 | PASS / PASS        | 824.432 / 565.339 / 824.432 / 259.093 | none observed                  |
| 28  | NONE -> FSR3 Ultra Performance | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 942.343 / 942.343 / 891.403 / 0.000   | PASS / PASS        | 990.633 / 801.738 / 942.457 / 140.720 | both passes; also in reference |
| 30  | DLSS Ultra Performance -> TAA  | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 744.439 / 513.574 / 744.439 / 230.865 | PASS / PASS        | 807.863 / 529.216 / 807.863 / 278.647 | none observed                  |
| 31  | TAA -> FSR3 Native AA          | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 639.541 / 639.541 / 639.541 / 0.000   | PASS / PASS        | 661.757 / 661.757 / 661.757 / 0.000   | none observed                  |
| 32  | FSR3 Native AA -> NONE         | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 473.896 / 196.196 / 473.896 / 277.700 | PASS / PASS        | 533.560 / 232.288 / 533.560 / 301.272 | none observed                  |
| 33  | NONE -> DLAA                   | complete; 0; none; none observed; ready-to-candidate n.d. | complete; 0; none; none observed; ready-to-candidate n.d. | PASS / PASS        | 260.297 / 260.297 / 260.297 / 0.000   | PASS / PASS        | 309.080 / 309.080 / 309.080 / 0.000   | none observed                  |

## Candidate health gates, relatch/stretch and memory

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 871.699        | 1630.789 | 1831.879 | 15.485             | 826.449         | 14.040              | 20               | 88             | 5733.872   | 11      | 4        | 2                   | NOT_MET         | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 809.922        | 1268.807 | 1615.870 | 14.455             | 726.564         | 12.440              | 17               | 65             | 4581.027   | 6       | 4        | 2                   | NOT_MET         | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":88,"allowedPresentationStretchEpisodes":20,"allowedPresentationStretchEyeObservations":191,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":15638,"leftPath":"NativeOriginal","referenceFrame":16026,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations                                | Receipt                                             |
| --- | --------- | ----------------------------------------------------------- | --------------------------------------------------- |
| 26  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-1/transitions/26/retained.json |
| 28  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-1/transitions/28/retained.json |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":65,"allowedPresentationStretchEpisodes":17,"allowedPresentationStretchEyeObservations":143,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":19968,"leftPath":"NativeOriginal","referenceFrame":20338,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations                                | Receipt                                             |
| --- | --------- | ----------------------------------------------------------- | --------------------------------------------------- |
| 26  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-2/transitions/26/retained.json |
| 28  | true      | fidelityMismatches=2; vendorFailureStretchEyeObservations=1 | raw/lane-nvidia/pass-2/transitions/28/retained.json |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |        16893 |  16615.199 |     -277.801 |      16954.574 |    16956.547 |          1.973 |    16991.145 |  16646.555 |      -344.59 |                   n.d. |
| System commit MiB                  |    55468.461 |  54664.203 |     -804.258 |      54995.164 |    54901.699 |        -93.465 |    54965.676 |  54796.973 |     -168.703 |                   n.d. |
| DXGI process usage MiB             |     6028.246 |   3819.688 |    -2208.559 |        4077.25 |      4077.25 |              0 |     4136.426 |   3616.016 |      -520.41 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        232 |          232 |            232 |          232 |              0 |            0 |        210 |          210 |                  0.905 |
| Estimated live tracked texture MiB |            0 |   2325.432 |     2325.432 |       2325.432 |     2325.432 |              0 |            0 |   2267.056 |     2267.056 |                  0.975 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -277.80078125,
        "systemCommitMiB": -804.2578125,
        "dxgiUsageMiB": -2208.55859375,
        "liveTextures": 232,
        "liveTextureMiB": 2325.431926727295
    },
    "pass2": {
        "processPrivateMiB": -344.58984375,
        "systemCommitMiB": -168.703125,
        "dxgiUsageMiB": -520.41015625,
        "liveTextures": 210,
        "liveTextureMiB": 2267.056079864502
    },
    "positivePass1PrivateAndCommit": false,
    "pass2ResourceGrowth": true,
    "retentionPredicate": false,
    "initializationPredicate": false
}
```

Conclusion: classification_is_not_proof_for_or_against_a_leak. Growth is relative to the freshly reset tracker in each pass; session IDs are retained.

Evidence gaps: none.

Memory classification is inconclusive. Process-private and system-commit
growth are negative in both passes, so positive-pass-1 growth ratios are
unavailable. Fresh tracker counts and Normal pressure do not establish
a leak or leak freedom.

## Evidence and validation

-   [Canonical ledger](vr-render-scale-ledger.md): 528 new numeric timing cells plus all 66 full timing/retry records, with source-to-destination routes, run/build identities, units and timing origins.
-   The maintained comparison wrapper verified 1,056 paired timing cells and preserved every historical cell. The complete embedded timing/retry records were also checked against the candidate summary after the append.
-   [Summary](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/summary.json), [transition CSV](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/transitions.csv), [receipt index](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/receipt-index.json) and [full scalar export](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/evidence-values.csv): 442 journal records, 182 raw JSON files and 5,984,310 scalar/null/empty values. All 24 required DLSS trace windows are complete.
-   [Worker status](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/worker-status.json): COMPLETE; captures verified inactive; evidencePending=0. Maximum client dispatch gap 40.059 ms against the 250 ms diagnostic limit.
-   [Exact finalization command](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/finalization-command.json), [comparison command](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z/comparison-command.json), [ledger audit](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z-comparison/ledger-validation.json), [comparison JSON](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z-comparison/comparison.json) and [comparison CSV](../../artifacts/renderscale-tuning/nvidia-2026-09-10T13-36-15-291Z-comparison/comparison.csv).
-   Finalization ran once in 14.588 s; deployment/ledger preparation took 1.328 s. Comparison ran once in 3.476 s; generation 3047.613 ms, ledger audit/update 109.560 ms. Stage receipts are preserved locally.
-   Focused whitespace and complete embedded-ledger validation are retained in completion-validation.json. Build and code regression tests were not run for this evidence-only update.

Raw evidence remains local. PR inclusion is the user's decision.

## Detailed baseline comparison

Change assessment: **DOES_NOT_MEET_STANDARD**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                    |
| ----------------------- | --------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T124329625Z                                                              | nvidia-2026-09-10T13-36-15-291Z                                                              |
| Renderer base           | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | 80f83d2ddad9a5c3c5274cfe5454b3c9b2af287c                                                     |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                     |
| Compiled source         | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | 80f83d2ddad9a5c3c5274cfe5454b3c9b2af287c                                                     |
| Build ID                | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        | 3a3898d47e65a1b692aaec78e7af2d4fa5f1511ec229ab0b7e7d42c670196417                             |
| DLL SHA-256             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        | 1b968c66f7f962b594744425137886db7012171e9a50d09807f4119a9c6d60c5                             |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-2026-09-10T13-36-15-291Z |

Assessment limits: candidate_health_standard_not_met; retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 866.009/871.699 | 0.657        | 10/11       | 4/4          | 2/2                 | none             | NOT_MET/NOT_MET     |
| nvidia | 2    | 33/33    | 833.078/809.922 | -2.780       | 10/6        | 4/4          | 2/2                 | none             | NOT_MET/NOT_MET     |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 807.337   | 826.449   | 19.112   | 2.367   |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920    | 14.040    | 0.120    | 0.862   |
| nvidia | 1    | Relatch proof total        | ms          | 20183.415 | 20661.219 | 477.804  | 2.367   |
| nvidia | 1    | Relatch proof total        | frames      | 348       | 351       | 3        | 0.862   |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 866.009   | 871.699   | 5.691    | 0.657   |
| nvidia | 1    | Strict completion mean     | frames      | 15.576    | 15.485    | -0.091   | -0.584  |
| nvidia | 1    | Strict completion total    | ms          | 28578.281 | 28766.076 | 187.796  | 0.657   |
| nvidia | 1    | Strict completion total    | frames      | 514       | 511       | -3       | -0.584  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 20        | 1        | 5.263   |
| nvidia | 1    | Stretch completed total    | frames      | 89        | 88        | -1       | -1.124  |
| nvidia | 1    | Stretch completed total    | ms          | 5719.962  | 5733.872  | 13.910   | 0.243   |
| nvidia | 1    | Stretch longest episode    | ms          | 646.277   | 407.547   | -238.730 | -36.939 |
| nvidia | 2    | Relatch proof mean         | ms          | 763.765   | 726.564   | -37.202  | -4.871  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.840    | 12.440    | -1.400   | -10.116 |
| nvidia | 2    | Relatch proof total        | ms          | 19094.136 | 18164.092 | -930.044 | -4.871  |
| nvidia | 2    | Relatch proof total        | frames      | 346       | 311       | -35      | -10.116 |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 833.078   | 809.922   | -23.156  | -2.780  |
| nvidia | 2    | Strict completion mean     | frames      | 15.455    | 14.455    | -1       | -6.471  |
| nvidia | 2    | Strict completion total    | ms          | 27491.579 | 26727.441 | -764.138 | -2.780  |
| nvidia | 2    | Strict completion total    | frames      | 510       | 477       | -33      | -6.471  |
| nvidia | 2    | Stretch completed episodes | episodes    | 19        | 17        | -2       | -10.526 |
| nvidia | 2    | Stretch completed total    | frames      | 83        | 65        | -18      | -21.687 |
| nvidia | 2    | Stretch completed total    | ms          | 5383.107  | 4581.027  | -802.080 | -14.900 |
| nvidia | 2    | Stretch longest episode    | ms          | 441.441   | 438.791   | -2.651   | -0.600  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 688.545 / 709.577   | 21.032   | 3.055   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.425 / 169.520   | 0.095    | 0.056   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 249.126 / 250.637   | 1.511    | 0.606   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.374 / 987.322  | -17.052  | -1.698  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.234 / 1272.445 | 6.211    | 0.491   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.702 / 1120.965 | -54.736  | -4.656  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.400 / 1342.139 | -51.261  | -3.679  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.171 / 1384.985 | 32.814   | 2.427   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.511 / 1310.241 | -76.270  | -5.501  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 770.294 / 840.730   | 70.436   | 9.144   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 450.117 / 483.915   | 33.799   | 7.509   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 190.626 / 172.530   | -18.096  | -9.493  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 822.942 / 803.566   | -19.377  | -2.355  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.140 / 1675.199 | 298.059  | 21.643  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.789 / 835.988   | 86.199   | 11.496  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 929.137 / 795.593   | -133.544 | -14.373 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 762.921 / 824.891   | 61.970   | 8.123   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.519 / 858.227  | -542.292 | -38.721 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 778.498 / 911.733   | 133.236  | 17.114  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 731.796 / 1165.675  | 433.880  | 59.290  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 572.501 / 514.630   | -57.871  | -10.108 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 172.732 / 213.438   | 40.706   | 23.566  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 261.855 / 315.845   | 53.990   | 20.618  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1233.573 / 1131.334 | -102.239 | -8.288  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.698 / 1601.183  | 624.485  | 63.938  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.005 / 1409.403 | -126.602 | -8.242  | 1/1         | 2/1 -> 2/1 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.032 / 771.969   | -161.063 | -17.262 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 924.794 / 942.343   | 17.549   | 1.898   | 0/0         | 2/1 -> 2/1 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.558 / 1831.879 | -352.680 | -16.144 | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 744.487 / 744.439   | -0.048   | -0.006  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 610.701 / 639.541   | 28.840   | 4.722   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 481.567 / 473.896   | -7.671   | -1.593  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 296.510 / 260.297   | -36.212  | -12.213 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 466.266 / 487.396   | 688.545 / 709.577   | 222.279 / 222.181 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4904,"dispatchToBlockedOrPreparationMs":363.77,"firstNewGenerationToCleanupDrainedMs":222.8026,"firstPhysicalMutationToFirstNewGenerationMs":119.514,"presentationToStrictCompletionMs":222.181}       |
| 2   | 169.425 / 169.520   | 169.425 / 169.520   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.5197,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 249.126 / 250.637   | 249.126 / 250.637   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.6366,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 790.942 / 771.492   | 920.929 / 902.207   | 129.987 / 130.715 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7427,"dispatchToBlockedOrPreparationMs":395.4643,"firstNewGenerationToCleanupDrainedMs":174.9654,"firstPhysicalMutationToFirstNewGenerationMs":328.0342,"presentationToStrictCompletionMs":215.8301}   |
| 5   | 1014.085 / 1033.769 | 1168.706 / 1181.184 | 154.620 / 147.415 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2253,"dispatchToBlockedOrPreparationMs":419.2544,"firstNewGenerationToCleanupDrainedMs":196.7924,"firstPhysicalMutationToFirstNewGenerationMs":560.9122,"presentationToStrictCompletionMs":238.6759}   |
| 6   | 947.755 / 907.649   | 1089.396 / 1038.381 | 141.641 / 130.733 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.026,"dispatchToBlockedOrPreparationMs":366.5709,"firstNewGenerationToCleanupDrainedMs":174.402,"firstPhysicalMutationToFirstNewGenerationMs":493.3826,"presentationToStrictCompletionMs":213.3166}     |
| 7   | 1160.289 / 1129.226 | 1307.700 / 1259.246 | 147.411 / 130.020 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1856,"dispatchToBlockedOrPreparationMs":394.9773,"firstNewGenerationToCleanupDrainedMs":172.2281,"firstPhysicalMutationToFirstNewGenerationMs":687.8546,"presentationToStrictCompletionMs":212.9138}   |
| 8   | 1122.704 / 1209.061 | 1271.880 / 1299.049 | 149.177 / 89.987  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2983,"dispatchToBlockedOrPreparationMs":396.8224,"firstNewGenerationToCleanupDrainedMs":181.7192,"firstPhysicalMutationToFirstNewGenerationMs":715.2088,"presentationToStrictCompletionMs":175.9232}   |
| 9   | 1153.443 / 1070.238 | 1299.332 / 1217.579 | 145.889 / 147.341 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5194,"dispatchToBlockedOrPreparationMs":373.8133,"firstNewGenerationToCleanupDrainedMs":193.956,"firstPhysicalMutationToFirstNewGenerationMs":645.2901,"presentationToStrictCompletionMs":240.0031}    |
| 10  | 592.366 / 645.302   | 770.294 / 840.730   | 177.929 / 195.429 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7601,"dispatchToBlockedOrPreparationMs":393.9134,"firstNewGenerationToCleanupDrainedMs":253.9007,"firstPhysicalMutationToFirstNewGenerationMs":189.1562,"presentationToStrictCompletionMs":195.4289}   |
| 11  | 171.418 / 202.036   | 450.117 / 483.915   | 278.699 / 281.880 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2702,"dispatchToBlockedOrPreparationMs":153.4956,"firstNewGenerationToCleanupDrainedMs":283.029,"firstPhysicalMutationToFirstNewGenerationMs":43.1206,"presentationToStrictCompletionMs":281.8796}     |
| 12  | 190.626 / 172.530   | 190.626 / 172.530   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.5296,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 822.942 / 803.566   | 822.942 / 803.566   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":197.5765,"dispatchToBlockedOrPreparationMs":418.7759,"firstNewGenerationToCleanupDrainedMs":44.509,"firstPhysicalMutationToFirstNewGenerationMs":142.7042,"presentationToStrictCompletionMs":0}          |
| 14  | 1240.733 / 1495.806 | 1329.442 / 1610.378 | 88.709 / 114.573  | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | {"blockedOrPreparationToFirstPhysicalMutationMs":325.3221,"dispatchToBlockedOrPreparationMs":436.9366,"firstNewGenerationToCleanupDrainedMs":234.5459,"firstPhysicalMutationToFirstNewGenerationMs":613.5738,"presentationToStrictCompletionMs":179.3932} |
| 15  | 749.789 / 835.988   | 570.716 / 696.409   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0166,"dispatchToBlockedOrPreparationMs":405.0376,"firstNewGenerationToCleanupDrainedMs":45.1193,"firstPhysicalMutationToFirstNewGenerationMs":241.2359,"presentationToStrictCompletionMs":0}           |
| 16  | 929.137 / 795.593   | 662.595 / 603.429   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.563,"dispatchToBlockedOrPreparationMs":383.5645,"firstNewGenerationToCleanupDrainedMs":0.1888,"firstPhysicalMutationToFirstNewGenerationMs":215.113,"presentationToStrictCompletionMs":0}              |
| 17  | 762.921 / 824.891   | 616.503 / 666.818   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5023,"dispatchToBlockedOrPreparationMs":385.7123,"firstNewGenerationToCleanupDrainedMs":47.7724,"firstPhysicalMutationToFirstNewGenerationMs":228.8311,"presentationToStrictCompletionMs":0}           |
| 18  | 1400.519 / 858.227  | 1120.851 / 710.462  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0489,"dispatchToBlockedOrPreparationMs":405.5786,"firstNewGenerationToCleanupDrainedMs":52.5583,"firstPhysicalMutationToFirstNewGenerationMs":247.2765,"presentationToStrictCompletionMs":0}           |
| 19  | 778.498 / 911.733   | 635.134 / 770.756   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.9682,"dispatchToBlockedOrPreparationMs":456.2299,"firstNewGenerationToCleanupDrainedMs":47.1509,"firstPhysicalMutationToFirstNewGenerationMs":261.4066,"presentationToStrictCompletionMs":0}           |
| 20  | 546.689 / 909.017   | 731.796 / 1165.675  | 185.107 / 256.659 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":295.8796,"dispatchToBlockedOrPreparationMs":471.328,"firstNewGenerationToCleanupDrainedMs":257.0427,"firstPhysicalMutationToFirstNewGenerationMs":141.4251,"presentationToStrictCompletionMs":256.6587}  |
| 21  | 254.898 / 231.891   | 572.501 / 514.630   | 317.603 / 282.738 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":150.9876,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":282.7385}             |
| 22  | 172.732 / 213.438   | 172.732 / 213.438   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":213.4381,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 261.855 / 315.845   | 261.855 / 315.845   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":315.8449,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 1042.196 / 938.922  | 1233.573 / 1131.334 | 191.377 / 192.411 | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | {"blockedOrPreparationToFirstPhysicalMutationMs":276.6239,"dispatchToBlockedOrPreparationMs":459.1265,"firstNewGenerationToCleanupDrainedMs":236.3274,"firstPhysicalMutationToFirstNewGenerationMs":159.256,"presentationToStrictCompletionMs":192.4114}  |
| 25  | 747.831 / 1342.657  | 895.346 / 1503.941  | 147.514 / 161.284 | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | {"blockedOrPreparationToFirstPhysicalMutationMs":30.2166,"dispatchToBlockedOrPreparationMs":742.1742,"firstNewGenerationToCleanupDrainedMs":209.0801,"firstPhysicalMutationToFirstNewGenerationMs":522.4699,"presentationToStrictCompletionMs":258.5257}  |
| 26  | 1384.499 / 1317.015 | 1484.709 / 1363.769 | 100.210 / 46.753  | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | {"blockedOrPreparationToFirstPhysicalMutationMs":286.8655,"dispatchToBlockedOrPreparationMs":417.1666,"firstNewGenerationToCleanupDrainedMs":172.4749,"firstPhysicalMutationToFirstNewGenerationMs":487.2617,"presentationToStrictCompletionMs":92.3875}  |
| 27  | 636.450 / 509.729   | 933.032 / 771.969   | 296.582 / 262.240 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0827,"dispatchToBlockedOrPreparationMs":357.3506,"firstNewGenerationToCleanupDrainedMs":263.0428,"firstPhysicalMutationToFirstNewGenerationMs":147.493,"presentationToStrictCompletionMs":262.2402}    |
| 28  | 748.294 / 942.343   | 879.078 / 891.403   | 130.784 / 0       | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8302,"dispatchToBlockedOrPreparationMs":412.3047,"firstNewGenerationToCleanupDrainedMs":180.8527,"firstPhysicalMutationToFirstNewGenerationMs":294.4158,"presentationToStrictCompletionMs":0}          |
| 29  | 1978.717 / 1620.772 | 2084.388 / 1751.752 | 105.670 / 130.980 | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | {"blockedOrPreparationToFirstPhysicalMutationMs":657.4566,"dispatchToBlockedOrPreparationMs":436.1041,"firstNewGenerationToCleanupDrainedMs":171.8075,"firstPhysicalMutationToFirstNewGenerationMs":486.3839,"presentationToStrictCompletionMs":211.1062} |
| 30  | 560.371 / 513.574   | 744.487 / 744.439   | 184.117 / 230.865 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6497,"dispatchToBlockedOrPreparationMs":398.9772,"firstNewGenerationToCleanupDrainedMs":231.3106,"firstPhysicalMutationToFirstNewGenerationMs":110.5016,"presentationToStrictCompletionMs":230.8651}   |
| 31  | 610.701 / 639.541   | 610.701 / 639.541   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":55.6497,"dispatchToBlockedOrPreparationMs":434.5558,"firstNewGenerationToCleanupDrainedMs":48.7133,"firstPhysicalMutationToFirstNewGenerationMs":100.6226,"presentationToStrictCompletionMs":0}          |
| 32  | 197.556 / 196.196   | 481.567 / 473.896   | 284.011 / 277.700 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":127.3361,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.7003}             |
| 33  | 296.510 / 260.297   | 296.510 / 260.297   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":260.2972,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 9                    | 0            | 465.818 / 486.774    | 20.956   | 14 / 14           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 4   | 12 / 13                  | 1            | 748.532 / 727.241    | -21.290  | 17 / 18           | 1            |
| 5   | 14 / 14                  | 0            | 960.719 / 984.392    | 23.673   | 19 / 19           | 0            |
| 6   | 17 / 17                  | 0            | 904.599 / 863.980    | -40.620  | 22 / 22           | 0            |
| 7   | 16 / 17                  | 1            | 1113.762 / 1087.017  | -26.745  | 21 / 22           | 1            |
| 8   | 17 / 16                  | -1           | 1079.314 / 1117.330  | 38.016   | 22 / 21           | -1           |
| 9   | 16 / 16                  | 0            | 1094.946 / 1023.623  | -71.323  | 21 / 21           | 0            |
| 10  | 9 / 9                    | 0            | 541.019 / 586.830    | 45.811   | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 169.912 / 200.886    | 30.974   | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 775.120 / 759.057    | -16.063  | 10 / 10           | 0            |
| 14  | 23 / 23                  | 0            | 1155.285 / 1375.832  | 220.547  | 28 / 28           | 0            |
| 15  | 12 / 12                  | 0            | 570.504 / 651.290    | 80.787   | 16 / 16           | 0            |
| 16  | 12 / 12                  | 0            | 611.667 / 603.240    | -8.427   | 18 / 16           | -2           |
| 17  | 12 / 12                  | 0            | 571.105 / 619.046    | 47.941   | 16 / 16           | 0            |
| 18  | 22 / 12                  | -10          | 1076.575 / 657.904   | -418.671 | 29 / 16           | -13          |
| 19  | 12 / 12                  | 0            | 584.498 / 723.605    | 139.107  | 16 / 16           | 0            |
| 20  | 10 / 16                  | 6            | 485.074 / 908.633    | 423.559  | 16 / 21           | 5            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 5             | 2            |
| 24  | 16 / 15                  | -1           | 992.563 / 895.006    | -97.557  | 21 / 20           | -1           |
| 25  | 13 / 23                  | 10           | 704.319 / 1294.861   | 590.542  | 18 / 28           | 10           |
| 26  | 24 / 22                  | -2           | 1288.337 / 1191.294  | -97.043  | 29 / 27           | -2           |
| 27  | 10 / 10                  | 0            | 635.729 / 508.926    | -126.803 | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 704.974 / 710.551    | 5.577    | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1879.015 / 1579.945  | -299.071 | 34 / 34           | 0            |
| 30  | 11 / 10                  | -1           | 506.461 / 513.129    | 6.668    | 16 / 16           | 0            |
| 31  | 9 / 9                    | 0            | 563.570 / 590.828    | 27.258   | 11 / 10           | -1           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 116.243 / 124.392  | 8.149    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.448 / 214.438  | -12.010  |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 225.499 / 228.617  | 3.118    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 378.865 / 386.493  | 7.628    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 379.410 / 381.290  | 1.880    |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 375.234 / 388.203  | 12.969   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 422.382 / 377.762  | -44.620  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 293.625 / 391.599  | 97.974   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.915 / 162.065  | 18.150   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 156.516 / 150.324  | -6.192   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 152.521 / 172.099  | 19.578   |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 646.277 / 186.410  | -459.867 |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 151.172 / 189.478  | 38.306   |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0 / 379.360        | 379.360  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 207.881 / 407.547  | 199.667  |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 629.135 / 607.985  | -21.150  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 1214.838 / 985.810 | -229.029 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 716.918 / 826.841   | 109.923  | 15.333  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 170.160 / 185.004   | 14.844   | 8.724   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.338 / 278.698   | -7.639   | -2.668  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1032.563 / 1122.734 | 90.171   | 8.733   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1027.220 / 1107.595 | 80.375   | 7.824   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1169.543 / 1230.876 | 61.332   | 5.244   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1390.699 / 1277.435 | -113.264 | -8.144  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.918 / 1193.610 | -2.308   | -0.193  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1249.407 / 1246.341 | -3.066   | -0.245  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 914.287 / 891.766   | -22.521  | -2.463  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 480.252 / 526.423   | 46.171   | 9.614   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.881 / 177.907   | 5.027    | 2.908   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 619.982 / 645.000   | 25.018   | 4.035   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1408.406 / 1028.630 | -379.777 | -26.965 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 945.154 / 898.082   | -47.072  | -4.980  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 789.230 / 763.098   | -26.133  | -3.311  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 831.971 / 746.341   | -85.631  | -10.293 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 757.557 / 774.013   | 16.456   | 2.172   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 758.886 / 758.939   | 0.053    | 0.007   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1053.683 / 1099.831 | 46.148   | 4.380   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 495.001 / 546.920   | 51.919   | 10.489  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 217.047 / 188.521   | -28.526  | -13.143 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 254.963 / 318.899   | 63.936   | 25.076  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1118.329 / 863.544  | -254.785 | -22.783 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1028.504 / 1263.055 | 234.551  | 22.805  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1514.576 / 1024.143 | -490.433 | -32.381 | 1/0         | 2/1 -> 2/1 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 774.192 / 824.432   | 50.240   | 6.489   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 939.204 / 990.633   | 51.429   | 5.476   | 0/0         | 2/1 -> 2/1 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1918.325 / 1615.870 | -302.454 | -15.767 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 827.143 / 807.863   | -19.281  | -2.331  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 628.123 / 661.757   | 33.634   | 5.355   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 531.443 / 533.560   | 2.117    | 0.398   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 273.671 / 309.080   | 35.409   | 12.939  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 485.143 / 629.980   | 716.918 / 826.841   | 231.775 / 196.861 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0339,"dispatchToBlockedOrPreparationMs":433.6904,"firstNewGenerationToCleanupDrainedMs":255.0829,"firstPhysicalMutationToFirstNewGenerationMs":133.0337,"presentationToStrictCompletionMs":196.861}    |
| 2   | 170.160 / 185.004   | 170.160 / 185.004   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":185.004,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 286.338 / 278.698   | 286.338 / 278.698   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":278.6984,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 804.481 / 878.213   | 948.804 / 1019.028  | 144.323 / 140.815 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2076,"dispatchToBlockedOrPreparationMs":424.0968,"firstNewGenerationToCleanupDrainedMs":188.1845,"firstPhysicalMutationToFirstNewGenerationMs":402.5392,"presentationToStrictCompletionMs":244.5214}   |
| 5   | 805.305 / 921.790   | 939.029 / 1017.211  | 133.724 / 95.421  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0352,"dispatchToBlockedOrPreparationMs":446.1315,"firstNewGenerationToCleanupDrainedMs":191.5467,"firstPhysicalMutationToFirstNewGenerationMs":375.4979,"presentationToStrictCompletionMs":185.8049}   |
| 6   | 953.781 / 989.108   | 1087.266 / 1136.356 | 133.485 / 147.248 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7137,"dispatchToBlockedOrPreparationMs":397.0191,"firstNewGenerationToCleanupDrainedMs":194.2289,"firstPhysicalMutationToFirstNewGenerationMs":541.3946,"presentationToStrictCompletionMs":241.7674}   |
| 7   | 1093.288 / 1043.352 | 1251.222 / 1187.033 | 157.935 / 143.681 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2077,"dispatchToBlockedOrPreparationMs":426.1038,"firstNewGenerationToCleanupDrainedMs":190.1182,"firstPhysicalMutationToFirstNewGenerationMs":565.6033,"presentationToStrictCompletionMs":234.0828}   |
| 8   | 968.840 / 969.401   | 1111.087 / 1107.534 | 142.246 / 138.133 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6162,"dispatchToBlockedOrPreparationMs":386.4999,"firstNewGenerationToCleanupDrainedMs":180.4689,"firstPhysicalMutationToFirstNewGenerationMs":536.9487,"presentationToStrictCompletionMs":224.2093}   |
| 9   | 971.186 / 1009.518  | 1146.171 / 1150.578 | 174.985 / 141.060 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1888,"dispatchToBlockedOrPreparationMs":422.676,"firstNewGenerationToCleanupDrainedMs":187.5612,"firstPhysicalMutationToFirstNewGenerationMs":536.1519,"presentationToStrictCompletionMs":236.8227}    |
| 10  | 692.329 / 683.840   | 914.287 / 891.766   | 221.958 / 207.926 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5155,"dispatchToBlockedOrPreparationMs":404.9297,"firstNewGenerationToCleanupDrainedMs":264.9025,"firstPhysicalMutationToFirstNewGenerationMs":217.4185,"presentationToStrictCompletionMs":207.9261}   |
| 11  | 200.500 / 184.940   | 480.252 / 526.423   | 279.753 / 341.483 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4375,"dispatchToBlockedOrPreparationMs":138.5049,"firstNewGenerationToCleanupDrainedMs":342.4309,"firstPhysicalMutationToFirstNewGenerationMs":41.05,"presentationToStrictCompletionMs":341.4834}      |
| 12  | 172.881 / 177.907   | 172.881 / 177.907   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.9075,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 619.982 / 645.000   | 619.982 / 645.000   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":57.89,"dispatchToBlockedOrPreparationMs":439.2468,"firstNewGenerationToCleanupDrainedMs":46.7367,"firstPhysicalMutationToFirstNewGenerationMs":101.1261,"presentationToStrictCompletionMs":0}            |
| 14  | 1273.888 / 837.473  | 1361.411 / 978.195  | 87.523 / 140.722  | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5728,"dispatchToBlockedOrPreparationMs":459.1682,"firstNewGenerationToCleanupDrainedMs":187.2123,"firstPhysicalMutationToFirstNewGenerationMs":326.2418,"presentationToStrictCompletionMs":191.157}    |
| 15  | 945.154 / 898.082   | 634.137 / 655.861   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.506,"dispatchToBlockedOrPreparationMs":379.4159,"firstNewGenerationToCleanupDrainedMs":48.2907,"firstPhysicalMutationToFirstNewGenerationMs":222.6484,"presentationToStrictCompletionMs":0}            |
| 16  | 789.230 / 763.098   | 608.752 / 670.344   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.186,"dispatchToBlockedOrPreparationMs":396.8313,"firstNewGenerationToCleanupDrainedMs":46.8969,"firstPhysicalMutationToFirstNewGenerationMs":221.4302,"presentationToStrictCompletionMs":0}            |
| 17  | 831.971 / 746.341   | 680.701 / 606.583   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1624,"dispatchToBlockedOrPreparationMs":359.1756,"firstNewGenerationToCleanupDrainedMs":43.1767,"firstPhysicalMutationToFirstNewGenerationMs":200.0686,"presentationToStrictCompletionMs":0}           |
| 18  | 757.557 / 774.013   | 620.771 / 589.539   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3676,"dispatchToBlockedOrPreparationMs":371.5552,"firstNewGenerationToCleanupDrainedMs":0.4173,"firstPhysicalMutationToFirstNewGenerationMs":213.1992,"presentationToStrictCompletionMs":0}            |
| 19  | 758.886 / 758.939   | 624.111 / 625.553   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5231,"dispatchToBlockedOrPreparationMs":379.0175,"firstNewGenerationToCleanupDrainedMs":43.4177,"firstPhysicalMutationToFirstNewGenerationMs":198.5947,"presentationToStrictCompletionMs":0}           |
| 20  | 807.760 / 883.470   | 1053.683 / 1099.831 | 245.923 / 216.361 | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} | {"blockedOrPreparationToFirstPhysicalMutationMs":285.3622,"dispatchToBlockedOrPreparationMs":406.3696,"firstNewGenerationToCleanupDrainedMs":269.7355,"firstPhysicalMutationToFirstNewGenerationMs":138.3633,"presentationToStrictCompletionMs":216.3606} |
| 21  | 222.836 / 235.492   | 495.001 / 546.920   | 272.166 / 311.428 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":144.9138,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":311.4284}             |
| 22  | 217.047 / 188.521   | 217.047 / 188.521   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":188.5211,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 254.963 / 318.899   | 254.963 / 318.899   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":318.8989,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 935.684 / 660.640   | 1118.329 / 863.544  | 182.645 / 202.904 | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3754,"dispatchToBlockedOrPreparationMs":457.2799,"firstNewGenerationToCleanupDrainedMs":246.6444,"firstPhysicalMutationToFirstNewGenerationMs":155.2444,"presentationToStrictCompletionMs":202.904}    |
| 25  | 804.197 / 985.077   | 943.123 / 1155.156  | 138.926 / 170.079 | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":49.5081,"dispatchToBlockedOrPreparationMs":473.5216,"firstNewGenerationToCleanupDrainedMs":225.4663,"firstPhysicalMutationToFirstNewGenerationMs":406.6602,"presentationToStrictCompletionMs":277.9776}  |
| 26  | 1514.576 / 866.625  | 1459.934 / 974.481  | 0 / 107.856       | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5214,"dispatchToBlockedOrPreparationMs":425.9281,"firstNewGenerationToCleanupDrainedMs":199.1435,"firstPhysicalMutationToFirstNewGenerationMs":344.8882,"presentationToStrictCompletionMs":157.5178}   |
| 27  | 527.127 / 565.339   | 774.192 / 824.432   | 247.065 / 259.093 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1502,"dispatchToBlockedOrPreparationMs":403.2796,"firstNewGenerationToCleanupDrainedMs":260.3308,"firstPhysicalMutationToFirstNewGenerationMs":155.671,"presentationToStrictCompletionMs":259.093}     |
| 28  | 798.736 / 801.738   | 887.620 / 942.457   | 88.884 / 140.720  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9855,"dispatchToBlockedOrPreparationMs":422.4793,"firstNewGenerationToCleanupDrainedMs":186.51,"firstPhysicalMutationToFirstNewGenerationMs":329.4826,"presentationToStrictCompletionMs":188.8948}     |
| 29  | 1701.862 / 1379.208 | 1838.119 / 1527.516 | 136.257 / 148.307 | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} | {"blockedOrPreparationToFirstPhysicalMutationMs":32.0488,"dispatchToBlockedOrPreparationMs":759.8902,"firstNewGenerationToCleanupDrainedMs":199.6632,"firstPhysicalMutationToFirstNewGenerationMs":535.9134,"presentationToStrictCompletionMs":236.662}   |
| 30  | 588.638 / 529.216   | 827.143 / 807.863   | 238.505 / 278.647 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9681,"dispatchToBlockedOrPreparationMs":382.2503,"firstNewGenerationToCleanupDrainedMs":279.641,"firstPhysicalMutationToFirstNewGenerationMs":141.0032,"presentationToStrictCompletionMs":278.647}     |
| 31  | 628.123 / 661.757   | 628.123 / 661.757   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":60.3762,"dispatchToBlockedOrPreparationMs":452.7068,"firstNewGenerationToCleanupDrainedMs":48.9836,"firstPhysicalMutationToFirstNewGenerationMs":99.6907,"presentationToStrictCompletionMs":0}           |
| 32  | 210.815 / 232.288   | 531.443 / 533.560   | 320.628 / 301.272 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":146.1729,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":301.2724}             |
| 33  | 273.671 / 309.080   | 273.671 / 309.080   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":309.0802,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 484.990 / 571.758    | 86.768   | 15 / 16           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 4   | 14 / 12                  | -2           | 761.174 / 830.844    | 69.670   | 19 / 17           | -2           |
| 5   | 13 / 13                  | 0            | 761.523 / 825.665    | 64.141   | 18 / 18           | 0            |
| 6   | 17 / 16                  | -1           | 909.742 / 942.127    | 32.386   | 22 / 21           | -1           |
| 7   | 18 / 17                  | -1           | 1044.523 / 996.915   | -47.608  | 23 / 22           | -1           |
| 8   | 17 / 16                  | -1           | 923.903 / 927.065    | 3.162    | 22 / 21           | -1           |
| 9   | 16 / 18                  | 2            | 917.460 / 963.017    | 45.557   | 21 / 23           | 2            |
| 10  | 10 / 10                  | 0            | 628.552 / 626.864    | -1.688   | 15 / 16           | 1            |
| 11  | 3 / 3                    | 0            | 198.659 / 183.992    | -14.666  | 9 / 9             | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 577.400 / 598.263    | 20.863   | 10 / 10           | 0            |
| 14  | 23 / 14                  | -9           | 1185.491 / 790.983   | -394.508 | 28 / 19           | -9           |
| 15  | 12 / 12                  | 0            | 587.847 / 607.570    | 19.723   | 20 / 18           | -2           |
| 16  | 12 / 12                  | 0            | 564.512 / 623.447    | 58.935   | 17 / 15           | -2           |
| 17  | 12 / 12                  | 0            | 632.504 / 563.407    | -69.097  | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 576.864 / 589.122    | 12.258   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 581.254 / 582.135    | 0.882    | 16 / 16           | 0            |
| 20  | 16 / 16                  | 0            | 807.475 / 830.095    | 22.620   | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 5             | 2            |
| 24  | 15 / 9                   | -6           | 889.114 / 616.900    | -272.214 | 20 / 14           | -6           |
| 25  | 13 / 13                  | 0            | 760.230 / 929.690    | 169.460  | 18 / 18           | 0            |
| 26  | 23 / 13                  | -10          | 1232.033 / 775.338   | -456.695 | 28 / 18           | -10          |
| 27  | 10 / 10                  | 0            | 526.073 / 564.101    | 38.028   | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 711.446 / 755.947    | 44.501   | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1659.323 / 1327.852  | -331.470 | 34 / 28           | -6           |
| 30  | 10 / 9                   | -1           | 587.951 / 528.222    | -59.729  | 16 / 15           | -1           |
| 31  | 9 / 9                    | 0            | 584.095 / 612.774    | 28.679   | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 5             | 1            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 213.825 / 257.803  | 43.978   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 210.089 / 236.217  | 26.128   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.853 / 409.462  | 28.608   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 441.441 / 438.791  | -2.651   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.860 / 417.643  | 15.783   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 429.952 / 418.172  | -11.780  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 2              | -4    | 308.404 / 143.398  | -165.005 |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 158.148 / 160.886  | 2.738    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 143.759 / 161.141  | 17.383   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 165.514 / 149.821  | -15.693  |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 155.220 / 148.574  | -6.646   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 149.943 / 147.453  | -2.490   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 341.305 / 361.725  | 20.419   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 2              | 0     | 210.712 / 268.748  | 58.036   |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 617.137 / 105.293  | -511.844 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1054.946 / 755.902 | -299.045 |
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

### nvidia-2026-09-10T13-36-15-291Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":88,"allowedPresentationStretchEpisodes":20,"allowedPresentationStretchEyeObservations":191,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":15638,"leftPath":"NativeOriginal","referenceFrame":16026,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### nvidia-2026-09-10T13-36-15-291Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                                                                                                                                                                                                    | Limit                                                                                       | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| fidelity_invariants              | {"current":0,"metrics":4}                                                                                                                                                                                                                                                                                                   | 0                                                                                           | HEALTH            | Applicable observed health gate.                                                                |
| presentation_fallbacks           | {"allowedPresentationStretchCompletedFrames":65,"allowedPresentationStretchEpisodes":17,"allowedPresentationStretchEyeObservations":143,"allowedPresentationStretchMaximumFrames":6,"boundsMismatchOriginalFallbackEyeObservations":0,"validatedPresentationHoldEyeObservations":0,"vendorFailureStretchEyeObservations":2} | {"boundsMismatchOriginalFallbackEyeObservations":0,"vendorFailureStretchEyeObservations":0} | HEALTH            | Applicable observed health gate.                                                                |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                                                                                                                                                                                              | {"maximumFrames":2}                                                                         | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":19968,"leftPath":"NativeOriginal","referenceFrame":20338,"rightPath":"NativeOriginal","stableVendorPresentation":true}                                                                                                                                                                           | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true}       | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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
| nvidia | 1    | processPrivateMiB | 16894.5078125 / 16681.24609375 / -213.26171875 | 16893 / 16615.19921875 / -277.80078125         | -64.539               |
| nvidia | 1    | systemCommitMiB   | 55422.94921875 / 55060.37890625 / -362.5703125 | 55468.4609375 / 54664.203125 / -804.2578125    | -441.688              |
| nvidia | 1    | dxgiUsageMiB      | 5583.1171875 / 3750.015625 / -1833.1015625     | 6028.24609375 / 3819.6875 / -2208.55859375     | -375.457              |
| nvidia | 1    | liveTextures      | 0 / 261 / 261                                  | 0 / 232 / 232                                  | -29                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2429.8961448669434 / 2429.8961448669434    | 0 / 2325.431926727295 / 2325.431926727295      | -104.464              |
| nvidia | 2    | processPrivateMiB | 17097.22265625 / 16617.34765625 / -479.875     | 16991.14453125 / 16646.5546875 / -344.58984375 | 135.285               |
| nvidia | 2    | systemCommitMiB   | 55505.6015625 / 54815.92578125 / -689.67578125 | 54965.67578125 / 54796.97265625 / -168.703125  | 520.973               |
| nvidia | 2    | dxgiUsageMiB      | 4066.75390625 / 3643.64453125 / -423.109375    | 4136.42578125 / 3616.015625 / -520.41015625    | -97.301               |
| nvidia | 2    | liveTextures      | 0 / 211 / 211                                  | 0 / 210 / 210                                  | -1                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2274.9311332702637 / 2274.9311332702637    | 0 / 2267.056079864502 / 2267.056079864502      | -7.875                |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2168        | 2121        | -47         |
| cpu/compactPresentationContract/reuses                  | 2146        | 2099        | -47         |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 155         | 154         | -1          |
| cpu/generationResourceValidation/fullValidations        | 581         | 576         | -5          |
| cpu/generationResourceValidation/stableChecks           | 8236        | 8088        | -148        |
| cpu/generationResourceValidation/stableHits             | 8159        | 8009        | -150        |
| cpu/generationResourceValidation/stableMisses           | 77          | 79          | 2           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 1           | 1           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4149        | 4099        | -50         |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4149        | 4099        | -50         |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4107        | 4057        | -50         |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4149        | 4099        | -50         |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4128        | 4078        | -50         |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4115        | 4065        | -50         |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4054        | 4004        | -50         |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4134        | 4084        | -50         |
| cpu/strongStereoPacket/captures                         | 4492        | 4412        | -80         |
| cpu/strongStereoPacket/commitAccepts                    | 4317        | 4220        | -97         |
| cpu/strongStereoPacket/commitRejects                    | 65          | 68          | 3           |
| cpu/strongStereoPacket/commitValidations                | 4382        | 4288        | -94         |
| cpu/strongStereoPacket/cycleReuses                      | 2205        | 2164        | -41         |
| cpu/strongStereoPacket/fastSkips                        | 3806        | 3786        | -20         |
| cpu/strongStereoPacket/invalidations                    | 4706        | 4649        | -57         |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 105         | 1           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2183        | 2143        | -40         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.867       | 1.900       | 0.033       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 22.700      | 32.500      | 9.800       |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.138       | 0.004       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 13          | 11.400      |
| cpu/window/currentFrame                                 | 53015       | 16026       | -36989      |
| cpu/window/elapsedFrames                                | 4148        | 4098        | -50         |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 48867       | 11928       | -36939      |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 53017       | 16028       | -36989      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5974703856  | 5852900592  | -121803264  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4709        | 4613        | -96         |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11961613440 | 11717758080 | -243855360  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.297       | 0.307       | 0.010       |
| gpu/item5ActiveFSRCopies/activePixels                   | 11119322840 | 11190869160 | 71546320    |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26297233960 | 25285828440 | -1011405520 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14730       | 14360       | -370        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 1971        | 1885        | -86         |
| gpu/item7EarlyHAM/executedClears                        | 1974        | 1966        | -8          |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1974        | 1966        | -8          |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4104        | 4010        | -94         |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4150        | 4100        | -50         |
| gpu/startFrame                                          | 48867       | 11928       | -36939      |
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
| texture/createdCount                                    | 3962        | 3964        | 2           |
| texture/createdEstimatedBytes                           | 38815224912 | 38833609784 | 18384872    |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3701        | 3732        | 31          |
| texture/destroyedEstimatedBytes                         | 36267294132 | 36395217676 | 127923544   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 886         | 884         | -2          |
| texture/liveTextureRecordCount                          | 261         | 232         | -29         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 50          | 21          | -29         |
| texture/niSourceTextureMatchedEstimatedBytes            | 172279000   | 62740328    | -109538672  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1536        | 1507        | -29         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 261         | 232         | -29         |
| texture/outstandingEstimatedBytes                       | 2547930780  | 2438392108  | -109538672  |
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
| cpu/compactPresentationContract/publishes               | 2143        | 2056        | -87        |
| cpu/compactPresentationContract/reuses                  | 2121        | 2034        | -87        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 156         | 147         | -9         |
| cpu/generationResourceValidation/fullValidations        | 570         | 540         | -30        |
| cpu/generationResourceValidation/stableChecks           | 8160        | 7793        | -367       |
| cpu/generationResourceValidation/stableHits             | 8083        | 7714        | -369       |
| cpu/generationResourceValidation/stableMisses           | 77          | 79          | 2          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4106        | 3924        | -182       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4106        | 3924        | -182       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4064        | 3882        | -182       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4106        | 3924        | -182       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4085        | 3903        | -182       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4072        | 3890        | -182       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4017        | 3843        | -174       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 82          | -8         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4091        | 3909        | -182       |
| cpu/strongStereoPacket/captures                         | 4444        | 4246        | -198       |
| cpu/strongStereoPacket/commitAccepts                    | 4266        | 4095        | -171       |
| cpu/strongStereoPacket/commitRejects                    | 66          | 63          | -3         |
| cpu/strongStereoPacket/commitValidations                | 4332        | 4158        | -174       |
| cpu/strongStereoPacket/cycleReuses                      | 2180        | 2083        | -97        |
| cpu/strongStereoPacket/fastSkips                        | 3768        | 3602        | -166       |
| cpu/strongStereoPacket/invalidations                    | 4653        | 4475        | -178       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 99          | -5         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2160        | 2064        | -96        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.869       | 1.946       | 0.076      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 36.700      | 54.300      | 17.600     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.138       | 0.005      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.500       | 4.200       | 2.700      |
| cpu/window/currentFrame                                 | 57516       | 20339       | -37177     |
| cpu/window/elapsedFrames                                | 4107        | 3925        | -182       |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 53409       | 16414       | -36995     |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 57516       | 20340       | -37176     |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5901114384  | 5687958672  | -213155712 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4651        | 4483        | -168       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11814284160 | 11387537280 | -426746880 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.304       | -0.003     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11542473520 | 11121084080 | -421389440 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26077296080 | 25482621520 | -594674560 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14810       | 14410       | -400       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1955        | 1915        | -40        |
| gpu/item7EarlyHAM/executedClears                        | 1948        | 1854        | -94        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1948        | 1854        | -94        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4064        | 3930        | -134       |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4107        | 3926        | -181       |
| gpu/startFrame                                          | 53409       | 16414       | -36995     |
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
| texture/createdCount                                    | 3914        | 3910        | -4         |
| texture/createdEstimatedBytes                           | 38675267808 | 38626218712 | -49049096  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3703        | 3700        | -3         |
| texture/destroyedEstimatedBytes                         | 36289829620 | 36249038116 | -40791504  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 863         | 867         | 4          |
| texture/liveTextureRecordCount                          | 211         | 210         | -1         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 4           | 3           | -1         |
| texture/niSourceTextureMatchedEstimatedBytes            | 9786808     | 1529216     | -8257592   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1509        | 1508        | -1         |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 211         | 210         | -1         |
| texture/outstandingEstimatedBytes                       | 2385438188  | 2377180596  | -8257592   |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 2           | 2           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

## Context, memory, CPU/GPU and evidence

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
