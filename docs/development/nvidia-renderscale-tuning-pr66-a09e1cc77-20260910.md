# NVIDIA render-scale tuning: PR66 a09e1cc77, September 10

Both 33-transition passes completed in Skyrim VR PID 44000. Terminal
render PASS; Task 2 counts 66 PASS, 0 FAIL, 0 INCONCLUSIVE. Applicable
full-history health is MET in both passes. Reporting is COMPLETE and
all task-owned captures are verified inactive; the evidence journal is
flushed. These are separate execution, correctness and reporting results.

The measured clean Release source is `a09e1cc77de098f85e74e6d5bb341dc184f83640`,
based on main-VR `bf4ae54a7d49620c41cb32ee9ecfd44657688ead`.
The previous relevant measured main-VR reference is
`nvidia-20260910T124329625Z`, compiled from `7c8e3e656`.
The physical 28,062,720-byte DLL, adjacent manifest and AIO receipt match
runtime Build ID `9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757`.
Its SHA-256 is `aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461`.
The selected profile has one enabled loose DLL provider; Overwrite and
unmanaged Data have no competing DLL. Full compile identity is retained.

## Results and limitations

| Metric                     | Pass 1   | Pass 2   |
| -------------------------- | -------- | -------- |
| Measured transitions       | 33       | 33       |
| Strict mean (ms)           | 793.336  | 807.126  |
| Reference strict mean (ms) | 866.009  | 833.078  |
| Strict mean delta (%)      | -8.392   | -3.115   |
| Strict maximum (ms)        | 1856.757 | 1932.147 |
| Cleanup-tail mean (ms)     | 96.436   | 108.382  |
| Cleanup-tail maximum (ms)  | 266.049  | 298.618  |
| Owned retries              | 9        | 10       |

All five failure classes are zero: device loss, OOM, producer terminal
failure, vendor-native qualification failure and credible liveness timeout.
There are also zero fidelity mismatch, vendor fallback, bounds fallback,
lifecycle, memory-trim and retirement-fence observations in either pass.
The reference's fidelity/vendor-failure observations on rows 26 and 28
are absent in both candidate passes.

The lower means do not apply to every route. FSR Native AA to DLSS Hoshipa
(row 25) remains slower than the reference in both passes. Every route's
absolute and percentage delta is retained below. There were 32 transitions
with selected presentation stretch; all recovered. Pass captures recorded
17/18 completed stretch episodes, 69/78 frames and 4143.532/4955.678 ms.
The fixed stretch cutoff remains a diagnostic when settling imposes it;
raw native-target contract mismatches and all gate values remain visible.
The maximum client dispatch gap was 44.615 ms against a 250 ms budget.

Memory classification is inconclusive. Pass-1/pass-2 process-private
deltas were -114.105/-213.449 MiB; system commit -101.223/+40.145 MiB;
DXGI usage -674.609/-323.090 MiB. Full six-boundary memory and texture
evidence, ratios, and classification inputs follow. These measurements do
not establish leak freedom.

The improvement-or-neutral assessment is INCONCLUSIVE: retained scene and
toolchain differ, a matching fixture fingerprint is unavailable, and no
versioned tolerance policy was specified. These are transition timings;
fresh resolved GPU samples are unavailable for a steady-state GPU/FPS claim.

## Validation and retained evidence

The installed NVIDIA runner executed one positioning COC, two baselines,
and 66 runtime-only applies with five-second pre-dispatch waits and strict
20-second terminal budgets. Both passes ran in the same process.

The packaged finalizer completed in 23.495 seconds. The maintained
`tools/compare-render-scale-ledger.py` workflow generated the comparison
once and audited 1,056 numeric timing cells, including all 528 candidate
timings. Exact reconstruction of every summary and comparison field passed,
with all historical ledger cells preserved. Finalization, comparison and
audit stage timings remain in the local performance receipts.

The preserved AIO receipt reports a successful runtime DLL build and
shader validation. It also retains a strict controller-build C4458 failure;
the diagnostic test build used a local warning-policy override. No build,
deployment, controller/shader test suite, or SE/AE runtime test was run as
part of this measurement task.

[Canonical comparison ledger](vr-render-scale-ledger.md).

Local evidence: `artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z/`.
Complete field audit: `complete-ledger-validation.json` in that directory.
The full scalar export and immutable journal remain local. This report
contains the paired comparison and complete assay tables below.

## Candidate pass 1 and pass 2 by transition group

All timing pairs are pass 1 / pass 2 in milliseconds. Retry diagnostics,
per-role waits and stabilization intervals are retained in the assay tables.
Stretch flags describe selection and recovery, separately from failures.

### NVIDIA DLSS and DLAA

