# PR73 NVIDIA tuning compared with measured PR66

Displayed millisecond values are rounded to two decimal places. Full
precision is retained in the comparison ledger and raw evidence.

<!-- pr73-vs-pr66-nvidia-comparison-v1 -->

### NVIDIA comparison: measured PR66 vs PR73

**PR73 averaged 870.90 ms per switch versus
800.20 ms for PR66: 70.67 ms higher
(8.83%).** Pass 1 changed +8.795% and pass 2
+8.867%. Both builds completed 66/66 transitions with terminal PASS,
Task 2 counts 66 PASS / 0 FAIL / 0 INCONCLUSIVE, and applicable health
MET in both passes. No fidelity or vendor-fallback failures were observed.

The user-selected reference is measured PR66 integration build
`a09e1cc77` on main-VR `bf4ae54a7`, the same run reported in PR66.
PR73 was measured at its exact published head `d9780bb74`, based on
main-VR `ef7c366dd`. PR66's published head remains `80f83d2dd`; its
description distinguishes that older binary from the measured integration
build. These different integration bases remain explicit, so the observed
difference cannot be attributed solely to the PR73 patch.

Formal improvement-or-neutral assessment: **INCONCLUSIVE**. Retained
scene and toolchain fingerprints differ, a complete matching fixture
fingerprint is unavailable, and no versioned tolerance policy was specified.
These are descriptive switch timings from one process per build, each
with two ordered 33-transition passes. Both PR73 pass means are higher;
no performance improvement or neutrality is established.

#### Main comparison

Each timing row first averages identical routes within each pass: 33 for
completion/presentation/cleanup and 25 applicable relatch boundaries.
Stretch rows use full-pass totals. SE is sample standard deviation across
the two pass summaries divided by sqrt(2); it describes within-run
variation and is neither a confidence interval nor a significance test.
Timings exclude the five-second pre-dispatch wait.

| Measurement                    | PR66 mean (2 passes) | PR73 mean (2 passes) |            Change | ±SE PR66 | ±SE PR73 |
| ------------------------------ | -------------------: | -------------------: | ----------------: | -------: | -------: |
| Switch completion (ms)         |               800.20 |               870.90 |    +70.67 (+8.8%) |    ±6.90 |    ±7.80 |
| Presentation ready (ms)        |               673.50 |               737.10 |    +63.56 (+9.4%) |    ±1.00 |    ±3.50 |
| Cleanup tail (ms)              |               102.40 |               108.20 |     +5.75 (+5.6%) |    ±6.00 |   ±10.40 |
| Relatch proof (ms)             |               741.20 |               811.10 |    +69.91 (+9.4%) |    ±6.10 |    ±3.20 |
| Switch completion (frames)     |                 15.2 |                 15.3 |    +0.091 (+0.6%) |     ±0.1 |     ±0.2 |
| Relatch proof (frames)         |                 13.6 |                 13.7 |    +0.120 (+0.9%) |     ±0.2 |     ±0.1 |
| Stretch episodes per pass      |                 17.5 |                 18.5 |    +1.000 (+5.7%) |     ±0.5 |     ±0.5 |
| Stretch frames per pass        |                 73.5 |                 85.5 |  +12.000 (+16.3%) |     ±4.5 |     ±2.5 |
| Stretch duration per pass (ms) |             4,549.60 |             5,744.20 | +1194.57 (+26.3%) |  ±406.10 |  ±347.30 |

#### Each pass remains visible

| Pass | PR66 strict mean ms | PR73 strict mean ms |  Change | PR66 / PR73 p95 ms | PR66 / PR73 maximum ms | Health PR66 / PR73 |
| ---- | ------------------: | ------------------: | ------: | ------------------ | ---------------------- | ------------------ |
| 1    |              793.34 |              863.11 | +8.795% | 1389.27 / 1571.08  | 1856.76 / 1978.49      | MET / MET          |
| 2    |              807.13 |              878.70 | +8.867% | 1444.19 / 1616.17  | 1932.15 / 2045.21      | MET / MET          |

The largest higher route mean is **row 26, DLSS Hoshipa -> FSR3 Hoshipa**:
917.13 -> 1636.11 ms
(+78.39%). Its pass deltas are
+654.50/+783.45 ms.
The largest lower route mean is row 25, FSR3 Native AA -> DLSS Hoshipa,
-320.67 ms (-18.86%).
Rows 1, 2, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 21, 26, 27, 28, 29, 30, 31, 32 are slower in both passes.

#### Health, retries and recovered stretch

Counts below cover both complete passes of each build. Producer retries
occur within a switch and are distinct from protocol recovery applies.

| Result                                   | PR66 integration build |       PR73 |
| ---------------------------------------- | ---------------------: | ---------: |
| Completed / terminal PASS                |                66 / 66 |    66 / 66 |
| Task 2 PASS / FAIL / INCONCLUSIVE        |             66 / 0 / 0 | 66 / 0 / 0 |
| Fidelity mismatch observations           |                      0 |          0 |
| Vendor-failure stretch-eye observations  |                      0 |          0 |
| Bounds-mismatch fallback observations    |                      0 |          0 |
| Producer retries (pass 1 / pass 2)       |                 9 / 10 |     10 / 9 |
| Selected stretch transitions / recovered |                32 / 32 |    31 / 31 |
| Separate recovery applies / replays      |                  0 / 0 |      0 / 0 |
| Device loss                              |                      0 |          0 |
| OOM                                      |                      0 |          0 |
| Producer terminal failures               |                      0 |          0 |
| Vendor-native qualification failures     |                      0 |          0 |
| Credible liveness timeouts               |                      0 |          0 |

No new applicable health finding is observed in either pass. Lifecycle,
memory-trim and retirement-fence failure counters are also zero.
All selected-stretch transitions recovered; selected-transition counts
and full-capture episode counts measure different windows.

| Pass | Stretch episodes PR66/PR73 | Stretch frames PR66/PR73 | Stretch ms PR66/PR73 | Duration change | Active tail PR66/PR73 |
| ---- | -------------------------- | ------------------------ | -------------------- | --------------- | --------------------- |
| 1    | 17 / 19                    | 69 / 83                  | 4143.53 / 5396.83    | +1253.30 ms     | False / False         |
| 2    | 18 / 18                    | 78 / 88                  | 4955.68 / 6091.52    | +1135.84 ms     | False / False         |

Stretch duration increased in both passes. Raw cumulative acceptance is
false in all four passes. The observed maximum completed stretch is six
frames in both PR66 passes and PR73 pass 1, and 13 in PR73 pass 2.
The fixed two-frame cutoff remains DIAGNOSTIC_ONLY because settling
imposes stretch. The scaled-presentation `VendorEvaluated` gate against
proven native-AA `NativeOriginal` output remains CONTRACT_MISMATCH.
Native both-eye proof supports that exception; other gates remain
applicable. Neither excluded gate changes the MET health result.
No active tail or incomplete stereo cycle remains at stop.

<details>
<summary>Every switch: two-pass mean and SE</summary>

Times are ms; negative change is lower. Each route uses two observations
per build. Per-pass values are preserved in the next table.

