# NVIDIA transition comparison: 10 versus 9 September 2026

This is a historical comparison. At the user's subsequent request, the five
September 9 run columns were removed from the canonical ledger; today's run
is its only September 9-10 entry. These tables retain the original evidence
comparison, including the ledger validation performed before that removal.

Current run **N** measured current `main-VR` HEAD
`348803c1831c8cd71ceb75a58143ad0bcb00abb2`. Its enabled AIO DLL and manifest were
verified against the runtime producer and archive. Runs D and E used
the same source commit and Build ID. Both current passes completed all
33 transitions with valid pacing and all render results PASS.

Timings come from retained producer receipts and are cross-checked
against the existing [canonical comparison ledger](vr-render-scale-ledger.md).
They measure qualification dispatch to strict terminal completion,
including retries and excluding the 5,000 ms pre-dispatch pacing wait.
Pass totals below sum measured transition latencies; they are not pass
wall-clock durations and exclude pacing, baseline setup, and capture work.

| Code | Date   | Run                               | Source commit     | Coverage / limitation                                                           |
| ---- | ------ | --------------------------------- | ----------------- | ------------------------------------------------------------------------------- |
| A    | Sep 9  | `nvidia-mtu2u3dm`                 | `f2a73cd95` dirty | 66 completed live; 57 receipts retained; pass 2 rows 25-33 missing              |
| B    | Sep 9  | `nvidia-mtu6cpo8`                 | `89e396458`       | Pass 1 complete; pass 2 baseline timed out; no measured pass 2 rows             |
| C    | Sep 9  | `nv-mtu8nhph`                     | `89e396458`       | Pass 1 complete; pass 2 stopped after row 9                                     |
| D    | Sep 9  | `nv-mtud89dr-combined`            | `348803c18`       | Pass 2 interrupted after row 8, continued in the same process with new captures |
| E    | Sep 9  | `nv-mtuivutb`                     | `348803c18`       | Pass 1 complete; pass 2 stopped after row 6; cadence unqualified                |
| N    | Sep 10 | `nvidia-2026-09-10T07-19-50-405Z` | `348803c18`       | Both 33-row passes uninterrupted; valid pacing                                  |

D pass 2 combines two retained segments in one game process. E pass 2
contains only six measured rows. B was pressure affected; its pass-2
baseline failed after 20,001.5645 ms before any measured transition.
A pass-2 rows 25-33 completed live but their timing receipts are missing.
C and E later pass-2 transitions were not run. None of these gaps is zero.

D and N share source and Build ID, but their recorded shader compiler
identity differs: D used `d3dcompiler_47.dll:10.0.26100.9168`, while
E and N used `10.0.26100.9444`. Runner continuity and environmental
conditions also differ. Lower observed transition times do not establish
a causal code-performance improvement.

## Per-pass totals and means

| Run | Pass | Measured rows | Total transition time (s) | Mean (ms) |  Max (ms) | Retry events / affected transitions |
| --- | ---: | ------------: | ------------------------: | --------: | --------: | ----------------------------------: |
| A   |    1 |         33/33 |                    25.978 |   787.217 |  1447.129 |                               8 / 8 |
| A   |    2 |         24/33 |                    18.101 |   754.196 |  1335.828 |                               8 / 8 |
| B   |    1 |         33/33 |                    84.786 |  2569.260 | 16264.205 |                             21 / 11 |
| B   |    2 |          0/33 |                       n/a |       n/a |       n/a |                                 n/a |
| C   |    1 |         33/33 |                    26.230 |   794.846 |  1631.113 |                               9 / 9 |
| C   |    2 |          9/33 |                     8.087 |   898.537 |  1291.902 |                               4 / 4 |
| D   |    1 |         33/33 |                    26.149 |   792.385 |  1611.836 |                               8 / 8 |
| D   |    2 |         33/33 |                    27.495 |   833.175 |  2017.373 |                               9 / 8 |
| E   |    1 |         33/33 |                    26.804 |   812.230 |  1621.095 |                             11 / 11 |
| E   |    2 |          6/33 |                     4.678 |   779.620 |  1238.096 |                               1 / 1 |
| N   |    1 |         33/33 |                    25.319 |   767.246 |  1392.319 |                               8 / 8 |
| N   |    2 |         33/33 |                    24.107 |   730.514 |  1658.352 |                              10 / 9 |