| Row / route                                          | Retry diagnostic P1/P2 | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 | Strict ms           | Presentation ms     | Cleanup tail ms   | Relatch ms                                                              | Stretch selected/recovered P1/P2 |
| ---------------------------------------------------- | ---------------------- | ------------- | ------------ | ------------ | ------------------- | ------------------- | ----------------- | ----------------------------------------------------------------------- | -------------------------------- |
| 3: TAA -> DLAA                                       | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 250.227 / 247.366   | 250.227 / 247.366   | 0.000 / 0.000     | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |
| 4: DLAA -> DLSS Hoshipa                              | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 911.526 / 1043.867  | 789.446 / 816.593   | 40.280 / 140.250  | 670.423 / 772.572                                                       | True/True / True/True            |
| 5: DLSS Hoshipa -> DLSS Ultra Quality                | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 1151.036 / 1038.184 | 945.179 / 812.136   | 123.257 / 140.851 | 904.847 / 761.827                                                       | True/True / True/True            |
| 6: DLSS Ultra Quality -> DLSS Quality                | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1085.145 / 1196.438 | 877.562 / 963.683   | 127.602 / 144.789 | 830.726 / 915.181                                                       | True/True / True/True            |
| 7: DLSS Quality -> DLSS Balanced                     | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1311.362 / 1202.543 | 1092.614 / 965.785  | 134.973 / 147.564 | 1035.316 / 917.597                                                      | True/True / True/True            |
| 8: DLSS Balanced -> DLSS Performance                 | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1267.597 / 1191.568 | 1048.367 / 944.879  | 138.557 / 153.015 | 996.634 / 896.100                                                       | True/True / True/True            |
| 9: DLSS Performance -> DLSS Ultra Performance        | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1257.584 / 1123.080 | 1031.843 / 892.407  | 142.827 / 140.686 | 990.309 / 848.555                                                       | True/True / True/True            |
| 10: DLSS Ultra Performance -> DLAA                   | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 760.848 / 779.290   | 590.580 / 604.626   | 170.268 / 174.664 | 545.917 / 558.579                                                       | False/False / False/False        |
| 23: NONE -> DLAA                                     | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 263.125 / 250.712   | 263.125 / 250.712   | 0.000 / 0.000     | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |
| 25: FSR3 Native AA -> DLSS Hoshipa                   | complete / complete    | 2 / 1         | PASS / PASS  | PASS / PASS  | 1856.757 / 1543.613 | 1645.743 / 1323.681 | 128.616 / 134.215 | 1603.945 / 1279.758                                                     | True/True / True/True            |
| 29: FSR3 Ultra Performance -> DLSS Ultra Performance | complete / complete    | 1 / 2         | PASS / PASS  | PASS / PASS  | 1446.692 / 1932.147 | 1218.375 / 1762.502 | 137.629 / 87.671  | 1173.442 / 1677.335                                                     | True/True / True/True            |
| 33: NONE -> DLAA                                     | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 276.262 / 253.033   | 276.262 / 253.033   | 0.000 / 0.000     | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |

### NVIDIA FSR3

| Row / route                                    | Retry diagnostic P1/P2 | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 | Strict ms           | Presentation ms     | Cleanup tail ms   | Relatch ms          | Stretch selected/recovered P1/P2 |
| ---------------------------------------------- | ---------------------- | ------------- | ------------ | ------------ | ------------------- | ------------------- | ----------------- | ------------------- | -------------------------------- |
| 13: NONE -> FSR3 Native AA                     | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 747.166 / 641.300   | 747.166 / 641.300   | 0.000 / 0.000     | 706.069 / 594.114   | False/False / False/False        |
| 14: FSR3 Native AA -> FSR3 Hoshipa             | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1350.983 / 1377.913 | 1222.442 / 1171.620 | 83.792 / 152.101  | 1133.803 / 1120.356 | True/True / True/True            |
| 15: FSR3 Hoshipa -> FSR3 Ultra Quality         | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 903.650 / 896.172   | 903.650 / 896.172   | 0.000 / 0.000     | 561.661 / 558.729   | True/True / True/True            |
| 16: FSR3 Ultra Quality -> FSR3 Quality         | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 731.838 / 702.990   | 731.838 / 702.990   | 0.000 / 0.000     | 559.842 / 568.091   | True/True / True/True            |
| 17: FSR3 Quality -> FSR3 Balanced              | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 719.437 / 771.931   | 719.437 / 771.931   | 0.000 / 0.000     | 545.157 / 552.657   | True/True / True/True            |
| 18: FSR3 Balanced -> FSR3 Performance          | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 760.866 / 751.162   | 760.866 / 751.162   | 0.000 / 0.000     | 584.199 / 576.083   | True/True / True/True            |
| 19: FSR3 Performance -> FSR3 Ultra Performance | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 781.359 / 727.324   | 781.359 / 727.324   | 0.000 / 0.000     | 606.125 / 555.956   | True/True / True/True            |
| 20: FSR3 Ultra Performance -> FSR3 Native AA   | complete / complete    | 0 / 1         | PASS / PASS  | PASS / PASS  | 739.768 / 1078.028  | 530.869 / 901.838   | 208.899 / 176.190 | 477.821 / 853.302   | False/False / True/True          |
| 24: DLAA -> FSR3 Native AA                     | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1184.660 / 1133.454 | 993.040 / 955.472   | 191.620 / 177.982 | 935.371 / 912.054   | False/False / False/False        |
| 26: DLSS Hoshipa -> FSR3 Hoshipa               | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 932.906 / 901.361   | 803.459 / 763.246   | 85.433 / 138.115  | 721.216 / 675.179   | True/True / True/True            |
| 28: NONE -> FSR3 Ultra Performance             | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 910.661 / 984.498   | 810.416 / 849.139   | 50.538 / 89.058   | 673.450 / 762.689   | False/False / False/False        |
| 31: TAA -> FSR3 Native AA                      | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 577.394 / 660.497   | 577.394 / 660.497   | 0.000 / 0.000     | 538.000 / 617.707   | False/False / False/False        |

### NVIDIA provider crossings

| Row / route                                          | Retry diagnostic P1/P2 | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 | Strict ms           | Presentation ms     | Cleanup tail ms   | Relatch ms          | Stretch selected/recovered P1/P2 |
| ---------------------------------------------------- | ---------------------- | ------------- | ------------ | ------------ | ------------------- | ------------------- | ----------------- | ------------------- | -------------------------------- |
| 24: DLAA -> FSR3 Native AA                           | complete / complete    | 1 / 1         | PASS / PASS  | PASS / PASS  | 1184.660 / 1133.454 | 993.040 / 955.472   | 191.620 / 177.982 | 935.371 / 912.054   | False/False / False/False        |
| 25: FSR3 Native AA -> DLSS Hoshipa                   | complete / complete    | 2 / 1         | PASS / PASS  | PASS / PASS  | 1856.757 / 1543.613 | 1645.743 / 1323.681 | 128.616 / 134.215 | 1603.945 / 1279.758 | True/True / True/True            |
| 26: DLSS Hoshipa -> FSR3 Hoshipa                     | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 932.906 / 901.361   | 803.459 / 763.246   | 85.433 / 138.115  | 721.216 / 675.179   | True/True / True/True            |
| 29: FSR3 Ultra Performance -> DLSS Ultra Performance | complete / complete    | 1 / 2         | PASS / PASS  | PASS / PASS  | 1446.692 / 1932.147 | 1218.375 / 1762.502 | 137.629 / 87.671  | 1173.442 / 1677.335 | True/True / True/True            |

### NVIDIA TAA and None