| Row | Switch                            | PR66 mean | PR73 mean |           Change | ±SE PR66 | ±SE PR73 |
| --- | --------------------------------- | --------: | --------: | ---------------: | -------: | -------: |
| 1   | DLSS Hoshipa -> NONE              |    722.40 |    787.40 |   +64.95 (+9.0%) |   ±42.20 |   ±15.50 |
| 2   | NONE -> TAA                       |    168.60 |    190.10 |  +21.53 (+12.8%) |    ±0.90 |    ±7.50 |
| 3   | TAA -> DLAA                       |    248.80 |    252.80 |    +4.02 (+1.6%) |    ±1.40 |    ±6.80 |
| 4   | DLAA -> DLSS Hoshipa              |    977.70 |  1,005.10 |   +27.45 (+2.8%) |   ±66.20 |   ±33.00 |
| 5   | DLSS Hoshipa -> DLSS UQ           |  1,094.60 |  1,110.60 |   +16.01 (+1.5%) |   ±56.40 |  ±138.30 |
| 6   | DLSS UQ -> DLSS Quality           |  1,140.80 |  1,204.80 |   +63.97 (+5.6%) |   ±55.60 |   ±52.60 |
| 7   | DLSS Quality -> DLSS Balanced     |  1,257.00 |  1,350.10 |   +93.16 (+7.4%) |   ±54.40 |   ±95.20 |
| 8   | DLSS Balanced -> DLSS Performance |  1,229.60 |  1,273.50 |   +43.88 (+3.6%) |   ±38.00 |   ±45.20 |
| 9   | DLSS Performance -> DLSS UP       |  1,190.30 |  1,347.90 | +157.62 (+13.2%) |   ±67.30 |   ±84.40 |
| 10  | DLSS UP -> DLAA                   |    770.10 |    860.10 |  +90.04 (+11.7%) |    ±9.20 |   ±97.80 |
| 11  | DLAA -> TAA                       |    449.40 |    488.80 |   +39.41 (+8.8%) |   ±13.80 |   ±13.90 |
| 12  | TAA -> NONE                       |    177.40 |    197.50 |  +20.08 (+11.3%) |    ±6.10 |   ±19.70 |
| 13  | NONE -> FSR3 Native AA            |    694.20 |    743.90 |   +49.65 (+7.2%) |   ±52.90 |  ±100.50 |
| 14  | FSR3 Native AA -> FSR3 Hoshipa    |  1,364.40 |  1,510.50 | +146.05 (+10.7%) |   ±13.50 |   ±43.10 |
| 15  | FSR3 Hoshipa -> FSR3 UQ           |    899.90 |    977.80 |   +77.87 (+8.7%) |    ±3.70 |  ±159.80 |
| 16  | FSR3 UQ -> FSR3 Quality           |    717.40 |    770.60 |   +53.14 (+7.4%) |   ±14.40 |   ±45.80 |
| 17  | FSR3 Quality -> FSR3 Balanced     |    745.70 |    847.50 | +101.84 (+13.7%) |   ±26.20 |   ±52.10 |
| 18  | FSR3 Balanced -> FSR3 Performance |    756.00 |  1,216.00 | +459.99 (+60.8%) |    ±4.90 |  ±354.40 |
| 19  | FSR3 Performance -> FSR3 UP       |    754.30 |    781.70 |   +27.37 (+3.6%) |   ±27.00 |   ±46.80 |
| 20  | FSR3 UP -> FSR3 Native AA         |    908.90 |    738.80 | -170.09 (-18.7%) |  ±169.10 |   ±11.80 |
| 21  | FSR3 Native AA -> TAA             |    482.50 |    512.50 |   +30.05 (+6.2%) |   ±19.00 |   ±35.50 |
| 22  | TAA -> NONE                       |    170.20 |    170.60 |    +0.41 (+0.2%) |    ±0.30 |    ±2.60 |
| 23  | NONE -> DLAA                      |    256.90 |    290.60 |  +33.64 (+13.1%) |    ±6.20 |   ±43.40 |
| 24  | DLAA -> FSR3 Native AA            |  1,159.10 |    991.80 | -167.22 (-14.4%) |   ±25.60 |  ±108.70 |
| 25  | FSR3 Native AA -> DLSS Hoshipa    |  1,700.20 |  1,379.50 | -320.67 (-18.9%) |  ±156.60 |  ±180.70 |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa      |    917.10 |  1,636.10 | +718.98 (+78.4%) |   ±15.80 |   ±48.70 |
| 27  | FSR3 Hoshipa -> NONE              |    785.50 |    824.90 |   +39.39 (+5.0%) |   ±33.80 |   ±54.70 |
| 28  | NONE -> FSR3 UP                   |    947.60 |  1,039.80 |   +92.19 (+9.7%) |   ±36.90 |   ±37.60 |
| 29  | FSR3 UP -> DLSS UP                |  1,689.40 |  2,011.80 | +322.43 (+19.1%) |  ±242.70 |   ±33.40 |
| 30  | DLSS UP -> TAA                    |    688.80 |    753.80 |   +64.99 (+9.4%) |    ±7.80 |   ±29.10 |
| 31  | TAA -> FSR3 Native AA             |    618.90 |    655.00 |   +36.10 (+5.8%) |   ±41.60 |    ±6.30 |
| 32  | FSR3 Native AA -> NONE            |    459.10 |    525.10 |  +66.01 (+14.4%) |    ±8.80 |   ±12.90 |
| 33  | NONE -> DLAA                      |    264.60 |    292.70 |  +28.01 (+10.6%) |   ±11.60 |   ±18.90 |

</details>

<details>
<summary>Every switch in each pass: strict completion and relatch</summary>

All cells are PR66 / PR73. Timings use the qualification-dispatch clock.
Relatch requires first exact new-generation proof; n/a is inapplicable or
missing, never zero. All terminal and Task 2 classifications are PASS.

| Row | P1 strict ms      | P1 change ms | P2 strict ms      | P2 change ms | P1 strict frames | P2 strict frames | P1 relatch ms     | P2 relatch ms     |
| --- | ----------------- | ------------ | ----------------- | ------------ | ---------------- | ---------------- | ----------------- | ----------------- |
| 1   | 680.25 / 802.94   | +122.69      | 764.65 / 771.85   | +7.21        | 15.000 / 15.000  | 16.000 / 14.000  | 451.45 / 548.76   | 523.90 / 518.90   |
| 2   | 169.47 / 182.56   | +13.10       | 167.67 / 197.62   | +29.96       | 4.000 / 3.000    | 4.000 / 4.000    | n/a / n/a         | n/a / n/a         |
| 3   | 250.23 / 245.99   | -4.23        | 247.37 / 259.65   | +12.28       | 3.000 / 3.000    | 3.000 / 3.000    | n/a / n/a         | n/a / n/a         |
| 4   | 911.53 / 972.18   | +60.65       | 1043.87 / 1038.11 | -5.76        | 17.000 / 19.000  | 19.000 / 17.000  | 670.42 / 724.07   | 772.57 / 758.70   |
| 5   | 1151.04 / 1248.97 | +97.93       | 1038.18 / 972.28  | -65.91       | 17.000 / 17.000  | 19.000 / 18.000  | 904.85 / 984.58   | 761.83 / 712.53   |
| 6   | 1085.15 / 1152.16 | +67.01       | 1196.44 / 1257.36 | +60.92       | 21.000 / 21.000  | 23.000 / 23.000  | 830.73 / 891.72   | 915.18 / 978.39   |
| 7   | 1311.36 / 1445.31 | +133.94      | 1202.54 / 1254.92 | +52.38       | 21.000 / 23.000  | 22.000 / 21.000  | 1035.32 / 1165.21 | 917.60 / 926.55   |
| 8   | 1267.60 / 1318.68 | +51.08       | 1191.57 / 1228.26 | +36.69       | 21.000 / 21.000  | 22.000 / 21.000  | 996.63 / 1069.73  | 896.10 / 942.85   |
| 9   | 1257.58 / 1263.57 | +5.98        | 1123.08 / 1432.33 | +309.25      | 21.000 / 21.000  | 21.000 / 21.000  | 990.31 / 998.54   | 848.56 / 1114.54  |
| 10  | 760.85 / 762.36   | +1.51        | 779.29 / 957.87   | +178.58      | 15.000 / 15.000  | 14.000 / 14.000  | 545.92 / 530.41   | 558.58 / 647.07   |
| 11  | 463.24 / 474.97   | +11.74       | 435.64 / 502.72   | +67.08       | 11.000 / 9.000   | 10.000 / 10.000  | 195.94 / 172.90   | 162.58 / 203.21   |
| 12  | 171.24 / 177.78   | +6.54        | 183.53 / 217.14   | +33.62       | 4.000 / 3.000    | 3.000 / 4.000    | n/a / n/a         | n/a / n/a         |
| 13  | 747.17 / 844.42   | +97.26       | 641.30 / 643.35   | +2.05        | 10.000 / 10.000  | 10.000 / 10.000  | 706.07 / 798.46   | 594.11 / 600.89   |
| 14  | 1350.98 / 1467.44 | +116.46      | 1377.91 / 1553.55 | +175.64      | 29.000 / 28.000  | 28.000 / 29.000  | 1133.80 / 1236.71 | 1120.36 / 1291.34 |
| 15  | 903.65 / 1137.62  | +233.97      | 896.17 / 817.94   | -78.23       | 19.000 / 25.000  | 19.000 / 17.000  | 561.66 / 596.16   | 558.73 / 597.52   |
| 16  | 731.84 / 816.39   | +84.55       | 702.99 / 724.72   | +21.73       | 16.000 / 15.000  | 16.000 / 15.000  | 559.84 / 668.74   | 568.09 / 590.96   |
| 17  | 719.44 / 795.40   | +75.96       | 771.93 / 899.65   | +127.72      | 16.000 / 16.000  | 17.000 / 16.000  | 545.16 / 599.12   | 552.66 / 694.93   |
| 18  | 760.87 / 861.60   | +100.73      | 751.16 / 1570.41  | +819.25      | 16.000 / 16.000  | 16.000 / 28.000  | 584.20 / 667.64   | 576.08 / 1249.29  |
| 19  | 781.36 / 734.87   | -46.49       | 727.32 / 828.55   | +101.22      | 16.000 / 16.000  | 16.000 / 15.000  | 606.13 / 563.84   | 555.96 / 608.83   |
| 20  | 739.77 / 727.04   | -12.73       | 1078.03 / 750.57  | -327.46      | 15.000 / 15.000  | 21.000 / 15.000  | 477.82 / 490.53   | 853.30 / 500.98   |
| 21  | 463.47 / 476.99   | +13.53       | 501.45 / 548.03   | +46.58       | 11.000 / 11.000  | 10.000 / 10.000  | n/a / n/a         | n/a / n/a         |
| 22  | 170.55 / 167.99   | -2.56        | 169.90 / 173.28   | +3.38        | 4.000 / 4.000    | 4.000 / 4.000    | n/a / n/a         | n/a / n/a         |
| 23  | 263.13 / 247.14   | -15.99       | 250.71 / 333.99   | +83.28       | 4.000 / 3.000    | 3.000 / 5.000    | n/a / n/a         | n/a / n/a         |
| 24  | 1184.66 / 1100.56 | -84.10       | 1133.45 / 883.11  | -250.34      | 21.000 / 20.000  | 20.000 / 14.000  | 935.37 / 883.80   | 912.05 / 631.75   |
| 25  | 1856.76 / 1560.19 | -296.57      | 1543.61 / 1198.84 | -344.77      | 34.000 / 29.000  | 28.000 / 18.000  | 1603.95 / 1301.79 | 1279.76 / 901.68  |
| 26  | 932.91 / 1587.41  | +654.50      | 901.36 / 1684.81  | +783.45      | 18.000 / 28.000  | 17.000 / 29.000  | 721.22 / 1372.10  | 675.18 / 1412.73  |
| 27  | 751.64 / 770.14   | +18.50       | 819.34 / 879.62   | +60.29       | 16.000 / 16.000  | 16.000 / 16.000  | 490.10 / 535.23   | 558.58 / 611.83   |
| 28  | 910.66 / 1002.20  | +91.54       | 984.50 / 1077.34  | +92.84       | 16.000 / 16.000  | 16.000 / 16.000  | 673.45 / 771.84   | 762.69 / 822.31   |
| 29  | 1446.69 / 1978.49 | +531.80      | 1932.15 / 2045.21 | +113.06      | 28.000 / 34.000  | 34.000 / 34.000  | 1173.44 / 1715.33 | 1677.34 / 1765.29 |
| 30  | 681.04 / 724.71   | +43.67       | 696.61 / 782.91   | +86.30       | 15.000 / 15.000  | 15.000 / 15.000  | 445.61 / 467.75   | 464.19 / 499.34   |
| 31  | 577.39 / 648.72   | +71.32       | 660.50 / 661.38   | +0.88        | 10.000 / 10.000  | 10.000 / 10.000  | 538.00 / 604.49   | 617.71 / 614.63   |
| 32  | 450.36 / 512.21   | +61.85       | 467.90 / 538.07   | +70.17       | 11.000 / 10.000  | 10.000 / 10.000  | n/a / n/a         | n/a / n/a         |
| 33  | 276.26 / 273.73   | -2.53        | 253.03 / 311.57   | +58.54       | 3.000 / 3.000    | 3.000 / 3.000    | n/a / n/a         | n/a / n/a         |