## Current run versus each previous run

Each comparison uses exactly the same pass and transition ordinals.
Negative change means lower observed latency in the current run.

| Previous run | Pass | Matched rows | Previous mean (ms) | Current matched mean (ms) | Current change |
| ------------ | ---: | -----------: | -----------------: | ------------------------: | -------------: |
| A            |    1 |           33 |            787.217 |                   767.246 |         -2.54% |
| A            |    2 |           24 |            754.196 |                   682.792 |         -9.47% |
| B            |    1 |           33 |           2569.260 |                   767.246 |        -70.14% |
| C            |    1 |           33 |            794.846 |                   767.246 |         -3.47% |
| C            |    2 |            9 |            898.537 |                   822.951 |         -8.41% |
| D            |    1 |           33 |            792.385 |                   767.246 |         -3.17% |
| D            |    2 |           33 |            833.175 |                   730.514 |        -12.32% |
| E            |    1 |           33 |            812.230 |                   767.246 |         -5.54% |
| E            |    2 |            6 |            779.620 |                   670.684 |        -13.97% |

## Reading the transition tables

Every measured cell is **strict milliseconds / retry count / recovery
frames**. Retry count 0 explicitly means no retry was recorded for that
transition. Recovery frames span the first matching Retry event to the
controller Stable event, including subsequent work; they are not an
isolated retry-backoff duration or the full strict-completion interval.
Counts refer to internal producer retries, not repeated public API calls.

`-` = no retry, `?` = recovery interval unavailable,
`M` = missing timing receipt, `NR` = transition not run.
UQ = Ultra Quality, Q = Quality, Bal = Balanced, Perf = Performance,
UP = Ultra Performance, AA = anti-aliasing. Hoshipa retains the profile
name recorded by this protocol. All routes match the current matrix.

## Pass 1: all transitions