| Row / route                       | Retry diagnostic P1/P2 | Retries P1/P2 | Render P1/P2 | Task 2 P1/P2 | Strict ms         | Presentation ms   | Cleanup tail ms   | Relatch ms                                                              | Stretch selected/recovered P1/P2 |
| --------------------------------- | ---------------------- | ------------- | ------------ | ------------ | ----------------- | ----------------- | ----------------- | ----------------------------------------------------------------------- | -------------------------------- |
| 1: DLSS Hoshipa -> NONE           | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 680.250 / 764.649 | 501.629 / 524.712 | 178.621 / 239.937 | 451.448 / 523.896                                                       | True/True / False/False          |
| 2: NONE -> TAA                    | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 169.466 / 167.665 | 169.466 / 167.665 | 0.000 / 0.000     | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |
| 11: DLAA -> TAA                   | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 463.236 / 435.637 | 197.187 / 163.343 | 266.049 / 272.294 | 195.942 / 162.582                                                       | False/False / False/False        |
| 12: TAA -> NONE                   | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 171.239 / 183.527 | 171.239 / 183.527 | 0.000 / 0.000     | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |
| 21: FSR3 Native AA -> TAA         | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 463.465 / 501.448 | 205.525 / 202.830 | 257.940 / 298.618 | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |
| 22: TAA -> NONE                   | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 170.549 / 169.901 | 170.549 / 169.901 | 0.000 / 0.000     | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |
| 27: FSR3 Hoshipa -> NONE          | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 751.644 / 819.338 | 490.910 / 559.830 | 260.734 / 259.509 | 490.102 / 558.577                                                       | False/False / False/False        |
| 30: DLSS Ultra Performance -> TAA | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 681.036 / 696.614 | 492.154 / 464.694 | 188.881 / 231.920 | 445.611 / 464.188                                                       | False/False / False/False        |
| 32: FSR3 Native AA -> NONE        | complete / complete    | 0 / 0         | PASS / PASS  | PASS / PASS  | 450.355 / 467.899 | 184.484 / 190.736 | 265.870 / 277.163 | n/a; not applicable or not exposed / n/a; not applicable or not exposed | False/False / False/False        |

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                                       |
| ----------------------- | --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T124329625Z                                                              | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              |
| Renderer base           | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        |
| Compiled source         | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        |
| Build ID                | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                |
| DLL SHA-256             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 866.009/793.336 | -8.392       | 10/9        | 4/0          | 2/0                 | none             | NOT_MET/MET         |
| nvidia | 2    | 33/33    | 833.078/807.126 | -3.115       | 10/10       | 4/0          | 2/0                 | none             | NOT_MET/MET         |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 807.337   | 735.095   | -72.242   | -8.948  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920    | 13.400    | -0.520    | -3.736  |
| nvidia | 1    | Relatch proof total        | ms          | 20183.415 | 18377.376 | -1806.039 | -8.948  |
| nvidia | 1    | Relatch proof total        | frames      | 348       | 335       | -13       | -3.736  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 866.009   | 793.336   | -72.672   | -8.392  |
| nvidia | 1    | Strict completion mean     | frames      | 15.576    | 15.091    | -0.485    | -3.113  |
| nvidia | 1    | Strict completion total    | ms          | 28578.281 | 26180.089 | -2398.191 | -8.392  |
| nvidia | 1    | Strict completion total    | frames      | 514       | 498       | -16       | -3.113  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 17        | -2        | -10.526 |
| nvidia | 1    | Stretch completed total    | frames      | 89        | 69        | -20       | -22.472 |
| nvidia | 1    | Stretch completed total    | ms          | 5719.962  | 4143.532  | -1576.429 | -27.560 |
| nvidia | 1    | Stretch longest episode    | ms          | 646.277   | 376.746   | -269.531  | -41.705 |
| nvidia | 2    | Relatch proof mean         | ms          | 763.765   | 747.347   | -16.419   | -2.150  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.840    | 13.760    | -0.080    | -0.578  |
| nvidia | 2    | Relatch proof total        | ms          | 19094.136 | 18683.664 | -410.472  | -2.150  |
| nvidia | 2    | Relatch proof total        | frames      | 346       | 344       | -2        | -0.578  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 833.078   | 807.126   | -25.952   | -3.115  |
| nvidia | 2    | Strict completion mean     | frames      | 15.455    | 15.303    | -0.152    | -0.980  |
| nvidia | 2    | Strict completion total    | ms          | 27491.579 | 26635.148 | -856.430  | -3.115  |
| nvidia | 2    | Strict completion total    | frames      | 510       | 505       | -5        | -0.980  |
| nvidia | 2    | Stretch completed episodes | episodes    | 19        | 18        | -1        | -5.263  |
| nvidia | 2    | Stretch completed total    | frames      | 83        | 78        | -5        | -6.024  |
| nvidia | 2    | Stretch completed total    | ms          | 5383.107  | 4955.678  | -427.429  | -7.940  |
| nvidia | 2    | Stretch longest episode    | ms          | 441.441   | 408.503   | -32.939   | -7.462  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 688.545 / 680.250   | -8.295   | -1.205  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.425 / 169.466   | 0.041    | 0.024   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 249.126 / 250.227   | 1.102    | 0.442   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.374 / 911.526  | -92.848  | -9.244  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.234 / 1151.036 | -115.198 | -9.098  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.702 / 1085.145 | -90.556  | -7.702  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.400 / 1311.362 | -82.038  | -5.888  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.171 / 1267.597 | -84.574  | -6.255  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.511 / 1257.584 | -128.928 | -9.299  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 770.294 / 760.848   | -9.446   | -1.226  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 450.117 / 463.236   | 13.120   | 2.915   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 190.626 / 171.239   | -19.387  | -10.170 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 822.942 / 747.166   | -75.776  | -9.208  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.140 / 1350.983 | -26.156  | -1.899  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.789 / 903.650   | 153.861  | 20.521  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 929.137 / 731.838   | -197.300 | -21.235 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 762.921 / 719.437   | -43.485  | -5.700  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.519 / 760.866  | -639.654 | -45.673 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 778.498 / 781.359   | 2.861    | 0.368   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 731.796 / 739.768   | 7.972    | 1.089   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 572.501 / 463.465   | -109.036 | -19.045 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 172.732 / 170.549   | -2.183   | -1.264  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 261.855 / 263.125   | 1.270    | 0.485   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1233.573 / 1184.660 | -48.913  | -3.965  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.698 / 1856.757  | 880.059  | 90.106  | 0/2         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.005 / 932.906  | -603.099 | -39.264 | 1/0         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.032 / 751.644   | -181.388 | -19.441 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 924.794 / 910.661   | -14.133  | -1.528  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.558 / 1446.692 | -737.866 | -33.776 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 744.487 / 681.036   | -63.451  | -8.523  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 610.701 / 577.394   | -33.307  | -5.454  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 481.567 / 450.355   | -31.212  | -6.481  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 296.510 / 276.262   | -20.248  | -6.829  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 466.266 / 501.629   | 688.545 / 680.250   | 222.279 / 178.621 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   |
| 2   | 169.425 / 169.466   | 169.425 / 169.466   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 249.126 / 250.227   | 249.126 / 250.227   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 790.942 / 789.446   | 920.929 / 829.726   | 129.987 / 40.280  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   |
| 5   | 1014.085 / 945.179  | 1168.706 / 1068.436 | 154.620 / 123.257 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   |
| 6   | 947.755 / 877.562   | 1089.396 / 1005.163 | 141.641 / 127.602 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   |
| 7   | 1160.289 / 1092.614 | 1307.700 / 1227.587 | 147.411 / 134.973 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   |
| 8   | 1122.704 / 1048.367 | 1271.880 / 1186.924 | 149.177 / 138.557 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    |
| 9   | 1153.443 / 1031.843 | 1299.332 / 1174.671 | 145.889 / 142.827 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   |
| 10  | 592.366 / 590.580   | 770.294 / 760.848   | 177.929 / 170.268 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   |
| 11  | 171.418 / 197.187   | 450.117 / 463.236   | 278.699 / 266.049 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    |
| 12  | 190.626 / 171.239   | 190.626 / 171.239   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 822.942 / 747.166   | 822.942 / 747.166   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           |
| 14  | 1240.733 / 1222.442 | 1329.442 / 1306.234 | 88.709 / 83.792   | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  |
| 15  | 749.789 / 903.650   | 570.716 / 561.963   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            |
| 16  | 929.137 / 731.838   | 662.595 / 602.543   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            |
| 17  | 762.921 / 719.437   | 616.503 / 587.405   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           |
| 18  | 1400.519 / 760.866  | 1120.851 / 630.628  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           |
| 19  | 778.498 / 781.359   | 635.134 / 650.456   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            |
| 20  | 546.689 / 530.869   | 731.796 / 739.768   | 185.107 / 208.899 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   |
| 21  | 254.898 / 205.525   | 572.501 / 463.465   | 317.603 / 257.940 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              |
| 22  | 172.732 / 170.549   | 172.732 / 170.549   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 261.855 / 263.125   | 261.855 / 263.125   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 1042.196 / 993.040  | 1233.573 / 1184.660 | 191.377 / 191.620 | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} |
| 25  | 747.831 / 1645.743  | 895.346 / 1774.359  | 147.514 / 128.616 | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  |
| 26  | 1384.499 / 803.459  | 1484.709 / 888.892  | 100.210 / 85.433  | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   |
| 27  | 636.450 / 490.910   | 933.032 / 751.644   | 296.582 / 260.734 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      |
| 28  | 748.294 / 810.416   | 879.078 / 860.954   | 130.784 / 50.538  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   |
| 29  | 1978.717 / 1218.375 | 2084.388 / 1356.004 | 105.670 / 137.629 | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} |
| 30  | 560.371 / 492.154   | 744.487 / 681.036   | 184.117 / 188.881 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   |
| 31  | 610.701 / 577.394   | 610.701 / 577.394   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           |
| 32  | 197.556 / 184.484   | 481.567 / 450.355   | 284.011 / 265.870 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             |
| 33  | 296.510 / 276.262   | 296.510 / 276.262   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 10                   | 1            | 465.818 / 451.448    | -14.370  | 14 / 15           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 4   | 12 / 12                  | 0            | 748.532 / 670.423    | -78.109  | 17 / 17           | 0            |
| 5   | 14 / 12                  | -2           | 960.719 / 904.847    | -55.871  | 19 / 17           | -2           |
| 6   | 17 / 16                  | -1           | 904.599 / 830.726    | -73.874  | 22 / 21           | -1           |
| 7   | 16 / 16                  | 0            | 1113.762 / 1035.316  | -78.446  | 21 / 21           | 0            |
| 8   | 17 / 16                  | -1           | 1079.314 / 996.634   | -82.680  | 22 / 21           | -1           |
| 9   | 16 / 16                  | 0            | 1094.946 / 990.309   | -104.637 | 21 / 21           | 0            |
| 10  | 9 / 10                   | 1            | 541.019 / 545.917    | 4.898    | 15 / 15           | 0            |
| 11  | 3 / 4                    | 1            | 169.912 / 195.942    | 26.029   | 10 / 11           | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 775.120 / 706.069    | -69.050  | 10 / 10           | 0            |
| 14  | 23 / 24                  | 1            | 1155.285 / 1133.803  | -21.482  | 28 / 29           | 1            |
| 15  | 12 / 11                  | -1           | 570.504 / 561.661    | -8.842   | 16 / 19           | 3            |
| 16  | 12 / 12                  | 0            | 611.667 / 559.842    | -51.825  | 18 / 16           | -2           |
| 17  | 12 / 12                  | 0            | 571.105 / 545.157    | -25.948  | 16 / 16           | 0            |
| 18  | 22 / 12                  | -10          | 1076.575 / 584.199   | -492.375 | 29 / 16           | -13          |
| 19  | 12 / 12                  | 0            | 584.498 / 606.125    | 21.627   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 485.074 / 477.821    | -7.253   | 16 / 15           | -1           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 16 / 16                  | 0            | 992.563 / 935.371    | -57.192  | 21 / 21           | 0            |
| 25  | 13 / 29                  | 16           | 704.319 / 1603.945   | 899.626  | 18 / 34           | 16           |
| 26  | 24 / 13                  | -11          | 1288.337 / 721.216   | -567.121 | 29 / 18           | -11          |
| 27  | 10 / 10                  | 0            | 635.729 / 490.102    | -145.627 | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 704.974 / 673.450    | -31.523  | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1879.015 / 1173.442  | -705.573 | 34 / 28           | -6           |
| 30  | 11 / 10                  | -1           | 506.461 / 445.611    | -60.850  | 16 / 15           | -1           |
| 31  | 9 / 9                    | 0            | 563.570 / 538.000    | -25.570  | 11 / 10           | -1           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 116.243 / 119.890  | 3.648    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.448 / 199.802  | -26.646  |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 225.499 / 199.320  | -26.179  |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 378.865 / 371.552  | -7.314   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 379.410 / 376.746  | -2.664   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 375.234 / 366.685  | -8.549   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 422.382 / 370.248  | -52.135  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 293.625 / 268.924  | -24.701  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.915 / 142.698  | -1.218   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 156.516 / 160.236  | 3.720    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 152.521 / 140.158  | -12.362  |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 646.277 / 148.345  | -497.932 |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 151.172 / 154.845  | 3.674    |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 207.881 / 363.912  | 156.031  |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 629.135 / 94.387   | -534.748 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1214.838 / 665.784 | -549.054 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 716.918 / 764.649   | 47.730   | 6.658   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 170.160 / 167.665   | -2.495   | -1.466  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.338 / 247.366   | -38.972  | -13.610 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1032.563 / 1043.867 | 11.303   | 1.095   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1027.220 / 1038.184 | 10.964   | 1.067   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1169.543 / 1196.438 | 26.895   | 2.300   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1390.699 / 1202.543 | -188.156 | -13.530 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.918 / 1191.568 | -4.351   | -0.364  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1249.407 / 1123.080 | -126.327 | -10.111 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 914.287 / 779.290   | -134.997 | -14.765 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 480.252 / 435.637   | -44.615  | -9.290  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.881 / 183.527   | 10.647   | 6.158   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 619.982 / 641.300   | 21.318   | 3.439   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1408.406 / 1377.913 | -30.493  | -2.165  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 945.154 / 896.172   | -48.982  | -5.182  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 789.230 / 702.990   | -86.240  | -10.927 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 831.971 / 771.931   | -60.040  | -7.217  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 757.557 / 751.162   | -6.395   | -0.844  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 758.886 / 727.324   | -31.563  | -4.159  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1053.683 / 1078.028 | 24.345   | 2.310   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 495.001 / 501.448   | 6.447    | 1.302   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 217.047 / 169.901   | -47.147  | -21.722 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 254.963 / 250.712   | -4.251   | -1.667  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1118.329 / 1133.454 | 15.125   | 1.352   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1028.504 / 1543.613 | 515.109  | 50.083  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1514.576 / 901.361  | -613.214 | -40.488 | 1/0         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 774.192 / 819.338   | 45.146   | 5.831   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 939.204 / 984.498   | 45.294   | 4.823   | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1918.325 / 1932.147 | 13.822   | 0.721   | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 827.143 / 696.614   | -130.529 | -15.781 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 628.123 / 660.497   | 32.374   | 5.154   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 531.443 / 467.899   | -63.544  | -11.957 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 273.671 / 253.033   | -20.638  | -7.541  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 485.143 / 524.712   | 716.918 / 764.649   | 231.775 / 239.937 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   |
| 2   | 170.160 / 167.665   | 170.160 / 167.665   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 286.338 / 247.366   | 286.338 / 247.366   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 804.481 / 816.593   | 948.804 / 956.843   | 144.323 / 140.250 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      |
| 5   | 805.305 / 812.136   | 939.029 / 952.986   | 133.724 / 140.851 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   |
| 6   | 953.781 / 963.683   | 1087.266 / 1108.473 | 133.485 / 144.789 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   |
| 7   | 1093.288 / 965.785  | 1251.222 / 1113.348 | 157.935 / 147.564 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    |
| 8   | 968.840 / 944.879   | 1111.087 / 1097.893 | 142.246 / 153.015 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    |
| 9   | 971.186 / 892.407   | 1146.171 / 1033.092 | 174.985 / 140.686 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   |
| 10  | 692.329 / 604.626   | 914.287 / 779.290   | 221.958 / 174.664 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   |
| 11  | 200.500 / 163.343   | 480.252 / 435.637   | 279.753 / 272.294 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    |
| 12  | 172.881 / 183.527   | 172.881 / 183.527   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 619.982 / 641.300   | 619.982 / 641.300   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          |
| 14  | 1273.888 / 1171.620 | 1361.411 / 1323.721 | 87.523 / 152.101  | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  |
| 15  | 945.154 / 896.172   | 634.137 / 558.987   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            |
| 16  | 789.230 / 702.990   | 608.752 / 611.687   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           |
| 17  | 831.971 / 771.931   | 680.701 / 595.296   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            |
| 18  | 757.557 / 751.162   | 620.771 / 619.652   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            |
| 19  | 758.886 / 727.324   | 624.111 / 556.316   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             |
| 20  | 807.760 / 901.838   | 1053.683 / 1078.028 | 245.923 / 176.190 | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} |
| 21  | 222.836 / 202.830   | 495.001 / 501.448   | 272.166 / 298.618 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              |
| 22  | 217.047 / 169.901   | 217.047 / 169.901   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 254.963 / 250.712   | 254.963 / 250.712   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 935.684 / 955.472   | 1118.329 / 1133.454 | 182.645 / 177.982 | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} |
| 25  | 804.197 / 1323.681  | 943.123 / 1457.896  | 138.926 / 134.215 | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} |
| 26  | 1514.576 / 763.246  | 1459.934 / 901.361  | 0 / 138.115       | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   |
| 27  | 527.127 / 559.830   | 774.192 / 819.338   | 247.065 / 259.509 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   |
| 28  | 798.736 / 849.139   | 887.620 / 938.197   | 88.884 / 89.058   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   |
| 29  | 1701.862 / 1762.502 | 1838.119 / 1850.173 | 136.257 / 87.671  | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} |
| 30  | 588.638 / 464.694   | 827.143 / 696.614   | 238.505 / 231.920 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    |
| 31  | 628.123 / 660.497   | 628.123 / 660.497   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           |
| 32  | 210.815 / 190.736   | 531.443 / 467.899   | 320.628 / 277.163 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             |
| 33  | 273.671 / 253.033   | 273.671 / 253.033   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 484.990 / 523.896    | 38.906   | 15 / 16           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 14                  | 0            | 761.174 / 772.572    | 11.398   | 19 / 19           | 0            |
| 5   | 13 / 14                  | 1            | 761.523 / 761.827    | 0.304    | 18 / 19           | 1            |
| 6   | 17 / 18                  | 1            | 909.742 / 915.181    | 5.440    | 22 / 23           | 1            |
| 7   | 18 / 17                  | -1           | 1044.523 / 917.597   | -126.927 | 23 / 22           | -1           |
| 8   | 17 / 17                  | 0            | 923.903 / 896.100    | -27.803  | 22 / 22           | 0            |
| 9   | 16 / 16                  | 0            | 917.460 / 848.555    | -68.905  | 21 / 21           | 0            |
| 10  | 10 / 9                   | -1           | 628.552 / 558.579    | -69.972  | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 198.659 / 162.582    | -36.077  | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 577.400 / 594.114    | 16.713   | 10 / 10           | 0            |
| 14  | 23 / 23                  | 0            | 1185.491 / 1120.356  | -65.134  | 28 / 28           | 0            |
| 15  | 12 / 12                  | 0            | 587.847 / 558.729    | -29.118  | 20 / 19           | -1           |
| 16  | 12 / 12                  | 0            | 564.512 / 568.091    | 3.579    | 17 / 16           | -1           |
| 17  | 12 / 12                  | 0            | 632.504 / 552.657    | -79.847  | 16 / 17           | 1            |
| 18  | 12 / 12                  | 0            | 576.864 / 576.083    | -0.780   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 581.254 / 555.956    | -25.297  | 16 / 16           | 0            |
| 20  | 16 / 16                  | 0            | 807.475 / 853.302    | 45.827   | 21 / 21           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 24  | 15 / 15                  | 0            | 889.114 / 912.054    | 22.940   | 20 / 20           | 0            |
| 25  | 13 / 23                  | 10           | 760.230 / 1279.758   | 519.528  | 18 / 28           | 10           |
| 26  | 23 / 12                  | -11          | 1232.033 / 675.179   | -556.854 | 28 / 17           | -11          |
| 27  | 10 / 10                  | 0            | 526.073 / 558.577    | 32.504   | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 711.446 / 762.689    | 51.243   | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1659.323 / 1677.335  | 18.013   | 34 / 34           | 0            |
| 30  | 10 / 9                   | -1           | 587.951 / 464.188    | -123.763 | 16 / 15           | -1           |
| 31  | 9 / 9                    | 0            | 584.095 / 617.707    | 33.612   | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C      | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 213.825 / 237.463   | 23.638   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 210.089 / 232.347   | 22.258   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.853 / 400.832   | 19.979   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 441.441 / 401.917   | -39.524  |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.860 / 401.944   | 0.084    |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 429.952 / 390.033   | -39.919  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 308.404 / 307.782   | -0.622   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 158.148 / 143.903   | -14.245  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 143.759 / 141.176   | -2.582   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 165.514 / 144.864   | -20.650  |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 155.220 / 144.350   | -10.870  |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 149.943 / 143.964   | -5.979   |
| 20  | 1 / 1                | 0     | 5 / 5              | 0     | 341.305 / 334.871   | -6.434   |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 210.712 / 395.523   | 184.812  |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 617.137 / 93.461    | -523.676 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 1054.946 / 1041.248 | -13.698  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0               | 0        |

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

### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 1

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

### renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z / nvidia / pass 2

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

## Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16894.5078125 / 16681.24609375 / -213.26171875 | 16521.3203125 / 16407.21484375 / -114.10546875 | 99.156                |
| nvidia | 1    | systemCommitMiB   | 55422.94921875 / 55060.37890625 / -362.5703125 | 54414.625 / 54313.40234375 / -101.22265625     | 261.348               |
| nvidia | 1    | dxgiUsageMiB      | 5583.1171875 / 3750.015625 / -1833.1015625     | 4201.6171875 / 3527.0078125 / -674.609375      | 1158.492              |
| nvidia | 1    | liveTextures      | 0 / 261 / 261                                  | 0 / 256 / 256                                  | -5                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2429.8961448669434 / 2429.8961448669434    | 0 / 2424.0259971618652 / 2424.0259971618652    | -5.870                |
| nvidia | 2    | processPrivateMiB | 17097.22265625 / 16617.34765625 / -479.875     | 16831.234375 / 16617.78515625 / -213.44921875  | 266.426               |
| nvidia | 2    | systemCommitMiB   | 55505.6015625 / 54815.92578125 / -689.67578125 | 54507.67578125 / 54547.8203125 / 40.14453125   | 729.820               |
| nvidia | 2    | dxgiUsageMiB      | 4066.75390625 / 3643.64453125 / -423.109375    | 3889.8828125 / 3566.79296875 / -323.08984375   | 100.020               |
| nvidia | 2    | liveTextures      | 0 / 211 / 211                                  | 0 / 233 / 233                                  | 22                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2274.9311332702637 / 2274.9311332702637    | 0 / 2349.4316596984863 / 2349.4316596984863    | 74.501                |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2168        | 2222        | 54         |
| cpu/compactPresentationContract/reuses                  | 2146        | 2200        | 54         |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 155         | 151         | -4         |
| cpu/generationResourceValidation/fullValidations        | 581         | 568         | -13        |
| cpu/generationResourceValidation/stableChecks           | 8236        | 8397        | 161        |
| cpu/generationResourceValidation/stableHits             | 8159        | 8316        | 157        |
| cpu/generationResourceValidation/stableMisses           | 77          | 81          | 4          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4149        | 4328        | 179        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4149        | 4328        | 179        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4107        | 4286        | 179        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4149        | 4328        | 179        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4128        | 4307        | 179        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4115        | 4294        | 179        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4054        | 4238        | 184        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 90          | -4         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4134        | 4313        | 179        |
| cpu/strongStereoPacket/captures                         | 4492        | 4628        | 136        |
| cpu/strongStereoPacket/commitAccepts                    | 4317        | 4424        | 107        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 66          | 1          |
| cpu/strongStereoPacket/commitValidations                | 4382        | 4490        | 108        |
| cpu/strongStereoPacket/cycleReuses                      | 2205        | 2275        | 70         |
| cpu/strongStereoPacket/fastSkips                        | 3806        | 4028        | 222        |
| cpu/strongStereoPacket/invalidations                    | 4706        | 4882        | 176        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 100         | -4         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2183        | 2253        | 70         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.867       | 1.855       | -0.012     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 22.700      | 68.100      | 45.400     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.147       | 0.014      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 11.900      | 10.300     |
| cpu/window/currentFrame                                 | 53015       | 18480       | -34535     |
| cpu/window/elapsedFrames                                | 4148        | 4328        | 180        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 48867       | 14152       | -34715     |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 53017       | 18482       | -34535     |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5974703856  | 6230998224  | 256294368  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4709        | 4911        | 202        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11961613440 | 12474725760 | 513112320  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.297       | 0.306       | 0.009      |
| gpu/item5ActiveFSRCopies/activePixels                   | 11119322840 | 12047940400 | 928617560  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26297233960 | 27299138000 | 1001904040 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14730       | 15490       | 760        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1971        | 2045        | 74         |
| gpu/item7EarlyHAM/executedClears                        | 1974        | 2052        | 78         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1974        | 2052        | 78         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4104        | 4256        | 152        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4150        | 4330        | 180        |
| gpu/startFrame                                          | 48867       | 14152       | -34715     |
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
| texture/createdCount                                    | 3962        | 3974        | 12         |
| texture/createdEstimatedBytes                           | 38815224912 | 38786366328 | -28858584  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3701        | 3718        | 17         |
| texture/destroyedEstimatedBytes                         | 36267294132 | 36244590844 | -22703288  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 886         | 896         | 10         |
| texture/liveTextureRecordCount                          | 261         | 256         | -5         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 50          | 47          | -3         |
| texture/niSourceTextureMatchedEstimatedBytes            | 172279000   | 166123904   | -6155096   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1536        | 1473        | -63        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 261         | 256         | -5         |
| texture/outstandingEstimatedBytes                       | 2547930780  | 2541775484  | -6155296   |
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
| cpu/compactPresentationContract/publishes               | 2143        | 2189        | 46        |
| cpu/compactPresentationContract/reuses                  | 2121        | 2167        | 46        |
| cpu/devBenchOnly                                        | true        | true        | n/a       |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0         |
| cpu/generationResourceValidation/contractPublishes      | 156         | 156         | 0         |
| cpu/generationResourceValidation/fullValidations        | 570         | 575         | 5         |
| cpu/generationResourceValidation/stableChecks           | 8160        | 8303        | 143       |
| cpu/generationResourceValidation/stableHits             | 8083        | 8226        | 143       |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0         |
| cpu/schemaVersion                                       | 1           | 1           | 0         |
| cpu/sessionId                                           | 2           | 2           | 0         |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0         |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4106        | 4221        | 115       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0         |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4106        | 4221        | 115       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4064        | 4179        | 115       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0         |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4106        | 4221        | 115       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0         |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4085        | 4200        | 115       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0         |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0         |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4072        | 4187        | 115       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4017        | 4130        | 113       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 90          | 0         |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0         |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4091        | 4206        | 115       |
| cpu/strongStereoPacket/captures                         | 4444        | 4548        | 104       |
| cpu/strongStereoPacket/commitAccepts                    | 4266        | 4359        | 93        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 65          | -1        |
| cpu/strongStereoPacket/commitValidations                | 4332        | 4424        | 92        |
| cpu/strongStereoPacket/cycleReuses                      | 2180        | 2233        | 53        |
| cpu/strongStereoPacket/fastSkips                        | 3768        | 3894        | 126       |
| cpu/strongStereoPacket/invalidations                    | 4653        | 4767        | 114       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 103         | -1        |
| cpu/strongStereoPacket/lifetimeReuses                   | 2160        | 2212        | 52        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.869       | 1.879       | 0.009     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 36.700      | 25.400      | -11.300   |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.143       | 0.010     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.500       | 1.600       | 0.100     |
| cpu/window/currentFrame                                 | 57516       | 23112       | -34404    |
| cpu/window/elapsedFrames                                | 4107        | 4220        | 113       |
| cpu/window/initialized                                  | true        | true        | n/a       |
| cpu/window/startFrame                                   | 53409       | 18892       | -34517    |
| gpu/active                                              | false       | false       | n/a       |
| gpu/currentFrame                                        | 57516       | 23113       | -34403    |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0         |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5901114384  | 6071131440  | 170017056 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4651        | 4785        | 134       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11814284160 | 12154665600 | 340381440 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0         |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.309       | 0.002     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11542473520 | 11865705520 | 323232000 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26077296080 | 26516112080 | 438816000 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14810       | 15110       | 300       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0         |
| gpu/item7EarlyHAM/directOutputSkips                     | 1955        | 1991        | 36        |
| gpu/item7EarlyHAM/executedClears                        | 1948        | 2016        | 68        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1948        | 2016        | 68        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4064        | 4168        | 104       |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0         |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0         |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0         |
| gpu/observedFrames                                      | 4107        | 4221        | 114       |
| gpu/startFrame                                          | 53409       | 18892       | -34517    |
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
| texture/createdCount                                    | 3914        | 3968        | 54        |
| texture/createdEstimatedBytes                           | 38675267808 | 38826844112 | 151576304 |
| texture/currentCohort                                   | 0           | 0           | 0         |
| texture/destroyedCount                                  | 3703        | 3735        | 32        |
| texture/destroyedEstimatedBytes                         | 36289829620 | 36363286460 | 73456840  |
| texture/droppedTextureRecords                           | 0           | 0           | 0         |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0         |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0         |
| texture/groupCount                                      | 863         | 887         | 24        |
| texture/liveTextureRecordCount                          | 211         | 233         | 22        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0         |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0         |
| texture/niSourceTextureMatchedCount                     | 4           | 26          | 22        |
| texture/niSourceTextureMatchedEstimatedBytes            | 9786808     | 87906272    | 78119464  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0         |
| texture/niSourceTextureResourceCount                    | 1509        | 1506        | -3        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a       |
| texture/outstandingCount                                | 211         | 233         | 22        |
| texture/outstandingEstimatedBytes                       | 2385438188  | 2463557652  | 78119464  |
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
| toolchain                | false  |
| dependencies             | true   |
| shaderCompiler           | true   |