</details>

<details>
<summary>Exact builds, memory, evidence and limits</summary>

| Identity             | PR66 integration build                                             | PR73                                                               |
| -------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Compiled source      | `a09e1cc77de098f85e74e6d5bb341dc184f83640`                         | `d9780bb743134d975a561618f8b1cf89f8d20304`                         |
| Renderer source/base | `a09e1cc77de098f85e74e6d5bb341dc184f83640`                         | `d9780bb743134d975a561618f8b1cf89f8d20304`                         |
| Main-VR base         | `bf4ae54a7d49620c41cb32ee9ecfd44657688ead`                         | `ef7c366dd73989b2b87751c0ef975db7c6fd310f`                         |
| Run ID               | `renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z`               | `nvidia-20260910T214350263Z`                                       |
| Build ID             | `9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757` | `ac724629b68fbaaceadd64f217d48ad54269165031b984c2c9bb1deb24165f35` |
| DLL SHA-256          | `aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461` | `53adff3245c00856fa8cf18bfb99ad5c36a832f465d604c96980476198a3c89f` |
| DLL size (bytes)     | `28062720`                                                         | `28170752`                                                         |

Both runs used Dragonsreach, DLSS K, explicit FSR3, foveation 0.3/0.3/0.7,
five-second pre-dispatch pacing and a 20-second strict deadline. Retained
scene/toolchain fingerprints differ. Driver, power, headset refresh and
complete modlist/cache equivalence are not established. No causal or
statistically significant change is claimed from these ordered passes.
Fresh resolved GPU samples do not establish an FPS or steady-state
GPU-cost comparison.

Memory classification is inconclusive for both builds. Values below are
end minus start in MiB, except texture count. Positive values are growth.

| Metric                   | PR66 pass 1 | PR73 pass 1 | PR66 pass 2 | PR73 pass 2 |
| ------------------------ | ----------: | ----------: | ----------: | ----------: |
| Process private MiB      |    -114.105 |    -234.930 |    -213.449 |    -225.910 |
| System commit MiB        |    -101.223 |    -819.988 |      40.145 |     351.215 |
| DXGI usage MiB           |    -674.609 |    -611.203 |    -323.090 |    -294.102 |
| Tracked live textures    |     256.000 |     241.000 |     233.000 |     233.000 |
| Tracked live texture MiB |    2424.026 |    2375.922 |    2349.432 |    2349.432 |

Texture trackers were reset at each pass start; tracked-count deltas do
not by themselves establish retained allocation or a leak. All six
memory boundaries, cooldown, ratios and exact classification inputs
remain in each saved summary and complete ledger detail record.

Both runs retain verified physical DLL/manifest/AIO identities matching
their runtime producers. All owned captures are verified inactive and
the journals are flushed. Reporting is COMPLETE. The canonical local
ledger reconstructs the full PR73 summary, PR66 comparison and unrounded
mean/SE data exactly; all 1,056 paired numeric timing cells pass audit
and historical cells are preserved. Raw evidence remains local.

The CSV reader now accepts complete structured cells beyond Python's
default 131,072-character limit, using the ledger byte length as the
bound and restoring the caller's parser limit. All 20 focused reporting
tests pass, including lossless large-cell reads, append transactions,
rejection of changed historical detail and parser-limit restoration.
This reporting correction changes no measured result.

Separate `csx-render-scale-pr-v1` release qualification remains pending;
this tuning assay does not replace it. Live SE/AE scenarios were not run.

</details>

<!-- end pr73-vs-pr66-nvidia-comparison-v1 -->