|   # | Transition                  |                 A |                   B |                 C |                 D |                 E |         N current |
| --: | --------------------------- | ----------------: | ------------------: | ----------------: | ----------------: | ----------------: | ----------------: |
|   1 | DLSS Hoshipa → NONE         |   693.477 / 0 / - |     724.601 / 0 / - |   682.315 / 0 / - |   708.479 / 0 / - |   688.362 / 0 / - |   669.500 / 0 / - |
|   2 | NONE → TAA                  |   160.716 / 0 / - |     170.679 / 0 / - |   171.500 / 0 / - |   164.575 / 0 / - |   155.925 / 0 / - |   181.499 / 0 / - |
|   3 | TAA → DLAA                  |   252.047 / 0 / - |     266.449 / 0 / - |   288.364 / 0 / - |   254.146 / 0 / - |   273.260 / 0 / - |   289.827 / 0 / - |
|   4 | DLAA → DLSS Hoshipa         |   929.545 / 0 / - |     984.174 / 0 / - |   921.272 / 0 / - |   955.023 / 0 / - |   889.733 / 0 / - |   920.565 / 0 / - |
|   5 | DLSS Hoshipa → DLSS UQ      |  1165.858 / 0 / - |    1172.372 / 0 / - |  1197.640 / 0 / - |  1125.421 / 0 / - |  1143.557 / 0 / - |   981.793 / 0 / - |
|   6 | DLSS UQ → DLSS Q            |  1129.215 / 1 / 8 |    1169.762 / 1 / 8 |  1080.805 / 1 / 8 |  1114.571 / 1 / 8 |  1050.519 / 1 / 8 |  1099.691 / 1 / 8 |
|   7 | DLSS Q → DLSS Bal           |  1311.420 / 1 / 8 |    1481.087 / 1 / 8 |  1293.209 / 1 / 8 |  1274.819 / 1 / 8 |  1267.870 / 1 / 8 |  1392.319 / 1 / 8 |
|   8 | DLSS Bal → DLSS Perf        |  1341.494 / 1 / 8 |    1431.501 / 1 / 8 |  1267.085 / 1 / 8 |  1295.005 / 1 / 8 |  1238.290 / 1 / 8 |  1343.752 / 1 / 8 |
|   9 | DLSS Perf → DLSS UP         |  1262.846 / 1 / 8 |    1391.591 / 1 / 8 |  1235.690 / 1 / 8 |  1249.917 / 1 / 8 |  1224.421 / 1 / 8 |  1221.973 / 1 / 8 |
|  10 | DLSS UP → DLAA              |   777.340 / 0 / - |     785.256 / 0 / - |   726.891 / 0 / - |   795.424 / 0 / - |   756.672 / 0 / - |   740.453 / 0 / - |
|  11 | DLAA → TAA                  |   427.240 / 0 / - |     441.566 / 0 / - |   425.306 / 0 / - |   425.442 / 0 / - |   411.271 / 0 / - |   486.216 / 0 / - |
|  12 | TAA → NONE                  |   163.985 / 0 / - |     176.340 / 0 / - |   163.752 / 0 / - |   166.854 / 0 / - |   171.948 / 0 / - |   155.001 / 0 / - |
|  13 | NONE → FSR3 AA              |  1386.810 / 0 / - |     764.332 / 0 / - |   731.542 / 0 / - |   769.830 / 0 / - |   745.356 / 0 / - |   937.889 / 0 / - |
|  14 | FSR3 AA → FSR3 Hoshipa      |   865.969 / 0 / - |     913.319 / 0 / - | 1293.740 / 1 / 14 | 1350.073 / 1 / 14 | 1259.578 / 1 / 14 | 1339.594 / 1 / 14 |
|  15 | FSR3 Hoshipa → FSR3 UQ      |   819.370 / 0 / - |     712.979 / 0 / - |   758.565 / 0 / - |   860.145 / 0 / - | 1148.082 / 1 / 13 |   802.999 / 0 / - |
|  16 | FSR3 UQ → FSR3 Q            |   806.393 / 0 / - |  7728.934 / 2 / 133 | 1198.690 / 1 / 13 |   813.904 / 0 / - |   684.405 / 0 / - |   803.849 / 0 / - |
|  17 | FSR3 Q → FSR3 Bal           |   713.672 / 0 / - |  8404.336 / 3 / 139 |   721.320 / 0 / - |   714.526 / 0 / - |   711.471 / 0 / - |   732.811 / 0 / - |
|  18 | FSR3 Bal → FSR3 Perf        |   712.746 / 0 / - |  8012.362 / 2 / 133 |   703.443 / 0 / - |   705.352 / 0 / - |   765.275 / 0 / - |   840.052 / 0 / - |
|  19 | FSR3 Perf → FSR3 UP         |   724.159 / 0 / - |   1198.697 / 1 / 13 |   714.789 / 0 / - |   697.310 / 0 / - | 1137.136 / 1 / 13 |   708.567 / 0 / - |
|  20 | FSR3 UP → FSR3 AA           |  1005.280 / 1 / 7 |     728.326 / 0 / - |   682.963 / 0 / - |   681.413 / 0 / - |   987.088 / 1 / 7 |   655.757 / 0 / - |
|  21 | FSR3 AA → TAA               |   422.430 / 0 / - |     482.464 / 0 / - |   478.739 / 0 / - |   445.091 / 0 / - |   434.899 / 0 / - |   475.970 / 0 / - |
|  22 | TAA → NONE                  |   181.514 / 0 / - |     178.667 / 0 / - |   160.060 / 0 / - |   164.368 / 0 / - |   166.599 / 0 / - |   174.895 / 0 / - |
|  23 | NONE → DLAA                 |   240.211 / 0 / - |     272.785 / 0 / - |   239.281 / 0 / - |   240.933 / 0 / - |   243.626 / 0 / - |   270.911 / 0 / - |
|  24 | DLAA → FSR3 AA              |   733.775 / 0 / - |     885.426 / 0 / - |   827.506 / 0 / - |   927.783 / 0 / - |  1151.555 / 1 / 7 |  1024.970 / 1 / 7 |
|  25 | FSR3 AA → DLSS Hoshipa      | 1447.129 / 1 / 14 |  8989.652 / 3 / 140 | 1424.698 / 1 / 14 | 1416.690 / 1 / 14 | 1621.095 / 1 / 14 |   934.134 / 0 / - |
|  26 | DLSS Hoshipa → FSR3 Hoshipa | 1368.140 / 1 / 14 |     990.474 / 0 / - | 1631.113 / 1 / 14 | 1611.836 / 1 / 14 | 1541.825 / 1 / 14 | 1378.302 / 1 / 14 |
|  27 | FSR3 Hoshipa → NONE         |   702.388 / 0 / - |     854.050 / 0 / - |   692.409 / 0 / - |   758.883 / 0 / - |   753.030 / 0 / - |   665.093 / 0 / - |
|  28 | NONE → FSR3 UP              |   864.806 / 0 / - |    1037.740 / 0 / - |   847.281 / 0 / - |   851.990 / 0 / - |   949.293 / 0 / - |   841.685 / 0 / - |
|  29 | FSR3 UP → DLSS UP           | 1429.611 / 1 / 14 | 16264.205 / 4 / 260 | 1440.121 / 1 / 14 | 1403.986 / 1 / 14 |  1071.868 / 0 / - | 1387.084 / 1 / 14 |
|  30 | DLSS UP → TAA               |   684.849 / 0 / - |     882.064 / 0 / - |   763.040 / 0 / - |   779.119 / 0 / - |   708.917 / 0 / - |   644.207 / 0 / - |
|  31 | TAA → FSR3 AA               |   588.876 / 0 / - | 14511.810 / 2 / 242 |   663.543 / 0 / - |   672.716 / 0 / - |   692.304 / 0 / - |   562.726 / 0 / - |
|  32 | FSR3 AA → NONE              |   407.674 / 0 / - |     488.787 / 0 / - |   515.332 / 0 / - |   457.252 / 0 / - |   476.137 / 0 / - |   417.950 / 0 / - |
|  33 | NONE → DLAA                 |   257.162 / 0 / - |     288.779 / 0 / - |   287.919 / 0 / - |   295.825 / 0 / - |   282.237 / 0 / - |   237.079 / 0 / - |