Full start/end memory evidence, CPU/GPU/resource counters, phase timings, retry details, native execution proofs, every gate and raw receipt hash are in comparison.json. comparison.csv contains every paired or missing transition with both original row payloads. Missing samples remain null/n/a. Current and baseline failures are preserved separately; a persistent gate failure is not automatically a newly introduced regression. The fixed stretch-frame cutoff is diagnostic only because settling imposes stretch. Compare its actual frames and duration, together with time to applied relatch and strict completion. Request-to-applied frames begin at the producer request, whereas strict frames/ms begin at qualification dispatch; these intervals must not be conflated.

-   Headset refresh, driver version, power state and full modlist/cache equality require retained proof.
-   One process and ordered repeats do not establish causal or statistically significant gains.
-   Missing profiler samples are unavailable time evidence, never zero cost.

# renderscale-tuning-nvidia final report

-   Assay execution: **COMPLETE**
-   Transitions dispatched: **66/66**
-   Terminal render verdict: **PASS** (terminal condition only)
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
| nvidia | 1    | 33   | 33/0               | 793.336        | 1389.267 | 1856.757 | 15.091             | 735.095         | 13.400              | 17               | 69             | 4143.532   | 9       | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 807.126        | 1444.193 | 1932.147 | 15.303             | 747.347         | 13.760              | 18               | 78             | 4955.678   | 10      | 0        | 0                   | MET             | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":18078,"leftPath":"NativeOriginal","referenceFrame":18480,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":22718,"leftPath":"NativeOriginal","referenceFrame":23111,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |     16521.32 |  16407.215 |     -114.105 |      16746.605 |    16814.719 |         68.113 |    16831.234 |  16617.785 |     -213.449 |                   n.d. |
| System commit MiB                  |    54414.625 |  54313.402 |     -101.223 |      54675.047 |    54435.898 |       -239.148 |    54507.676 |   54547.82 |       40.145 |                   n.d. |
| DXGI process usage MiB             |     4201.617 |   3527.008 |     -674.609 |        3784.57 |     3849.195 |         64.625 |     3889.883 |   3566.793 |      -323.09 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        256 |          256 |            256 |          256 |              0 |            0 |        233 |          233 |                   0.91 |
| Estimated live tracked texture MiB |            0 |   2424.026 |     2424.026 |       2424.026 |     2424.026 |              0 |            0 |   2349.432 |     2349.432 |                  0.969 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -114.10546875,
        "systemCommitMiB": -101.22265625,
        "dxgiUsageMiB": -674.609375,
        "liveTextures": 256,
        "liveTextureMiB": 2424.0259971618652
    },
    "pass2": {
        "processPrivateMiB": -213.44921875,
        "systemCommitMiB": 40.14453125,
        "dxgiUsageMiB": -323.08984375,
        "liveTextures": 233,
        "liveTextureMiB": 2349.4316596984863
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

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | 1              | 14164/501.6291 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 101.782 ms                     | dlss           | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 14568/789.4461 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 109.131 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 14706/945.179 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 47.937 ms; SubmitStageFoveatedCenter: 47.620 ms | 207.484 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 14849/877.5615 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.848 ms; SubmitStageFoveatedCenter: 1.730 ms   | 267.937 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 14989/1092.6138 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.338 ms; SubmitStageFoveatedCenter: 53.305 ms | 209.720 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 15129/1048.3668 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.050 ms; SubmitStageFoveatedCenter: 53.009 ms | 212.983 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 15266/1031.8432 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 6              | 15927/1222.4424 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 16067/903.6503 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 16198/731.8376 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 16331/719.4367 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 16462/760.8656 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 16596/781.3588 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 266.842 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 17391/1645.7425 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 2              | 17527/803.4593 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 262.082 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 17946/1218.3748 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 121.971 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 19280/816.5931 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 107.903 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 2              | 19416/812.1357 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 49.750 ms; SubmitStageFoveatedCenter: 49.681 ms | 231.184 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 19556/963.6832 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 67.508 ms; SubmitStageFoveatedCenter: 67.481 ms | 229.426 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 19696/965.7846 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.737 ms; SubmitStageFoveatedCenter: 53.659 ms | 235.781 ms                     | dlss           | PASS   | stable    | PASS   | 67 records / 3 pages | 6              | 19836/944.8785 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.345 ms; SubmitStageFoveatedCenter: 53.300 ms | 211.131 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 19975/892.4067 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 6              | 20627/1171.6196 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 20766/896.1723 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 20896/702.9905 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 21023/771.9312 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 21152/751.1622 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 21283/727.3236 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 5              | 21417/901.8381 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 277.923 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 22058/1323.6814 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 2              | 22190/763.2461 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 290.954 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 22593/1762.5021 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

## Presentation stretch anomalies

| Lane   | Pass | Row | From                                                      | To                                                                           | Consecutive frames | Recovered PASS |
| ------ | ---: | --: | --------------------------------------------------------- | ---------------------------------------------------------------------------- | -----------------: | -------------- |
| nvidia |    1 |   1 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"method":"none","qualityMode":0,"renderScaleMode":false}                    |                  1 | yes            |
| nvidia |    1 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    1 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}   |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true}  |                  3 | yes            |
| nvidia |    2 |  20 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":0,"renderScaleMode":false} |                  5 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}   |                  6 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}   |        not_exposed | yes            |