## Complete retained comparison

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                                        | Candidate                                                                               |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Run                     | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              | nvidia-20260910T214350263Z                                                              |
| Renderer base           | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | d9780bb743134d975a561618f8b1cf89f8d20304                                                |
| Main-VR base/equivalent | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        | ef7c366dd73989b2b87751c0ef975db7c6fd310f                                                |
| Compiled source         | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        | d9780bb743134d975a561618f8b1cf89f8d20304                                                |
| Build ID                | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                | ac724629b68fbaaceadd64f217d48ad54269165031b984c2c9bb1deb24165f35                        |
| DLL SHA-256             | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                | 53adff3245c00856fa8cf18bfb99ad5c36a832f465d604c96980476198a3c89f                        |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T214350263Z |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C   | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | ------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 793.34/863.11 | 8.795        | 9/10        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 807.13/878.70 | 8.867        | 10/9        | 0/0          | 0/0                 | none             | MET/MET             |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline | Candidate | Delta   | Delta % |
| ------ | ---- | -------------------------- | ----------- | -------- | --------- | ------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 735.10   | 814.38    | 79.28   | 10.785  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.400   | 13.840    | 0.440   | 3.284   |
| nvidia | 1    | Relatch proof total        | ms          | 18377.38 | 20359.41  | 1982.04 | 10.785  |
| nvidia | 1    | Relatch proof total        | frames      | 335      | 346       | 11      | 3.284   |
| nvidia | 1    | Relatch proof samples      | transitions | 25       | 25        | 0       | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 793.34   | 863.11    | 69.78   | 8.795   |
| nvidia | 1    | Strict completion mean     | frames      | 15.091   | 15.455    | 0.364   | 2.410   |
| nvidia | 1    | Strict completion total    | ms          | 26180.09 | 28482.71  | 2302.62 | 8.795   |
| nvidia | 1    | Strict completion total    | frames      | 498      | 510       | 12      | 2.410   |
| nvidia | 1    | Stretch completed episodes | episodes    | 17       | 19        | 2       | 11.765  |
| nvidia | 1    | Stretch completed total    | frames      | 69       | 83        | 14      | 20.290  |
| nvidia | 1    | Stretch completed total    | ms          | 4143.53  | 5396.83   | 1253.30 | 30.247  |
| nvidia | 1    | Stretch longest episode    | ms          | 376.75   | 419.56    | 42.81   | 11.363  |
| nvidia | 2    | Relatch proof mean         | ms          | 747.35   | 807.88    | 60.54   | 8.100   |
| nvidia | 2    | Relatch proof mean         | frames      | 13.760   | 13.560    | -0.200  | -1.453  |
| nvidia | 2    | Relatch proof total        | ms          | 18683.66 | 20197.05  | 1513.38 | 8.100   |
| nvidia | 2    | Relatch proof total        | frames      | 344      | 339       | -5      | -1.453  |
| nvidia | 2    | Relatch proof samples      | transitions | 25       | 25        | 0       | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 807.13   | 878.70    | 71.57   | 8.867   |
| nvidia | 2    | Strict completion mean     | frames      | 15.303   | 15.121    | -0.182  | -1.188  |
| nvidia | 2    | Strict completion total    | ms          | 26635.15 | 28997.00  | 2361.86 | 8.867   |
| nvidia | 2    | Strict completion total    | frames      | 505      | 499       | -6      | -1.188  |
| nvidia | 2    | Stretch completed episodes | episodes    | 18       | 18        | 0       | 0       |
| nvidia | 2    | Stretch completed total    | frames      | 78       | 88        | 10      | 12.821  |
| nvidia | 2    | Stretch completed total    | ms          | 4955.68  | 6091.52   | 1135.84 | 22.920  |
| nvidia | 2    | Stretch longest episode    | ms          | 408.50   | 809.54    | 401.04  | 98.172  |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 680.25 / 802.94   | 122.69   | 18.036  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.47 / 182.56   | 13.10    | 7.728   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 250.23 / 245.99   | -4.23    | -1.692  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 911.53 / 972.18   | 60.65    | 6.654   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1151.04 / 1248.97 | 97.93    | 8.508   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1085.15 / 1152.16 | 67.01    | 6.176   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1311.36 / 1445.31 | 133.94   | 10.214  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1267.60 / 1318.68 | 51.08    | 4.030   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1257.58 / 1263.57 | 5.98     | 0.476   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 760.85 / 762.36   | 1.51     | 0.198   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 463.24 / 474.97   | 11.74    | 2.534   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 171.24 / 177.78   | 6.54     | 3.817   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 747.17 / 844.42   | 97.26    | 13.017  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1350.98 / 1467.44 | 116.46   | 8.620   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 903.65 / 1137.62  | 233.97   | 25.892  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 731.84 / 816.39   | 84.55    | 11.553  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 719.44 / 795.40   | 75.96    | 10.559  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 760.87 / 861.60   | 100.73   | 13.239  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 781.36 / 734.87   | -46.49   | -5.950  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 739.77 / 727.04   | -12.73   | -1.720  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 463.47 / 476.99   | 13.53    | 2.919   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 170.55 / 167.99   | -2.56    | -1.499  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 263.13 / 247.14   | -15.99   | -6.076  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1184.66 / 1100.56 | -84.10   | -7.099  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1856.76 / 1560.19 | -296.57  | -15.973 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 932.91 / 1587.41  | 654.50   | 70.158  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 751.64 / 770.14   | 18.50    | 2.461   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 910.66 / 1002.20  | 91.54    | 10.052  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1446.69 / 1978.49 | 531.80   | 36.759  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 681.04 / 724.71   | 43.67    | 6.413   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 577.39 / 648.72   | 71.32    | 12.353  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 450.36 / 512.21   | 61.85    | 13.734  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 276.26 / 273.73   | -2.53    | -0.915  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 501.63 / 549.59   | 680.25 / 802.94   | 178.62 / 253.35  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   | {"blockedOrPreparationToFirstPhysicalMutationMs":312.0717,"dispatchToBlockedOrPreparationMs":97.5029,"firstNewGenerationToCleanupDrainedMs":254.184,"firstPhysicalMutationToFirstNewGenerationMs":139.1818,"presentationToStrictCompletionMs":253.3529}   |
| 2   | 169.47 / 182.56   | 169.47 / 182.56   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":92.3404,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 250.23 / 245.99   | 250.23 / 245.99   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":245.9936,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 789.45 / 807.61   | 829.73 / 891.46   | 40.28 / 83.84    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7582,"dispatchToBlockedOrPreparationMs":381.2807,"firstNewGenerationToCleanupDrainedMs":167.3852,"firstPhysicalMutationToFirstNewGenerationMs":339.0311,"presentationToStrictCompletionMs":164.564}    |
| 5   | 945.18 / 1026.44  | 1068.44 / 1162.69 | 123.26 / 136.25  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1391,"dispatchToBlockedOrPreparationMs":392.2223,"firstNewGenerationToCleanupDrainedMs":178.1069,"firstPhysicalMutationToFirstNewGenerationMs":589.2212,"presentationToStrictCompletionMs":222.5252}   |
| 6   | 877.56 / 934.61   | 1005.16 / 1067.29 | 127.60 / 132.68  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5337,"dispatchToBlockedOrPreparationMs":383.5321,"firstNewGenerationToCleanupDrainedMs":175.5771,"firstPhysicalMutationToFirstNewGenerationMs":503.6494,"presentationToStrictCompletionMs":217.5505}   |
| 7   | 1092.61 / 1210.31 | 1227.59 / 1355.19 | 134.97 / 144.89  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8268,"dispatchToBlockedOrPreparationMs":401.0507,"firstNewGenerationToCleanupDrainedMs":189.9864,"firstPhysicalMutationToFirstNewGenerationMs":759.3282,"presentationToStrictCompletionMs":234.9993}   |
| 8   | 1048.37 / 1110.51 | 1186.92 / 1238.05 | 138.56 / 127.54  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6262,"dispatchToBlockedOrPreparationMs":374.8056,"firstNewGenerationToCleanupDrainedMs":168.328,"firstPhysicalMutationToFirstNewGenerationMs":690.2935,"presentationToStrictCompletionMs":208.1641}    |
| 9   | 1031.84 / 1087.55 | 1174.67 / 1177.83 | 142.83 / 90.29   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5079,"dispatchToBlockedOrPreparationMs":343.5565,"firstNewGenerationToCleanupDrainedMs":179.2961,"firstPhysicalMutationToFirstNewGenerationMs":651.4723,"presentationToStrictCompletionMs":176.0199}   |
| 10  | 590.58 / 594.30   | 760.85 / 762.36   | 170.27 / 168.06  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2328,"dispatchToBlockedOrPreparationMs":333.0018,"firstNewGenerationToCleanupDrainedMs":231.9483,"firstPhysicalMutationToFirstNewGenerationMs":194.1747,"presentationToStrictCompletionMs":168.0551}   |
| 11  | 197.19 / 173.90   | 463.24 / 474.97   | 266.05 / 301.07  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1011,"dispatchToBlockedOrPreparationMs":130.0806,"firstNewGenerationToCleanupDrainedMs":302.0767,"firstPhysicalMutationToFirstNewGenerationMs":38.7151,"presentationToStrictCompletionMs":301.0701}    |
| 12  | 171.24 / 177.78   | 171.24 / 177.78   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":177.7751,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 747.17 / 844.42   | 747.17 / 844.42   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":241.4002,"dispatchToBlockedOrPreparationMs":434.4351,"firstNewGenerationToCleanupDrainedMs":45.9668,"firstPhysicalMutationToFirstNewGenerationMs":122.6216,"presentationToStrictCompletionMs":0}         |
| 14  | 1222.44 / 1372.04 | 1306.23 / 1419.18 | 83.79 / 47.14    | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  | {"blockedOrPreparationToFirstPhysicalMutationMs":298.7148,"dispatchToBlockedOrPreparationMs":439.3714,"firstNewGenerationToCleanupDrainedMs":182.4697,"firstPhysicalMutationToFirstNewGenerationMs":498.6203,"presentationToStrictCompletionMs":95.4095}  |
| 15  | 903.65 / 1137.62  | 561.96 / 637.38   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.4656,"dispatchToBlockedOrPreparationMs":374.2975,"firstNewGenerationToCleanupDrainedMs":41.2185,"firstPhysicalMutationToFirstNewGenerationMs":216.3949,"presentationToStrictCompletionMs":0}           |
| 16  | 731.84 / 816.39   | 602.54 / 717.13   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.5924,"dispatchToBlockedOrPreparationMs":407.9993,"firstNewGenerationToCleanupDrainedMs":48.3925,"firstPhysicalMutationToFirstNewGenerationMs":255.1434,"presentationToStrictCompletionMs":0}           |
| 17  | 719.44 / 795.40   | 587.41 / 644.98   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.3619,"dispatchToBlockedOrPreparationMs":361.9918,"firstNewGenerationToCleanupDrainedMs":45.8614,"firstPhysicalMutationToFirstNewGenerationMs":231.7649,"presentationToStrictCompletionMs":0}           |
| 18  | 760.87 / 861.60   | 630.63 / 713.83   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2232,"dispatchToBlockedOrPreparationMs":443.781,"firstNewGenerationToCleanupDrainedMs":46.186,"firstPhysicalMutationToFirstNewGenerationMs":218.6355,"presentationToStrictCompletionMs":0}             |
| 19  | 781.36 / 734.87   | 650.46 / 604.53   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2401,"dispatchToBlockedOrPreparationMs":345.1883,"firstNewGenerationToCleanupDrainedMs":40.6882,"firstPhysicalMutationToFirstNewGenerationMs":213.4096,"presentationToStrictCompletionMs":0}           |
| 20  | 530.87 / 552.11   | 739.77 / 727.04   | 208.90 / 174.93  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.038,"dispatchToBlockedOrPreparationMs":367.0488,"firstNewGenerationToCleanupDrainedMs":236.5171,"firstPhysicalMutationToFirstNewGenerationMs":119.4382,"presentationToStrictCompletionMs":174.9289}    |
| 21  | 205.53 / 204.52   | 463.47 / 476.99   | 257.94 / 272.47  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.4185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.4697}             |
| 22  | 170.55 / 167.99   | 170.55 / 167.99   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.9918,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 263.13 / 247.14   | 263.13 / 247.14   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.1366,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 993.04 / 925.82   | 1184.66 / 1100.56 | 191.62 / 174.74  | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} | {"blockedOrPreparationToFirstPhysicalMutationMs":288.9222,"dispatchToBlockedOrPreparationMs":452.6056,"firstNewGenerationToCleanupDrainedMs":216.7565,"firstPhysicalMutationToFirstNewGenerationMs":142.2719,"presentationToStrictCompletionMs":174.735}  |
| 25  | 1645.74 / 1344.41 | 1774.36 / 1475.12 | 128.62 / 130.71  | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  | {"blockedOrPreparationToFirstPhysicalMutationMs":328.1896,"dispatchToBlockedOrPreparationMs":486.4611,"firstNewGenerationToCleanupDrainedMs":173.322,"firstPhysicalMutationToFirstNewGenerationMs":487.1429,"presentationToStrictCompletionMs":215.7761}  |
| 26  | 803.46 / 1455.24  | 888.89 / 1540.62  | 85.43 / 85.38    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   | {"blockedOrPreparationToFirstPhysicalMutationMs":333.2343,"dispatchToBlockedOrPreparationMs":477.0536,"firstNewGenerationToCleanupDrainedMs":168.5136,"firstPhysicalMutationToFirstNewGenerationMs":561.8147,"presentationToStrictCompletionMs":132.1692} |
| 27  | 490.91 / 535.48   | 751.64 / 770.14   | 260.73 / 234.66  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5465,"dispatchToBlockedOrPreparationMs":366.8679,"firstNewGenerationToCleanupDrainedMs":234.9134,"firstPhysicalMutationToFirstNewGenerationMs":163.8152,"presentationToStrictCompletionMs":234.6634}   |
| 28  | 810.42 / 914.13   | 860.95 / 957.31   | 50.54 / 43.19    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7464,"dispatchToBlockedOrPreparationMs":436.0405,"firstNewGenerationToCleanupDrainedMs":185.4704,"firstPhysicalMutationToFirstNewGenerationMs":332.0561,"presentationToStrictCompletionMs":88.0751}    |
| 29  | 1218.38 / 1759.50 | 1356.00 / 1896.40 | 137.63 / 136.90  | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} | {"blockedOrPreparationToFirstPhysicalMutationMs":758.6718,"dispatchToBlockedOrPreparationMs":435.4322,"firstNewGenerationToCleanupDrainedMs":181.0707,"firstPhysicalMutationToFirstNewGenerationMs":521.223,"presentationToStrictCompletionMs":218.9908}  |
| 30  | 492.15 / 536.50   | 681.04 / 724.71   | 188.88 / 188.21  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8994,"dispatchToBlockedOrPreparationMs":346.1577,"firstNewGenerationToCleanupDrainedMs":256.9617,"firstPhysicalMutationToFirstNewGenerationMs":117.6914,"presentationToStrictCompletionMs":188.2065}   |
| 31  | 577.39 / 648.72   | 577.39 / 648.72   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":58.2698,"dispatchToBlockedOrPreparationMs":443.9177,"firstNewGenerationToCleanupDrainedMs":44.2242,"firstPhysicalMutationToFirstNewGenerationMs":102.3061,"presentationToStrictCompletionMs":0}          |
| 32  | 184.48 / 211.22   | 450.36 / 512.21   | 265.87 / 300.99  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.5879,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":300.9867}             |
| 33  | 276.26 / 273.73   | 276.26 / 273.73   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.7338,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 451.45 / 548.76      | 97.31    | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 14                  | 2            | 670.42 / 724.07      | 53.65    | 17 / 19           | 2            |
| 5   | 12 / 12                  | 0            | 904.85 / 984.58      | 79.74    | 17 / 17           | 0            |
| 6   | 16 / 16                  | 0            | 830.73 / 891.72      | 60.99    | 21 / 21           | 0            |
| 7   | 16 / 18                  | 2            | 1035.32 / 1165.21    | 129.89   | 21 / 23           | 2            |
| 8   | 16 / 16                  | 0            | 996.63 / 1069.73     | 73.09    | 21 / 21           | 0            |
| 9   | 16 / 16                  | 0            | 990.31 / 998.54      | 8.23     | 21 / 21           | 0            |
| 10  | 10 / 9                   | -1           | 545.92 / 530.41      | -15.51   | 15 / 15           | 0            |
| 11  | 4 / 3                    | -1           | 195.94 / 172.90      | -23.05   | 11 / 9            | -2           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 706.07 / 798.46      | 92.39    | 10 / 10           | 0            |
| 14  | 24 / 23                  | -1           | 1133.80 / 1236.71    | 102.90   | 29 / 28           | -1           |
| 15  | 11 / 12                  | 1            | 561.66 / 596.16      | 34.50    | 19 / 25           | 6            |
| 16  | 12 / 12                  | 0            | 559.84 / 668.74      | 108.89   | 16 / 15           | -1           |
| 17  | 12 / 12                  | 0            | 545.16 / 599.12      | 53.96    | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 584.20 / 667.64      | 83.44    | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 606.13 / 563.84      | -42.29   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 477.82 / 490.53      | 12.70    | 15 / 15           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 16 / 15                  | -1           | 935.37 / 883.80      | -51.57   | 21 / 20           | -1           |
| 25  | 29 / 24                  | -5           | 1603.95 / 1301.79    | -302.15  | 34 / 29           | -5           |
| 26  | 13 / 23                  | 10           | 721.22 / 1372.10     | 650.89   | 18 / 28           | 10           |
| 27  | 10 / 10                  | 0            | 490.10 / 535.23      | 45.13    | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 673.45 / 771.84      | 98.39    | 16 / 16           | 0            |
| 29  | 23 / 29                  | 6            | 1173.44 / 1715.33    | 541.89   | 28 / 34           | 6            |
| 30  | 10 / 9                   | -1           | 445.61 / 467.75      | 22.14    | 15 / 15           | 0            |
| 31  | 9 / 9                    | 0            | 538.00 / 604.49      | 66.49    | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C   | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ---------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 119.89 / 143.67  | 23.78    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 199.80 / 206.03  | 6.23     |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 199.32 / 217.85  | 18.53    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 371.55 / 366.94  | -4.61    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 376.75 / 419.56  | 42.81    |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 366.69 / 378.18  | 11.49    |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 370.25 / 385.81  | 15.56    |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 268.92 / 308.53  | 39.61    |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 142.70 / 139.85  | -2.84    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 160.24 / 162.32  | 2.08     |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 140.16 / 177.14  | 36.98    |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 148.35 / 158.00  | 9.66     |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 154.85 / 162.53  | 7.69     |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 363.91 / 373.52  | 9.60     |
| 26  | 1 / 2                | 1     | 2 / 11             | 9     | 94.39 / 714.01   | 619.62   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 29  | 2 / 3                | 1     | 11 / 16            | 5     | 665.78 / 1082.90 | 417.11   |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00      | 0.00     |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C        | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ----------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 764.65 / 771.85   | 7.21     | 0.942   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 167.67 / 197.62   | 29.96    | 17.868  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 247.37 / 259.65   | 12.28    | 4.964   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1043.87 / 1038.11 | -5.76    | -0.552  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1038.18 / 972.28  | -65.91   | -6.348  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1196.44 / 1257.36 | 60.92    | 5.092   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1202.54 / 1254.92 | 52.38    | 4.356   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1191.57 / 1228.26 | 36.69    | 3.079   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1123.08 / 1432.33 | 309.25   | 27.536  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 779.29 / 957.87   | 178.58   | 22.915  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 435.64 / 502.72   | 67.08    | 15.399  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 183.53 / 217.14   | 33.62    | 18.316  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 641.30 / 643.35   | 2.05     | 0.319   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.91 / 1553.55 | 175.64   | 12.747  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 896.17 / 817.94   | -78.23   | -8.730  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 702.99 / 724.72   | 21.73    | 3.091   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 771.93 / 899.65   | 127.72   | 16.546  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 751.16 / 1570.41  | 819.25   | 109.064 | 0/1         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 727.32 / 828.55   | 101.22   | 13.917  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1078.03 / 750.57  | -327.46  | -30.376 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 501.45 / 548.03   | 46.58    | 9.289   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 169.90 / 173.28   | 3.38     | 1.990   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 250.71 / 333.99   | 83.28    | 33.215  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1133.45 / 883.11  | -250.34  | -22.086 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1543.61 / 1198.84 | -344.77  | -22.335 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 901.36 / 1684.81  | 783.45   | 86.919  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 819.34 / 879.62   | 60.29    | 7.358   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 984.50 / 1077.34  | 92.84    | 9.430   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1932.15 / 2045.21 | 113.06   | 5.852   | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 696.61 / 782.91   | 86.30    | 12.389  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 660.50 / 661.38   | 0.88     | 0.133   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 467.90 / 538.07   | 70.17    | 14.998  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 253.03 / 311.57   | 58.54    | 23.135  | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C  | Cleanup B/C       | Cleanup tail B/C | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ----------------- | ----------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 524.71 / 519.52   | 764.65 / 771.85   | 239.94 / 252.33  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7265,"dispatchToBlockedOrPreparationMs":394.2918,"firstNewGenerationToCleanupDrainedMs":252.9548,"firstPhysicalMutationToFirstNewGenerationMs":120.8803,"presentationToStrictCompletionMs":252.3298}   |
| 2   | 167.67 / 197.62   | 167.67 / 197.62   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":197.6237,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 247.37 / 259.65   | 247.37 / 259.65   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":259.6454,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 816.59 / 803.02   | 956.84 / 943.39   | 140.25 / 140.37  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8405,"dispatchToBlockedOrPreparationMs":393.8367,"firstNewGenerationToCleanupDrainedMs":184.6964,"firstPhysicalMutationToFirstNewGenerationMs":361.0196,"presentationToStrictCompletionMs":235.0857}   |
| 5   | 812.14 / 756.75   | 952.99 / 886.13   | 140.85 / 129.37  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8012,"dispatchToBlockedOrPreparationMs":365.3724,"firstNewGenerationToCleanupDrainedMs":173.592,"firstPhysicalMutationToFirstNewGenerationMs":343.3596,"presentationToStrictCompletionMs":215.527}     |
| 6   | 963.68 / 1031.05  | 1108.47 / 1171.47 | 144.79 / 140.42  | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6885,"dispatchToBlockedOrPreparationMs":446.7609,"firstNewGenerationToCleanupDrainedMs":193.075,"firstPhysicalMutationToFirstNewGenerationMs":527.941,"presentationToStrictCompletionMs":226.3152}     |
| 7   | 965.79 / 974.69   | 1113.35 / 1158.15 | 147.56 / 183.45  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5767,"dispatchToBlockedOrPreparationMs":375.1493,"firstNewGenerationToCleanupDrainedMs":231.5996,"firstPhysicalMutationToFirstNewGenerationMs":547.8189,"presentationToStrictCompletionMs":280.2336}   |
| 8   | 944.88 / 989.88   | 1097.89 / 1145.33 | 153.02 / 155.45  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1262,"dispatchToBlockedOrPreparationMs":397.3086,"firstNewGenerationToCleanupDrainedMs":202.4733,"firstPhysicalMutationToFirstNewGenerationMs":540.4192,"presentationToStrictCompletionMs":238.3733}   |
| 9   | 892.41 / 1177.62  | 1033.09 / 1337.10 | 140.69 / 159.47  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0036,"dispatchToBlockedOrPreparationMs":492.9627,"firstNewGenerationToCleanupDrainedMs":222.5509,"firstPhysicalMutationToFirstNewGenerationMs":616.5778,"presentationToStrictCompletionMs":254.7074}   |
| 10  | 604.63 / 734.02   | 779.29 / 957.87   | 174.66 / 223.85  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3981,"dispatchToBlockedOrPreparationMs":428.804,"firstNewGenerationToCleanupDrainedMs":310.7936,"firstPhysicalMutationToFirstNewGenerationMs":213.8696,"presentationToStrictCompletionMs":223.8465}    |
| 11  | 163.34 / 204.49   | 435.64 / 502.72   | 272.29 / 298.23  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6884,"dispatchToBlockedOrPreparationMs":103.1938,"firstNewGenerationToCleanupDrainedMs":299.5075,"firstPhysicalMutationToFirstNewGenerationMs":95.3306,"presentationToStrictCompletionMs":298.2296}    |
| 12  | 183.53 / 217.14   | 183.53 / 217.14   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.142,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 641.30 / 643.35   | 641.30 / 643.35   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":55.5131,"dispatchToBlockedOrPreparationMs":449.6862,"firstNewGenerationToCleanupDrainedMs":42.4529,"firstPhysicalMutationToFirstNewGenerationMs":95.6928,"presentationToStrictCompletionMs":0}           |
| 14  | 1171.62 / 1395.57 | 1323.72 / 1498.31 | 152.10 / 102.74  | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  | {"blockedOrPreparationToFirstPhysicalMutationMs":299.5089,"dispatchToBlockedOrPreparationMs":465.0504,"firstNewGenerationToCleanupDrainedMs":206.9776,"firstPhysicalMutationToFirstNewGenerationMs":526.7758,"presentationToStrictCompletionMs":157.9802} |
| 15  | 896.17 / 817.94   | 558.99 / 641.88   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.3628,"dispatchToBlockedOrPreparationMs":354.2863,"firstNewGenerationToCleanupDrainedMs":44.36,"firstPhysicalMutationToFirstNewGenerationMs":237.8689,"presentationToStrictCompletionMs":0}             |
| 16  | 702.99 / 724.72   | 611.69 / 633.57   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":5.7923,"dispatchToBlockedOrPreparationMs":388.2959,"firstNewGenerationToCleanupDrainedMs":42.611,"firstPhysicalMutationToFirstNewGenerationMs":196.8739,"presentationToStrictCompletionMs":0}            |
| 17  | 771.93 / 899.65   | 595.30 / 741.69   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2518,"dispatchToBlockedOrPreparationMs":419.9445,"firstNewGenerationToCleanupDrainedMs":46.7604,"firstPhysicalMutationToFirstNewGenerationMs":269.736,"presentationToStrictCompletionMs":0}            |
| 18  | 751.16 / 1570.41  | 619.65 / 1303.03  | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":320.1174,"dispatchToBlockedOrPreparationMs":440.0657,"firstNewGenerationToCleanupDrainedMs":53.7404,"firstPhysicalMutationToFirstNewGenerationMs":489.1037,"presentationToStrictCompletionMs":0}         |
| 19  | 727.32 / 828.55   | 556.32 / 658.59   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8719,"dispatchToBlockedOrPreparationMs":365.5769,"firstNewGenerationToCleanupDrainedMs":49.7613,"firstPhysicalMutationToFirstNewGenerationMs":238.3832,"presentationToStrictCompletionMs":0}           |
| 20  | 901.84 / 501.09   | 1078.03 / 750.57  | 176.19 / 249.48  | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9704,"dispatchToBlockedOrPreparationMs":372.8314,"firstNewGenerationToCleanupDrainedMs":249.5907,"firstPhysicalMutationToFirstNewGenerationMs":123.1741,"presentationToStrictCompletionMs":249.4798}   |
| 21  | 202.83 / 254.35   | 501.45 / 548.03   | 298.62 / 293.68  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":150.7232,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":293.6762}             |
| 22  | 169.90 / 173.28   | 169.90 / 173.28   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":173.2812,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 250.71 / 333.99   | 250.71 / 333.99   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":333.9867,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 955.47 / 679.88   | 1133.45 / 883.11  | 177.98 / 203.24  | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6665,"dispatchToBlockedOrPreparationMs":446.5848,"firstNewGenerationToCleanupDrainedMs":251.3625,"firstPhysicalMutationToFirstNewGenerationMs":180.4998,"presentationToStrictCompletionMs":203.236}    |
| 25  | 1323.68 / 952.26  | 1457.90 / 1107.15 | 134.22 / 154.89  | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} | {"blockedOrPreparationToFirstPhysicalMutationMs":70.1894,"dispatchToBlockedOrPreparationMs":442.3875,"firstNewGenerationToCleanupDrainedMs":205.4701,"firstPhysicalMutationToFirstNewGenerationMs":389.1053,"presentationToStrictCompletionMs":246.5821}  |
| 26  | 763.25 / 1516.84  | 901.36 / 1629.59  | 138.12 / 112.75  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   | {"blockedOrPreparationToFirstPhysicalMutationMs":302.4972,"dispatchToBlockedOrPreparationMs":495.5607,"firstNewGenerationToCleanupDrainedMs":216.8519,"firstPhysicalMutationToFirstNewGenerationMs":614.6761,"presentationToStrictCompletionMs":167.9725} |
| 27  | 559.83 / 613.06   | 819.34 / 879.62   | 259.51 / 266.56  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2975,"dispatchToBlockedOrPreparationMs":405.8918,"firstNewGenerationToCleanupDrainedMs":267.7948,"firstPhysicalMutationToFirstNewGenerationMs":200.6391,"presentationToStrictCompletionMs":266.5594}   |
| 28  | 849.14 / 915.33   | 938.20 / 1027.14  | 89.06 / 111.81   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9692,"dispatchToBlockedOrPreparationMs":479.2482,"firstNewGenerationToCleanupDrainedMs":204.8266,"firstPhysicalMutationToFirstNewGenerationMs":339.0928,"presentationToStrictCompletionMs":162.0104}   |
| 29  | 1762.50 / 1814.02 | 1850.17 / 1960.14 | 87.67 / 146.11   | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} | {"blockedOrPreparationToFirstPhysicalMutationMs":734.4687,"dispatchToBlockedOrPreparationMs":468.2664,"firstNewGenerationToCleanupDrainedMs":194.8429,"firstPhysicalMutationToFirstNewGenerationMs":562.5572,"presentationToStrictCompletionMs":231.1859} |
| 30  | 464.69 / 499.94   | 696.61 / 782.91   | 231.92 / 282.97  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9882,"dispatchToBlockedOrPreparationMs":368.1975,"firstNewGenerationToCleanupDrainedMs":283.5744,"firstPhysicalMutationToFirstNewGenerationMs":127.1543,"presentationToStrictCompletionMs":282.9732}   |
| 31  | 660.50 / 661.38   | 660.50 / 661.38   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":63.6418,"dispatchToBlockedOrPreparationMs":449.8247,"firstNewGenerationToCleanupDrainedMs":46.7498,"firstPhysicalMutationToFirstNewGenerationMs":101.1616,"presentationToStrictCompletionMs":0}          |
| 32  | 190.74 / 233.97   | 467.90 / 538.07   | 277.16 / 304.11  | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":135.8276,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":304.1062}             |
| 33  | 253.03 / 311.57   | 253.03 / 311.57   | 0.00 / 0.00      | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":311.5717,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 9                   | -1           | 523.90 / 518.90      | -5.00    | 16 / 14           | -2           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 12                  | -2           | 772.57 / 758.70      | -13.88   | 19 / 17           | -2           |
| 5   | 14 / 13                  | -1           | 761.83 / 712.53      | -49.29   | 19 / 18           | -1           |
| 6   | 18 / 18                  | 0            | 915.18 / 978.39      | 63.21    | 23 / 23           | 0            |
| 7   | 17 / 16                  | -1           | 917.60 / 926.55      | 8.95     | 22 / 21           | -1           |
| 8   | 17 / 16                  | -1           | 896.10 / 942.85      | 46.75    | 22 / 21           | -1           |
| 9   | 16 / 16                  | 0            | 848.56 / 1114.54     | 265.99   | 21 / 21           | 0            |
| 10  | 9 / 9                    | 0            | 558.58 / 647.07      | 88.49    | 14 / 14           | 0            |
| 11  | 3 / 4                    | 1            | 162.58 / 203.21      | 40.63    | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 13  | 9 / 9                    | 0            | 594.11 / 600.89      | 6.78     | 10 / 10           | 0            |
| 14  | 23 / 24                  | 1            | 1120.36 / 1291.34    | 170.98   | 28 / 29           | 1            |
| 15  | 12 / 12                  | 0            | 558.73 / 597.52      | 38.79    | 19 / 17           | -2           |
| 16  | 12 / 12                  | 0            | 568.09 / 590.96      | 22.87    | 16 / 15           | -1           |
| 17  | 12 / 12                  | 0            | 552.66 / 694.93      | 142.28   | 17 / 16           | -1           |
| 18  | 12 / 22                  | 10           | 576.08 / 1249.29     | 673.20   | 16 / 28           | 12           |
| 19  | 12 / 11                  | -1           | 555.96 / 608.83      | 52.88    | 16 / 15           | -1           |
| 20  | 16 / 10                  | -6           | 853.30 / 500.98      | -352.33  | 21 / 15           | -6           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 5             | 2            |
| 24  | 15 / 9                   | -6           | 912.05 / 631.75      | -280.30  | 20 / 14           | -6           |
| 25  | 23 / 13                  | -10          | 1279.76 / 901.68     | -378.08  | 28 / 18           | -10          |
| 26  | 12 / 24                  | 12           | 675.18 / 1412.73     | 737.56   | 17 / 29           | 12           |
| 27  | 10 / 10                  | 0            | 558.58 / 611.83      | 53.25    | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 762.69 / 822.31      | 59.62    | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1677.34 / 1765.29    | 87.96    | 34 / 34           | 0            |
| 30  | 9 / 9                    | 0            | 464.19 / 499.34      | 35.15    | 15 / 15           | 0            |
| 31  | 9 / 9                    | 0            | 617.71 / 614.63      | -3.08    | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 237.46 / 222.00   | -15.47   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 232.35 / 218.34   | -14.00   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 400.83 / 395.56   | -5.27    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 401.92 / 435.80   | 33.88    |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.94 / 417.76   | 15.81    |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 390.03 / 484.75   | 94.71    |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 307.78 / 321.89   | 14.11    |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.90 / 156.51   | 12.60    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 141.18 / 141.26   | 0.09     |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 144.86 / 205.36   | 60.49    |
| 18  | 1 / 1                | 0     | 3 / 13             | 10    | 144.35 / 809.54   | 665.19   |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 143.96 / 183.57   | 39.61    |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 334.87 / 0.00     | -334.87  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 25  | 1 / 1                | 0     | 6 / 2              | -4    | 395.52 / 261.10   | -134.42  |
| 26  | 1 / 2                | 1     | 2 / 11             | 9     | 93.46 / 722.26    | 628.79   |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 1041.25 / 1115.84 | 74.59    |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0.00 / 0.00       | 0.00     |