## Pass 2: all transitions

|   # | Transition                  |                A |   B |                C |                 D |                E |         N current |
| --: | --------------------------- | ---------------: | --: | ---------------: | ----------------: | ---------------: | ----------------: |
|   1 | DLSS Hoshipa → NONE         |  672.609 / 0 / - |  NR |  688.314 / 0 / - |   723.715 / 0 / - |  735.106 / 0 / - |   734.886 / 0 / - |
|   2 | NONE → TAA                  |  159.893 / 0 / - |  NR |  189.553 / 0 / - |   187.440 / 0 / - |  189.373 / 0 / - |   177.782 / 0 / - |
|   3 | TAA → DLAA                  |  247.240 / 0 / - |  NR |  276.546 / 0 / - |   295.701 / 0 / - |  338.329 / 0 / - |   260.525 / 0 / - |
|   4 | DLAA → DLSS Hoshipa         |  959.898 / 0 / - |  NR | 1090.545 / 0 / - |  1067.193 / 0 / - | 1113.928 / 0 / - |   906.681 / 0 / - |
|   5 | DLSS Hoshipa → DLSS UQ      |  987.578 / 0 / - |  NR | 1010.562 / 0 / - |  1126.054 / 0 / - | 1062.885 / 0 / - |   904.298 / 0 / - |
|   6 | DLSS UQ → DLSS Q            | 1162.545 / 1 / ? |  NR | 1190.924 / 1 / 8 |  1257.694 / 1 / 8 | 1238.096 / 1 / 8 |  1039.935 / 1 / 8 |
|   7 | DLSS Q → DLSS Bal           | 1133.710 / 1 / ? |  NR | 1291.902 / 1 / 8 |  1258.464 / 1 / 8 |               NR |  1064.043 / 1 / 8 |
|   8 | DLSS Bal → DLSS Perf        | 1166.929 / 1 / ? |  NR | 1174.683 / 1 / 8 |  1243.175 / 1 / 8 |               NR |  1205.586 / 1 / 8 |
|   9 | DLSS Perf → DLSS UP         | 1178.773 / 1 / ? |  NR | 1173.803 / 1 / 8 |  1181.577 / 1 / 8 |               NR |  1112.825 / 1 / 8 |
|  10 | DLSS UP → DLAA              |  812.760 / 0 / - |  NR |               NR |   882.238 / 0 / - |               NR |   729.996 / 0 / - |
|  11 | DLAA → TAA                  |  388.229 / 0 / - |  NR |               NR |   448.197 / 0 / - |               NR |   431.684 / 0 / - |
|  12 | TAA → NONE                  |  181.004 / 0 / - |  NR |               NR |   175.020 / 0 / - |               NR |   154.506 / 0 / - |
|  13 | NONE → FSR3 AA              |  603.329 / 0 / - |  NR |               NR |   605.759 / 0 / - |               NR |   567.794 / 0 / - |
|  14 | FSR3 AA → FSR3 Hoshipa      | 1335.828 / 1 / ? |  NR |               NR |   907.253 / 0 / - |               NR | 1214.111 / 1 / 14 |
|  15 | FSR3 Hoshipa → FSR3 UQ      | 1265.105 / 1 / ? |  NR |               NR |   825.927 / 0 / - |               NR |   722.015 / 0 / - |
|  16 | FSR3 UQ → FSR3 Q            |  817.618 / 0 / - |  NR |               NR |   785.617 / 0 / - |               NR |   623.909 / 0 / - |
|  17 | FSR3 Q → FSR3 Bal           |  739.931 / 0 / - |  NR |               NR |   837.431 / 0 / - |               NR |   699.393 / 0 / - |
|  18 | FSR3 Bal → FSR3 Perf        |  679.871 / 0 / - |  NR |               NR |   871.073 / 0 / - |               NR |   670.129 / 0 / - |
|  19 | FSR3 Perf → FSR3 UP         |  688.160 / 0 / - |  NR |               NR |   832.884 / 0 / - |               NR |   698.477 / 0 / - |
|  20 | FSR3 UP → FSR3 AA           |  986.746 / 1 / ? |  NR |               NR |   781.036 / 0 / - |               NR |   643.825 / 0 / - |
|  21 | FSR3 AA → TAA               |  454.930 / 0 / - |  NR |               NR |   427.542 / 0 / - |               NR |   400.298 / 0 / - |
|  22 | TAA → NONE                  |  174.971 / 0 / - |  NR |               NR |   196.538 / 0 / - |               NR |   154.918 / 0 / - |
|  23 | NONE → DLAA                 |  250.677 / 0 / - |  NR |               NR |   280.087 / 0 / - |               NR |   230.219 / 0 / - |
|  24 | DLAA → FSR3 AA              | 1052.376 / 1 / ? |  NR |               NR |  1206.826 / 1 / 7 |               NR |  1039.184 / 1 / 7 |
|  25 | FSR3 AA → DLSS Hoshipa      |                M |  NR |               NR | 2017.373 / 2 / 20 |               NR | 1658.352 / 2 / 20 |
|  26 | DLSS Hoshipa → FSR3 Hoshipa |                M |  NR |               NR | 1577.395 / 1 / 14 |               NR | 1227.931 / 1 / 14 |
|  27 | FSR3 Hoshipa → NONE         |                M |  NR |               NR |   808.171 / 0 / - |               NR |   696.952 / 0 / - |
|  28 | NONE → FSR3 UP              |                M |  NR |               NR |   991.194 / 0 / - |               NR |   842.921 / 0 / - |
|  29 | FSR3 UP → DLSS UP           |                M |  NR |               NR | 1556.923 / 1 / 14 |               NR | 1330.059 / 1 / 14 |
|  30 | DLSS UP → TAA               |                M |  NR |               NR |   748.961 / 0 / - |               NR |   618.367 / 0 / - |
|  31 | TAA → FSR3 AA               |                M |  NR |               NR |   657.636 / 0 / - |               NR |   697.904 / 0 / - |
|  32 | FSR3 AA → NONE              |                M |  NR |               NR |   456.322 / 0 / - |               NR |   412.420 / 0 / - |
|  33 | NONE → DLAA                 |                M |  NR |               NR |   276.371 / 0 / - |               NR |   235.021 / 0 / - |