## Cumulative gates and other health evidence

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

### nvidia-20260910T214350263Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                            | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                      | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":159796,"leftPath":"NativeOriginal","referenceFrame":160181,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### nvidia-20260910T214350263Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                            | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":164115,"leftPath":"NativeOriginal","referenceFrame":164474,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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
| nvidia | 1    | processPrivateMiB | 16521.3203125 / 16407.21484375 / -114.10546875 | 16720.40234375 / 16485.47265625 / -234.9296875 | -120.824              |
| nvidia | 1    | systemCommitMiB   | 54414.625 / 54313.40234375 / -101.22265625     | 56627.2421875 / 55807.25390625 / -819.98828125 | -718.766              |
| nvidia | 1    | dxgiUsageMiB      | 4201.6171875 / 3527.0078125 / -674.609375      | 4176.86328125 / 3565.66015625 / -611.203125    | 63.406                |
| nvidia | 1    | liveTextures      | 0 / 256 / 256                                  | 0 / 241 / 241                                  | -15                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2424.0259971618652 / 2424.0259971618652    | 0 / 2375.9215354919434 / 2375.9215354919434    | -48.104               |
| nvidia | 2    | processPrivateMiB | 16831.234375 / 16617.78515625 / -213.44921875  | 16859.75 / 16633.83984375 / -225.91015625      | -12.461               |
| nvidia | 2    | systemCommitMiB   | 54507.67578125 / 54547.8203125 / 40.14453125   | 56129.0625 / 56480.27734375 / 351.21484375     | 311.070               |
| nvidia | 2    | dxgiUsageMiB      | 3889.8828125 / 3566.79296875 / -323.08984375   | 3898.15234375 / 3604.05078125 / -294.1015625   | 28.988                |
| nvidia | 2    | liveTextures      | 0 / 233 / 233                                  | 0 / 233 / 233                                  | 0                     |
| nvidia | 2    | liveTextureMiB    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0 / 2349.4316596984863 / 2349.4316596984863    | 0                     |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2222        | 2184        | -38        |
| cpu/compactPresentationContract/reuses                  | 2200        | 2162        | -38        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 151         | 154         | 3          |
| cpu/generationResourceValidation/fullValidations        | 568         | 582         | 14         |
| cpu/generationResourceValidation/stableChecks           | 8397        | 8304        | -93        |
| cpu/generationResourceValidation/stableHits             | 8316        | 8227        | -89        |
| cpu/generationResourceValidation/stableMisses           | 81          | 77          | -4         |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4328        | 4211        | -117       |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4328        | 4211        | -117       |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4286        | 4169        | -117       |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4328        | 4211        | -117       |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4307        | 4190        | -117       |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4294        | 4177        | -117       |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4238        | 4118        | -120       |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 94          | 4          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4313        | 4196        | -117       |
| cpu/strongStereoPacket/captures                         | 4628        | 4540        | -88        |
| cpu/strongStereoPacket/commitAccepts                    | 4424        | 4347        | -77        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 67          | 1          |
| cpu/strongStereoPacket/commitValidations                | 4490        | 4414        | -76        |
| cpu/strongStereoPacket/cycleReuses                      | 2275        | 2229        | -46        |
| cpu/strongStereoPacket/fastSkips                        | 4028        | 3882        | -146       |
| cpu/strongStereoPacket/invalidations                    | 4882        | 4770        | -112       |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 100         | 104         | 4          |
| cpu/strongStereoPacket/lifetimeReuses                   | 2253        | 2207        | -46        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.855       | 1.855       | 0.000      |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 68.100      | 24          | -44.100    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.147       | 0.136       | -0.011     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 11.900      | 1.500       | -10.400    |
| cpu/window/currentFrame                                 | 18480       | 160182      | 141702     |
| cpu/window/elapsedFrames                                | 4328        | 4212        | -116       |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 14152       | 155970      | 141818     |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 18482       | 160182      | 141700     |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6230998224  | 6072400224  | -158598000 |
| gpu/item10PeripheryTAAHistory/dispatches                | 4911        | 4786        | -125       |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12474725760 | 12157205760 | -317520000 |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.306       | -0.000     |
| gpu/item5ActiveFSRCopies/activePixels                   | 12047940400 | 11703352280 | -344588120 |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27299138000 | 26551457320 | -747680680 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15490       | 15060       | -430       |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 2045        | 1987        | -58        |
| gpu/item7EarlyHAM/executedClears                        | 2052        | 2002        | -50        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2052        | 2002        | -50        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4256        | 4148        | -108       |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4330        | 4212        | -118       |
| gpu/startFrame                                          | 14152       | 155970      | 141818     |
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
| texture/createdCount                                    | 3974        | 3969        | -5         |
| texture/createdEstimatedBytes                           | 38786366328 | 38791801128 | 5434800    |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3718        | 3728        | 10         |
| texture/destroyedEstimatedBytes                         | 36244590844 | 36300466828 | 55875984   |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 896         | 893         | -3         |
| texture/liveTextureRecordCount                          | 256         | 241         | -15        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 47          | 32          | -15        |
| texture/niSourceTextureMatchedEstimatedBytes            | 166123904   | 115682720   | -50441184  |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1473        | 1482        | 9          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 256         | 241         | -15        |
| texture/outstandingEstimatedBytes                       | 2541775484  | 2491334300  | -50441184  |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 1           | 1           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2189        | 2032        | -157        |
| cpu/compactPresentationContract/reuses                  | 2167        | 2010        | -157        |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 156         | 147         | -9          |
| cpu/generationResourceValidation/fullValidations        | 575         | 566         | -9          |
| cpu/generationResourceValidation/stableChecks           | 8303        | 7790        | -513        |
| cpu/generationResourceValidation/stableHits             | 8226        | 7713        | -513        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 2           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4221        | 3911        | -310        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4221        | 3911        | -310        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4179        | 3869        | -310        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4221        | 3911        | -310        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4200        | 3890        | -310        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4187        | 3877        | -310        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4130        | 3816        | -314        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 94          | 4           |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4206        | 3896        | -310        |
| cpu/strongStereoPacket/captures                         | 4548        | 4210        | -338        |
| cpu/strongStereoPacket/commitAccepts                    | 4359        | 4045        | -314        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 65          | 0           |
| cpu/strongStereoPacket/commitValidations                | 4424        | 4110        | -314        |
| cpu/strongStereoPacket/cycleReuses                      | 2233        | 2064        | -169        |
| cpu/strongStereoPacket/fastSkips                        | 3894        | 3612        | -282        |
| cpu/strongStereoPacket/invalidations                    | 4767        | 4455        | -312        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 103         | 0           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2212        | 2043        | -169        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.879       | 1.966       | 0.087       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 25.400      | 18.100      | -7.300      |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.143       | 0.141       | -0.001      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 2.500       | 0.900       |
| cpu/window/currentFrame                                 | 23112       | 164474      | 141362      |
| cpu/window/elapsedFrames                                | 4220        | 3910        | -310        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 18892       | 160564      | 141672      |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 23113       | 164476      | 141363      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6071131440  | 5591531088  | -479600352  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4785        | 4407        | -378        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12154665600 | 11194485120 | -960180480  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.309       | 0.315       | 0.006       |
| gpu/item5ActiveFSRCopies/activePixels                   | 11865705520 | 11319119960 | -546585560  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26516112080 | 24624144040 | -1891968040 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15110       | 14150       | -960        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 1991        | 1847        | -144        |
| gpu/item7EarlyHAM/executedClears                        | 2016        | 1826        | -190        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2016        | 1826        | -190        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4168        | 3834        | -334        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4221        | 3912        | -309        |
| gpu/startFrame                                          | 18892       | 160564      | 141672      |
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
| texture/createdCount                                    | 3968        | 3964        | -4          |
| texture/createdEstimatedBytes                           | 38826844112 | 38844584736 | 17740624    |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3735        | 3731        | -4          |
| texture/destroyedEstimatedBytes                         | 36363286460 | 36381027084 | 17740624    |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 887         | 885         | -2          |
| texture/liveTextureRecordCount                          | 233         | 233         | 0           |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 26          | 26          | 0           |
| texture/niSourceTextureMatchedEstimatedBytes            | 87906272    | 87906272    | 0           |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1506        | 1502        | -4          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 233         | 233         | 0           |
| texture/outstandingEstimatedBytes                       | 2463557652  | 2463557652  | 0           |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0           |
| texture/recordingFailures                               | 0           | 0           | 0           |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0           |
| texture/sessionID                                       | 2           | 2           | 0           |
| texture/supported                                       | true        | true        | n/a         |

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

## Complete candidate evidence summary

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
-   Presentation stretch: **31 selected, 31 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

## Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms  | Max ms  | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | ------- | ------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 863.11         | 1571.08 | 1978.49 | 15.455             | 814.38          | 13.840              | 19               | 83             | 5396.83    | 10      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 878.70         | 1616.17 | 2045.21 | 15.121             | 807.88          | 13.560              | 18               | 88             | 6091.52    | 9       | 0        | 0                   | MET             | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                            | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                      | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":159796,"leftPath":"NativeOriginal","referenceFrame":160181,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                            | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":13,"maximumObservedFrames":13}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":164115,"leftPath":"NativeOriginal","referenceFrame":164474,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000.00 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    16720.402 |  16485.473 |      -234.93 |      16841.449 |    16833.945 |         -7.504 |     16859.75 |   16633.84 |      -225.91 |                   n.d. |
| System commit MiB                  |    56627.242 |  55807.254 |     -819.988 |       56194.34 |    56122.238 |        -72.102 |    56129.063 |  56480.277 |      351.215 |                   n.d. |
| DXGI process usage MiB             |     4176.863 |    3565.66 |     -611.203 |       3829.477 |     3838.977 |            9.5 |     3898.152 |   3604.051 |     -294.102 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        241 |          241 |            241 |          241 |              0 |            0 |        233 |          233 |                  0.967 |
| Estimated live tracked texture MiB |            0 |   2375.922 |     2375.922 |       2375.922 |     2375.922 |              0 |            0 |   2349.432 |     2349.432 |                  0.989 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -234.9296875,
        "systemCommitMiB": -819.98828125,
        "dxgiUsageMiB": -611.203125,
        "liveTextures": 241,
        "liveTextureMiB": 2375.9215354919434
    },
    "pass2": {
        "processPrivateMiB": -225.91015625,
        "systemCommitMiB": 351.21484375,
        "dxgiUsageMiB": -294.1015625,
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

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                          | Ready-to-candidate             | Actual backend | Lane qualification                     | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery  | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | ------------------------------------------------------ | ------------------------------ | -------------- | -------------------------------------- | ------ | --------- | ------ | -------------------- | -------------- | ----------------- | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 1              | 155980/549.59 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                          | 104.69 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 156346/807.61 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                          | 109.19 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 156479/1026.44 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.83 ms; SubmitStageFoveatedCenter: 53.80 ms | 213.36 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 156610/934.61 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 59.38 ms; SubmitStageFoveatedCenter: 59.32 ms | 250.84 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 156741/1210.31 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 53.59 ms; SubmitStageFoveatedCenter: 53.56 ms | 221.77 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 156881/1110.51 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 55.01 ms; SubmitStageFoveatedCenter: 54.96 ms | 224.25 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 157023/1087.55 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 157683/1372.03 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 157824/1137.62 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 157952/816.39 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 158077/795.40 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 158200/861.60 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 158330/734.87 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | 271.83 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 159111/1344.41 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 159256/1455.24 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                          | 304.71 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 159668/1759.50 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                          | 111.67 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 160929/803.02 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                          | 100.52 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 161059/756.75 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.92 ms; SubmitStageFoveatedCenter: 52.84 ms | 224.25 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 161193/1031.05 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 57.16 ms; SubmitStageFoveatedCenter: 57.03 ms | 265.33 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 161319/974.69 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 62.83 ms; SubmitStageFoveatedCenter: 62.75 ms | 254.94 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 161445/989.88 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 65.23 ms; SubmitStageFoveatedCenter: 65.22 ms | 290.13 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 161565/1177.62 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 6              | 162145/1395.57 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 162276/817.94 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 162400/724.72 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 162504/899.65 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 13             | 162633/1570.41 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | 3              | 162760/828.55 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 0       | complete                       | none observed                                          | 125.68 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | 2              | 163484/952.26 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                          | n.d. (no viewport observation) | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 163616/1516.84 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                          | 319.99 ms                      | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 163994/1814.02 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | fsr_runtime    | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | none           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | not_applicable       | none           | none              | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                          | n.d.                           | dlss           | NOT_APPLICABLE: qualified/inapplicable | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none              | not_needed | MATCHED   | none                | none             | none                      |

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
| nvidia |    1 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    1 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    1 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |        not_exposed | yes            |
| nvidia |    1 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |
| nvidia |    2 |   4 | {"method":"dlss","qualityMode":0,"renderScaleMode":false} | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |   5 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":2,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |   6 | {"method":"dlss","qualityMode":2,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":3,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   7 | {"method":"dlss","qualityMode":3,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":4,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   8 | {"method":"dlss","qualityMode":4,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":5,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |   9 | {"method":"dlss","qualityMode":5,"renderScaleMode":true}  | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |  14 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |                  6 | yes            |
| nvidia |    2 |  15 | {"method":"fsr","qualityMode":1,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":2,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  16 | {"method":"fsr","qualityMode":2,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":3,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  17 | {"method":"fsr","qualityMode":3,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":4,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                 13 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  2 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |        not_exposed | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |

## Local evidence and ledger

-   [Canonical ledger](../../docs/development/vr-render-scale-comparison-ledger.csv)
-   [Run summary](../../artifacts/renderscale-tuning/nvidia-20260910T214350263Z/summary.json)
-   [Complete field coverage](../../artifacts/renderscale-tuning/nvidia-20260910T214350263Z/complete-ledger-validation.json)
-   [Unrounded mean/SE inputs](../../artifacts/renderscale-tuning/nvidia-20260910T214350263Z/comparison-statistics.json)
-   [DLL verification](../../artifacts/renderscale-tuning/nvidia-20260910T214350263Z/raw/offline/physical-aio-verification.json)