## Current retry-associated waiting evidence

All 18 current retry occurrences are classified Backend, affecting
eight pass-1 transitions and nine pass-2 transitions. Pass 2 transition
25 retried twice. Every transition ultimately passed.

**Isolated retry milliseconds are unavailable.** Stress Retry events
retain frames and occurrence counts, without per-event QPC timestamps.
Repeated events may be coalesced. Frame spans therefore cannot be
converted to exact milliseconds by assuming a frame rate.

The blocked window below is a separate measured QPC interval from the
first observed blocked-pre-mutation state to first physical mutation.
It can include admission or backend lifecycle waiting and is already
included in strict latency. Even when its boundary frames align with
Retry and Applied events, it does not isolate each retry duration.
Unavailable blocked windows do not mean zero retry time.

| Pass |   # | Retry count | First Retry → Applied (frames) | First Retry → Stable (frames) | Observed blocked window (ms) | Blocked boundary frames align with Retry / Applied |
| ---: | --: | ----------: | -----------------------------: | ----------------------------: | ---------------------------: | -------------------------------------------------- |
|    1 |   6 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    1 |   7 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    1 |   8 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    1 |   9 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    1 |  14 |           1 |                              6 |                            14 |                      269.730 | no                                                 |
|    1 |  24 |           1 |                              6 |                             7 |                      256.054 | no                                                 |
|    1 |  26 |           1 |                              6 |                            14 |                      257.067 | yes                                                |
|    1 |  29 |           1 |                              6 |                            14 |                          n/a | n/a                                                |
|    2 |   6 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    2 |   7 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    2 |   8 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    2 |   9 |           1 |                              0 |                             8 |                          n/a | n/a                                                |
|    2 |  14 |           1 |                              6 |                            14 |                      253.132 | no                                                 |
|    2 |  24 |           1 |                              6 |                             7 |                      253.735 | no                                                 |
|    2 |  25 |           2 |                             12 |                            20 |                      592.203 | yes                                                |
|    2 |  26 |           1 |                              6 |                            14 |                      250.725 | yes                                                |
|    2 |  29 |           1 |                              6 |                            14 |                          n/a | n/a                                                |

Feedback recorded for timestamped per-retry intervals:
`AUTO-20260910-080419216-92710F95`. No runtime mutations were made for this
comparison. The worker was subsequently recovered at 08:19:19 UTC: it
published COMPLETE, exited, and released its own lock. Measurements and the
journal remain unchanged; reporting completeness is independent of shutdown.

## Data and validation

-   [Current run summary](nvidia-renderscale-tuning-20260910.md).
-   [Canonical timing ledger](vr-render-scale-ledger.md).
-   [Interactive local comparison](../../artifacts/renderscale-comparison-20260910/comparison.html).
-   [Full-precision CSV](../../artifacts/renderscale-comparison-20260910/comparison.csv), including presentation, cleanup, retry counts, recovery intervals, and receipt paths.
-   [Structured data](../../artifacts/renderscale-comparison-20260910/comparison.json).
-   [Validation receipt](../../artifacts/renderscale-comparison-20260910/validation.json).

Offline validation matched all 303 measured strict timings exactly to
the canonical ledger and all 303 routes to the current matrix. Full
stress events independently confirmed retry counts for 279 rows without
event overwrites. A pass-2 counts come from its 24 retained waiter
receipts; full stress-event history was unavailable. All 396 planned
positions are represented, including 9 missing receipts and 84 not-run
transitions. The canonical ledger was not modified by this comparison.
Source receipt and event-file hashes are preserved locally.
