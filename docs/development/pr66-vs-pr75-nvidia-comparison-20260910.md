# PR66 compared with measured PR75: NVIDIA, September 10

<!-- pr66-vs-pr75-nvidia-comparison-v1 -->

### NVIDIA comparison: measured PR75 vs PR66 on merged PR75

**PR66 averaged 800.2 ms per switch versus
810.4 ms for PR75: 10.159 ms lower
(1.25%).** Pass 1 changed -2.801% and pass 2
+0.316%. Both builds completed 66/66 transitions with terminal PASS,
Task 2 counts 66 PASS / 0 FAIL / 0 INCONCLUSIVE, and applicable health
MET in both passes. No fidelity or vendor-fallback failures were observed.

The reference is the exact PR75 run used in its published comparison,
compiled source `c615779a9`. The candidate is local PR66 integration build
`a09e1cc77` on merged PR75/main-VR `bf4ae54a7`. The published PR66 branch
still points to `80f83d2dd` on `7c8e3e656`. This assay therefore measures
the PR66 patches integrated with PR75, and does not establish runtime
qualification of that older published binary. `git range-diff 7c8e3e656..80f83d2dd bf4ae54a7..a09e1cc77` confirms all three PR66 patches
are unchanged. PR75 merged as `bf4ae54a7`; its exact compiled source and
merge commit remain distinct provenance identities.

Formal improvement-or-neutral assessment: **INCONCLUSIVE**. Retained
scene and toolchain differ, a complete matching fixture fingerprint is
unavailable, and no versioned tolerance policy was specified. These are
descriptive switch timings from one process per build, each with two
ordered 33-transition passes. No interrupted attempt is pooled into them.

#### Main comparison

Each timing row first averages identical routes within each pass: 33 for
completion/presentation/cleanup and 25 applicable relatch boundaries.
Stretch rows use full-pass totals. SE is sample standard deviation across
the two pass summaries divided by sqrt(2); it describes within-run
variation and is neither a confidence interval nor a significance test.
Timings exclude the five-second pre-dispatch wait.

| Measurement                    | PR75 mean (2 passes) | PR66 mean (2 passes) |           Change | ±SE PR75 | ±SE PR66 |
| ------------------------------ | -------------------: | -------------------: | ---------------: | -------: | -------: |
| Switch completion (ms)         |                810.4 |                800.2 |  -10.159 (-1.3%) |     ±5.8 |     ±6.9 |
| Presentation ready (ms)        |                686.4 |                673.5 |  -12.903 (-1.9%) |     ±1.1 |     ±1.0 |
| Cleanup tail (ms)              |                100.5 |                102.4 |   +1.874 (+1.9%) |     ±5.5 |     ±6.0 |
| Relatch proof (ms)             |                752.0 |                741.2 |  -10.765 (-1.4%) |    ±20.1 |     ±6.1 |
| Switch completion (frames)     |                 15.5 |                 15.2 |   -0.333 (-2.1%) |     ±0.2 |     ±0.1 |
| Relatch proof (frames)         |                 13.9 |                 13.6 |   -0.320 (-2.3%) |   ±0.020 |     ±0.2 |
| Stretch episodes per pass      |                 18.0 |                 17.5 |   -0.500 (-2.8%) |     ±1.0 |     ±0.5 |
| Stretch frames per pass        |                 80.0 |                 73.5 |   -6.500 (-8.1%) |     ±3.0 |     ±4.5 |
| Stretch duration per pass (ms) |              4,861.8 |              4,549.6 | -312.150 (-6.4%) |   ±277.7 |   ±406.1 |

#### Each pass remains visible

| Pass | PR75 strict mean ms | PR66 strict mean ms |  Change | PR75 / PR66 p95 ms  | PR75 / PR66 maximum ms | Health PR75 / PR66 |
| ---- | ------------------: | ------------------: | ------: | ------------------- | ---------------------- | ------------------ |
| 1    |             816.198 |             793.336 | -2.801% | 1450.504 / 1389.267 | 1848.429 / 1856.757    | MET / MET          |
| 2    |             804.581 |             807.126 | +0.316% | 1415.251 / 1444.193 | 1844.264 / 1932.147    | MET / MET          |

The largest higher route mean is **row 20, FSR3 UP -> FSR3 Native AA**:
726.734 -> 908.898 ms
(+25.07%). Its pass deltas are
+33.901/+330.427 ms.
The largest lower route mean is row 26, DLSS Hoshipa -> FSR3 Hoshipa,
-477.400 ms (-34.23%).
Rows 9, 11, 18, 20, 21, 24, 27, 28 are slower in both passes. Lower overall means
do not establish that every route improved.

#### Health, retries and recovered stretch

Counts below cover both complete passes of each build. Producer retries
occur within a switch and are distinct from protocol recovery applies.

| Result                                   |       PR75 | PR66 integration build |
| ---------------------------------------- | ---------: | ---------------------: |
| Completed / terminal PASS                |    66 / 66 |                66 / 66 |
| Task 2 PASS / FAIL / INCONCLUSIVE        | 66 / 0 / 0 |             66 / 0 / 0 |
| Fidelity mismatch observations           |          0 |                      0 |
| Vendor-failure stretch-eye observations  |          0 |                      0 |
| Bounds-mismatch fallback observations    |          0 |                      0 |
| Producer retries (pass 1 / pass 2)       |    10 / 10 |                 9 / 10 |
| Selected stretch transitions / recovered |    31 / 31 |                32 / 32 |
| Separate recovery applies / replays      |      0 / 0 |                  0 / 0 |
| Device loss                              |          0 |                      0 |
| OOM                                      |          0 |                      0 |
| Producer terminal failures               |          0 |                      0 |
| Vendor-native qualification failures     |          0 |                      0 |
| Credible liveness timeouts               |          0 |                      0 |

PR75's clean behavior on rows 26 and 28 remains clean with PR66.
No new applicable health finding is observed in either pass. Lifecycle,
memory-trim and retirement-fence failure counters are also zero.
All selected-stretch transitions recovered; selected-transition counts
and full-capture episode counts measure different windows.

| Pass | Stretch episodes PR75/PR66 | Stretch frames PR75/PR66 | Stretch ms PR75/PR66 | Duration change | Active tail PR75/PR66 |
| ---- | -------------------------- | ------------------------ | -------------------- | --------------- | --------------------- |
| 1    | 19 / 17                    | 83 / 69                  | 5139.496 / 4143.532  | -995.964 ms     | False / False         |
| 2    | 17 / 18                    | 77 / 78                  | 4584.015 / 4955.678  | +371.663 ms     | False / False         |

The repeat's stretch duration increased although its pooled mean fell.
Raw cumulative acceptance is false in all four passes. Each retains the
six-frame observed maximum against the fixed two-frame stretch cutoff as
DIAGNOSTIC_ONLY because settling imposes stretch. Each also retains the
scaled-presentation `VendorEvaluated` gate against proven native-AA
`NativeOriginal` output as CONTRACT_MISMATCH. Native both-eye proof
supports that exception; other gates remain applicable. Neither excluded
gate changes the MET health result. No active tail or incomplete stereo
cycle remains at stop.

<details>
<summary>Every switch: two-pass mean and SE</summary>

Times are ms; negative change is lower. Each route uses two observations
per build. Per-pass values are preserved in the next table.

| Row | Switch                            | PR75 mean | PR66 mean |            Change | ±SE PR75 | ±SE PR66 |
| --- | --------------------------------- | --------: | --------: | ----------------: | -------: | -------: |
| 1   | DLSS Hoshipa -> NONE              |     745.0 |     722.4 |   -22.573 (-3.0%) |     ±7.2 |    ±42.2 |
| 2   | NONE -> TAA                       |     177.8 |     168.6 |    -9.199 (-5.2%) |     ±8.4 |     ±0.9 |
| 3   | TAA -> DLAA                       |     284.6 |     248.8 |  -35.766 (-12.6%) |    ±17.6 |     ±1.4 |
| 4   | DLAA -> DLSS Hoshipa              |     994.3 |     977.7 |   -16.566 (-1.7%) |    ±45.3 |    ±66.2 |
| 5   | DLSS Hoshipa -> DLSS UQ           |   1,108.7 |   1,094.6 |   -14.110 (-1.3%) |   ±123.4 |    ±56.4 |
| 6   | DLSS UQ -> DLSS Quality           |   1,129.7 |   1,140.8 |   +11.072 (+1.0%) |    ±20.9 |    ±55.6 |
| 7   | DLSS Quality -> DLSS Balanced     |   1,244.5 |   1,257.0 |   +12.427 (+1.0%) |    ±86.2 |    ±54.4 |
| 8   | DLSS Balanced -> DLSS Performance |   1,244.9 |   1,229.6 |   -15.366 (-1.2%) |    ±92.4 |    ±38.0 |
| 9   | DLSS Performance -> DLSS UP       |   1,179.8 |   1,190.3 |   +10.538 (+0.9%) |    ±74.8 |    ±67.3 |
| 10  | DLSS UP -> DLAA                   |     786.0 |     770.1 |   -15.919 (-2.0%) |    ±34.2 |     ±9.2 |
| 11  | DLAA -> TAA                       |     445.0 |     449.4 |    +4.426 (+1.0%) |    ±14.7 |    ±13.8 |
| 12  | TAA -> NONE                       |     176.5 |     177.4 |    +0.916 (+0.5%) |     ±4.3 |     ±6.1 |
| 13  | NONE -> FSR3 Native AA            |     671.8 |     694.2 |   +22.398 (+3.3%) |    ±84.0 |    ±52.9 |
| 14  | FSR3 Native AA -> FSR3 Hoshipa    |   1,343.2 |   1,364.4 |   +21.295 (+1.6%) |    ±30.4 |    ±13.5 |
| 15  | FSR3 Hoshipa -> FSR3 UQ           |   1,042.8 |     899.9 | -142.839 (-13.7%) |   ±348.4 |     ±3.7 |
| 16  | FSR3 UQ -> FSR3 Quality           |     761.2 |     717.4 |   -43.755 (-5.7%) |    ±20.1 |    ±14.4 |
| 17  | FSR3 Quality -> FSR3 Balanced     |     735.6 |     745.7 |   +10.080 (+1.4%) |     ±6.6 |    ±26.2 |
| 18  | FSR3 Balanced -> FSR3 Performance |     738.7 |     756.0 |   +17.339 (+2.3%) |    ±10.3 |     ±4.9 |
| 19  | FSR3 Performance -> FSR3 UP       |     743.9 |     754.3 |   +10.411 (+1.4%) |    ±16.5 |    ±27.0 |
| 20  | FSR3 UP -> FSR3 Native AA         |     726.7 |     908.9 | +182.164 (+25.1%) |    ±20.9 |   ±169.1 |
| 21  | FSR3 Native AA -> TAA             |     454.0 |     482.5 |   +28.427 (+6.3%) |     ±6.5 |    ±19.0 |
| 22  | TAA -> NONE                       |     169.9 |     170.2 |    +0.284 (+0.2%) |     ±1.2 |     ±0.3 |
| 23  | NONE -> DLAA                      |     275.4 |     256.9 |   -18.486 (-6.7%) |    ±26.7 |     ±6.2 |
| 24  | DLAA -> FSR3 Native AA            |   1,088.5 |   1,159.1 |   +70.527 (+6.5%) |     ±6.7 |    ±25.6 |
| 25  | FSR3 Native AA -> DLSS Hoshipa    |   1,689.4 |   1,700.2 |   +10.756 (+0.6%) |   ±154.8 |   ±156.6 |
| 26  | DLSS Hoshipa -> FSR3 Hoshipa      |   1,394.5 |     917.1 | -477.400 (-34.2%) |     ±0.1 |    ±15.8 |
| 27  | FSR3 Hoshipa -> NONE              |     738.2 |     785.5 |   +47.284 (+6.4%) |     ±9.8 |    ±33.8 |
| 28  | NONE -> FSR3 UP                   |     907.0 |     947.6 |   +40.562 (+4.5%) |    ±13.2 |    ±36.9 |
| 29  | FSR3 UP -> DLSS UP                |   1,647.3 |   1,689.4 |   +42.109 (+2.6%) |   ±201.1 |   ±242.7 |
| 30  | DLSS UP -> TAA                    |     695.9 |     688.8 |    -7.078 (-1.0%) |     ±9.5 |     ±7.8 |
| 31  | TAA -> FSR3 Native AA             |     646.9 |     618.9 |   -27.932 (-4.3%) |    ±57.3 |    ±41.6 |
| 32  | FSR3 Native AA -> NONE            |     482.5 |     459.1 |   -23.386 (-4.8%) |    ±18.5 |     ±8.8 |
| 33  | NONE -> DLAA                      |     272.5 |     264.6 |    -7.879 (-2.9%) |    ±14.5 |    ±11.6 |

</details>

<details>
<summary>Every switch in each pass: strict completion and relatch</summary>

All cells are PR75 / PR66. Strict and relatch timings use the retained
qualification-dispatch clock. Relatch requires first exact new-generation
proof; n/a is inapplicable/missing, never zero. All four terminal and Task 2
classifications for each row are PASS.

| Row | P1 strict ms        | P1 change ms | P2 strict ms        | P2 change ms | P1 strict frames | P2 strict frames | P1 relatch ms       | P2 relatch ms       |
| --- | ------------------- | ------------ | ------------------- | ------------ | ---------------- | ---------------- | ------------------- | ------------------- |
| 1   | 737.870 / 680.250   | -57.620      | 752.175 / 764.649   | +12.474      | 16.000 / 15.000  | 15.000 / 16.000  | 503.666 / 451.448   | 527.149 / 523.896   |
| 2   | 169.350 / 169.466   | +0.117       | 186.179 / 167.665   | -18.514      | 3.000 / 4.000    | 4.000 / 4.000    | n/a / n/a           | n/a / n/a           |
| 3   | 302.164 / 250.227   | -51.936      | 266.961 / 247.366   | -19.595      | 5.000 / 3.000    | 3.000 / 3.000    | n/a / n/a           | n/a / n/a           |
| 4   | 1039.596 / 911.526  | -128.070     | 948.929 / 1043.867  | +94.938      | 18.000 / 17.000  | 17.000 / 19.000  | 785.229 / 670.423   | 690.533 / 772.572   |
| 5   | 1232.115 / 1151.036 | -81.078      | 985.326 / 1038.184  | +52.858      | 17.000 / 17.000  | 18.000 / 19.000  | 959.605 / 904.847   | 728.784 / 761.827   |
| 6   | 1108.823 / 1085.145 | -23.677      | 1150.617 / 1196.438 | +45.822      | 21.000 / 21.000  | 22.000 / 23.000  | 854.161 / 830.726   | 892.737 / 915.181   |
| 7   | 1330.702 / 1311.362 | -19.339      | 1158.350 / 1202.543 | +44.192      | 23.000 / 21.000  | 21.000 / 22.000  | 1072.887 / 1035.316 | 894.476 / 917.597   |
| 8   | 1337.321 / 1267.597 | -69.724      | 1152.575 / 1191.568 | +38.993      | 23.000 / 21.000  | 22.000 / 22.000  | 1069.308 / 996.634  | 883.022 / 896.100   |
| 9   | 1254.639 / 1257.584 | +2.944       | 1104.947 / 1123.080 | +18.132      | 21.000 / 21.000  | 22.000 / 21.000  | 995.155 / 990.309   | 853.848 / 848.555   |
| 10  | 751.836 / 760.848   | +9.013       | 820.141 / 779.290   | -40.851      | 14.000 / 15.000  | 14.000 / 14.000  | 527.450 / 545.917   | 575.351 / 558.579   |
| 11  | 459.711 / 463.236   | +3.526       | 430.311 / 435.637   | +5.327       | 9.000 / 11.000   | 10.000 / 10.000  | 174.745 / 195.942   | 168.396 / 162.582   |
| 12  | 180.788 / 171.239   | -9.549       | 172.146 / 183.527   | +11.382      | 4.000 / 4.000    | 4.000 / 3.000    | n/a / n/a           | n/a / n/a           |
| 13  | 755.790 / 747.166   | -8.623       | 587.880 / 641.300   | +53.420      | 10.000 / 10.000  | 11.000 / 10.000  | 713.906 / 706.069   | 545.669 / 594.114   |
| 14  | 1373.542 / 1350.983 | -22.558      | 1312.764 / 1377.913 | +65.149      | 28.000 / 29.000  | 27.000 / 28.000  | 1156.891 / 1133.803 | 1101.347 / 1120.356 |
| 15  | 694.339 / 903.650   | +209.311     | 1391.161 / 896.172  | -494.989     | 15.000 / 19.000  | 26.000 / 19.000  | 560.640 / 561.661   | 666.953 / 558.729   |
| 16  | 741.027 / 731.838   | -9.190       | 781.311 / 702.990   | -78.320      | 16.000 / 16.000  | 17.000 / 16.000  | 562.594 / 559.842   | 561.530 / 568.091   |
| 17  | 742.216 / 719.437   | -22.779      | 728.992 / 771.931   | +42.939      | 16.000 / 16.000  | 16.000 / 17.000  | 560.799 / 545.157   | 548.474 / 552.657   |
| 18  | 748.952 / 760.866   | +11.913      | 728.398 / 751.162   | +22.764      | 16.000 / 16.000  | 16.000 / 16.000  | 575.214 / 584.199   | 558.411 / 576.083   |
| 19  | 760.402 / 781.359   | +20.957      | 727.459 / 727.324   | -0.135       | 16.000 / 16.000  | 16.000 / 16.000  | 589.019 / 606.125   | 558.254 / 555.956   |
| 20  | 705.867 / 739.768   | +33.901      | 747.601 / 1078.028  | +330.427     | 15.000 / 15.000  | 16.000 / 21.000  | 486.186 / 477.821   | 524.715 / 853.302   |
| 21  | 460.578 / 463.465   | +2.887       | 447.482 / 501.448   | +53.966      | 11.000 / 11.000  | 11.000 / 10.000  | n/a / n/a           | n/a / n/a           |
| 22  | 171.103 / 170.549   | -0.555       | 168.778 / 169.901   | +1.122       | 4.000 / 4.000    | 4.000 / 4.000    | n/a / n/a           | n/a / n/a           |
| 23  | 248.704 / 263.125   | +14.421      | 302.104 / 250.712   | -51.393      | 4.000 / 4.000    | 4.000 / 3.000    | n/a / n/a           | n/a / n/a           |
| 24  | 1095.201 / 1184.660 | +89.458      | 1081.859 / 1133.454 | +51.595      | 20.000 / 21.000  | 20.000 / 20.000  | 876.857 / 935.371   | 867.707 / 912.054   |
| 25  | 1534.594 / 1856.757 | +322.163     | 1844.264 / 1543.613 | -300.651     | 28.000 / 34.000  | 34.000 / 28.000  | 1244.870 / 1603.945 | 1587.018 / 1279.758 |
| 26  | 1394.444 / 932.906  | -461.538     | 1394.624 / 901.361  | -493.263     | 27.000 / 18.000  | 28.000 / 17.000  | 1169.076 / 721.216  | 1160.806 / 675.179  |
| 27  | 748.012 / 751.644   | +3.632       | 728.402 / 819.338   | +90.937      | 16.000 / 16.000  | 16.000 / 16.000  | 502.514 / 490.102   | 502.110 / 558.577   |
| 28  | 893.811 / 910.661   | +16.850      | 920.225 / 984.498   | +64.273      | 16.000 / 16.000  | 16.000 / 16.000  | 672.999 / 673.450   | 686.864 / 762.689   |
| 29  | 1848.429 / 1446.692 | -401.737     | 1446.191 / 1932.147 | +485.956     | 34.000 / 28.000  | 28.000 / 34.000  | 1570.211 / 1173.442 | 1179.356 / 1677.335 |
| 30  | 686.412 / 681.036   | -5.376       | 705.395 / 696.614   | -8.780       | 16.000 / 15.000  | 16.000 / 15.000  | 458.378 / 445.611   | 484.032 / 464.188   |
| 31  | 704.150 / 577.394   | -126.756     | 589.605 / 660.497   | +70.891      | 11.000 / 10.000  | 10.000 / 10.000  | 660.982 / 538.000   | 548.418 / 617.707   |
| 32  | 463.999 / 450.355   | -13.644      | 501.027 / 467.899   | -33.128      | 11.000 / 11.000  | 10.000 / 10.000  | n/a / n/a           | n/a / n/a           |
| 33  | 258.051 / 276.262   | +18.211      | 287.002 / 253.033   | -33.969      | 3.000 / 3.000    | 4.000 / 3.000    | n/a / n/a           | n/a / n/a           |

</details>

<details>
<summary>Exact builds, memory, evidence and limits</summary>

| Identity             | PR75                                                               | PR66 integration build                                             |
| -------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Compiled source      | `c615779a903d7e3f8f95c7edcf303a02ad08dced`                         | `a09e1cc77de098f85e74e6d5bb341dc184f83640`                         |
| Renderer source/base | `c615779a903d7e3f8f95c7edcf303a02ad08dced`                         | `a09e1cc77de098f85e74e6d5bb341dc184f83640`                         |
| Main-VR base         | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         | `bf4ae54a7d49620c41cb32ee9ecfd44657688ead`                         |
| Run ID               | `nvidia-2026-09-10T17-12-40-813Z`                                  | `renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z`               |
| Build ID             | `cbef73dfc7a11e26e01ad8ecdca03c8d45d4c6549ddfd3a51b8a0c537499014d` | `9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757` |
| DLL SHA-256          | `7ec4ccdb57f42183343fd9b5fe60aab643b86ee38489309ce6ad674589a904a9` | `aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461` |
| DLL size (bytes)     | `28060672`                                                         | `28062720`                                                         |

Both used RTX 5070 Ti Laptop GPU, the same Dragonsreach position, 1512 x
1680 output per eye, DLSS K, explicit FSR3, foveation 0.3/0.3/0.7,
five-second pre-dispatch pacing and a 20-second strict deadline. Weather
was Helios_SkyrimCloudyTU in both; game hour differed (17.900 vs 7.947).
The retained toolchain fingerprints differ. Driver, power, headset refresh
and complete modlist/cache equivalence are not established. No causal
or statistically significant gain is claimed from these ordered passes.
Fresh resolved GPU samples are unavailable for an FPS or steady-state
GPU-cost comparison.

Memory classification is inconclusive for both builds. Values below are
end minus start in MiB, except texture count. Positive values are growth.

| Metric                   | PR75 pass 1 | PR66 pass 1 | PR75 pass 2 | PR66 pass 2 |
| ------------------------ | ----------: | ----------: | ----------: | ----------: |
| Process private MiB      |    -417.043 |    -114.105 |    -427.484 |    -213.449 |
| System commit MiB        |    -405.660 |    -101.223 |    -478.430 |      40.145 |
| DXGI usage MiB           |    -869.980 |    -674.609 |    -422.961 |    -323.090 |
| Tracked live textures    |     213.000 |     256.000 |     207.000 |     233.000 |
| Tracked live texture MiB |    2270.931 |    2424.026 |    2265.598 |    2349.432 |

The PR66 repeat has +40.145 MiB system-commit growth, while PR75's repeat
decreased. Texture trackers were reset at each pass start; tracked-count
deltas do not by themselves establish retained allocation or a leak.
All six memory boundaries, cooldown, ratios and exact classification
inputs remain in each saved summary and complete ledger detail record.

Each run's deployed DLL was verified at measurement finalization against
its adjacent manifest, AIO receipt and runtime producer Build ID. This
comparison uses those preserved identities. PR75's old DLL is not
reinterpreted as the currently deployed candidate. Captures are verified
inactive and both journals are flushed; reporting is COMPLETE.

The maintained comparison workflow audited all 1,056 paired numeric timing
cells. The canonical local ledger retains every original summary field,
this complete PR75-to-PR66 comparison, all transition/pass deltas, health
and memory details, provenance, and the unrounded mean/SE inputs.
Field-for-field reconstruction and historical-cell preservation passed.
Raw evidence remains local. Separate `csx-render-scale-pr-v1` release
qualification remains pending; this tuning assay does not replace it.

</details>

<!-- end pr66-vs-pr75-nvidia-comparison-v1 -->

[Canonical comparison ledger](vr-render-scale-ledger.md).

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                     | Candidate                                                                                                       |
| ----------------------- | -------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| Run                     | nvidia-2026-09-10T17-12-40-813Z                                                              | renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z                                                              |
| Renderer base           | c615779a903d7e3f8f95c7edcf303a02ad08dced                                                     | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                     | bf4ae54a7d49620c41cb32ee9ecfd44657688ead                                                                        |
| Compiled source         | c615779a903d7e3f8f95c7edcf303a02ad08dced                                                     | a09e1cc77de098f85e74e6d5bb341dc184f83640                                                                        |
| Build ID                | cbef73dfc7a11e26e01ad8ecdca03c8d45d4c6549ddfd3a51b8a0c537499014d                             | 9b08428afdafd770435b8ec562537cec28d733c94c12bf020914b9461785d757                                                |
| DLL SHA-256             | 7ec4ccdb57f42183343fd9b5fe60aab643b86ee38489309ce6ad674589a904a9                             | aecc263e8df015df8b6f961b670c9365ae1b911222e2667f99b9941595aa6461                                                |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-2026-09-10T17-12-40-813Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z |

Assessment limits: retained_context_not_matched:scene; retained_context_not_matched:toolchain; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 816.198/793.336 | -2.801       | 10/9        | 0/0          | 0/0                 | none             | MET/MET             |
| nvidia | 2    | 33/33    | 804.581/807.126 | 0.316        | 10/10       | 0/0          | 0/0                 | none             | MET/MET             |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta    | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | -------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 772.134   | 735.095   | -37.039  | -4.797  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920    | 13.400    | -0.520   | -3.736  |
| nvidia | 1    | Relatch proof total        | ms          | 19303.343 | 18377.376 | -925.967 | -4.797  |
| nvidia | 1    | Relatch proof total        | frames      | 348       | 335       | -13      | -3.736  |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 816.198   | 793.336   | -22.862  | -2.801  |
| nvidia | 1    | Strict completion mean     | frames      | 15.364    | 15.091    | -0.273   | -1.775  |
| nvidia | 1    | Strict completion total    | ms          | 26934.537 | 26180.089 | -754.448 | -2.801  |
| nvidia | 1    | Strict completion total    | frames      | 507       | 498       | -9       | -1.775  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 17        | -2       | -10.526 |
| nvidia | 1    | Stretch completed total    | frames      | 83        | 69        | -14      | -16.867 |
| nvidia | 1    | Stretch completed total    | ms          | 5139.496  | 4143.532  | -995.964 | -19.379 |
| nvidia | 1    | Stretch longest episode    | ms          | 416.204   | 376.746   | -39.458  | -9.480  |
| nvidia | 2    | Relatch proof mean         | ms          | 731.838   | 747.347   | 15.508   | 2.119   |
| nvidia | 2    | Relatch proof mean         | frames      | 13.880    | 13.760    | -0.120   | -0.865  |
| nvidia | 2    | Relatch proof total        | ms          | 18295.960 | 18683.664 | 387.704  | 2.119   |
| nvidia | 2    | Relatch proof total        | frames      | 347       | 344       | -3       | -0.865  |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0        | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 804.581   | 807.126   | 2.545    | 0.316   |
| nvidia | 2    | Strict completion mean     | frames      | 15.697    | 15.303    | -0.394   | -2.510  |
| nvidia | 2    | Strict completion total    | ms          | 26551.180 | 26635.148 | 83.969   | 0.316   |
| nvidia | 2    | Strict completion total    | frames      | 518       | 505       | -13      | -2.510  |
| nvidia | 2    | Stretch completed episodes | episodes    | 17        | 18        | 1        | 5.882   |
| nvidia | 2    | Stretch completed total    | frames      | 77        | 78        | 1        | 1.299   |
| nvidia | 2    | Stretch completed total    | ms          | 4584.015  | 4955.678  | 371.663  | 8.108   |
| nvidia | 2    | Stretch longest episode    | ms          | 389.986   | 408.503   | 18.516   | 4.748   |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 737.870 / 680.250   | -57.620  | -7.809  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.350 / 169.466   | 0.117    | 0.069   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 302.164 / 250.227   | -51.936  | -17.188 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1039.596 / 911.526  | -128.070 | -12.319 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1232.115 / 1151.036 | -81.078  | -6.580  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1108.823 / 1085.145 | -23.677  | -2.135  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1330.702 / 1311.362 | -19.339  | -1.453  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1337.321 / 1267.597 | -69.724  | -5.214  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1254.639 / 1257.584 | 2.944    | 0.235   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 751.836 / 760.848   | 9.013    | 1.199   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 459.711 / 463.236   | 3.526    | 0.767   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 180.788 / 171.239   | -9.549   | -5.282  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 755.790 / 747.166   | -8.623   | -1.141  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1373.542 / 1350.983 | -22.558  | -1.642  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 694.339 / 903.650   | 209.311  | 30.145  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 741.027 / 731.838   | -9.190   | -1.240  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 742.216 / 719.437   | -22.779  | -3.069  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 748.952 / 760.866   | 11.913   | 1.591   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 760.402 / 781.359   | 20.957   | 2.756   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 705.867 / 739.768   | 33.901   | 4.803   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 460.578 / 463.465   | 2.887    | 0.627   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 171.103 / 170.549   | -0.555   | -0.324  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 248.704 / 263.125   | 14.421   | 5.798   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1095.201 / 1184.660 | 89.458   | 8.168   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1534.594 / 1856.757 | 322.163  | 20.993  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1394.444 / 932.906  | -461.538 | -33.098 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 748.012 / 751.644   | 3.632    | 0.486   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 893.811 / 910.661   | 16.850   | 1.885   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1848.429 / 1446.692 | -401.737 | -21.734 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 686.412 / 681.036   | -5.376   | -0.783  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 704.150 / 577.394   | -126.756 | -18.001 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 463.999 / 450.355   | -13.644  | -2.941  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 258.051 / 276.262   | 18.211   | 7.057   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 504.202 / 501.629   | 737.870 / 680.250   | 233.667 / 178.621 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2571,"dispatchToBlockedOrPreparationMs":385.2541,"firstNewGenerationToCleanupDrainedMs":234.2036,"firstPhysicalMutationToFirstNewGenerationMs":115.155,"presentationToStrictCompletionMs":233.6673}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4113,"dispatchToBlockedOrPreparationMs":331.8357,"firstNewGenerationToCleanupDrainedMs":228.8014,"firstPhysicalMutationToFirstNewGenerationMs":116.2013,"presentationToStrictCompletionMs":178.6206}   |
| 2   | 169.350 / 169.466   | 169.350 / 169.466   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.3495,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.466,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 302.164 / 250.227   | 302.164 / 250.227   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":302.1637,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.2275,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 828.588 / 789.446   | 957.035 / 829.726   | 128.447 / 40.280  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9363,"dispatchToBlockedOrPreparationMs":390.4597,"firstNewGenerationToCleanupDrainedMs":171.8067,"firstPhysicalMutationToFirstNewGenerationMs":390.8327,"presentationToStrictCompletionMs":211.0073}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3058,"dispatchToBlockedOrPreparationMs":343.3656,"firstNewGenerationToCleanupDrainedMs":159.3035,"firstPhysicalMutationToFirstNewGenerationMs":323.7512,"presentationToStrictCompletionMs":122.0799}   |
| 5   | 1008.163 / 945.179  | 1146.982 / 1068.436 | 138.818 / 123.257 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1408,"dispatchToBlockedOrPreparationMs":369.5332,"firstNewGenerationToCleanupDrainedMs":187.3764,"firstPhysicalMutationToFirstNewGenerationMs":586.9312,"presentationToStrictCompletionMs":223.9512}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4538,"dispatchToBlockedOrPreparationMs":359.0552,"firstNewGenerationToCleanupDrainedMs":163.5892,"firstPhysicalMutationToFirstNewGenerationMs":542.3382,"presentationToStrictCompletionMs":205.8572}   |
| 6   | 897.260 / 877.562   | 1026.015 / 1005.163 | 128.756 / 127.602 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6782,"dispatchToBlockedOrPreparationMs":351.3709,"firstNewGenerationToCleanupDrainedMs":171.8544,"firstPhysicalMutationToFirstNewGenerationMs":499.112,"presentationToStrictCompletionMs":211.563}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9724,"dispatchToBlockedOrPreparationMs":353.8686,"firstNewGenerationToCleanupDrainedMs":174.4377,"firstPhysicalMutationToFirstNewGenerationMs":472.8845,"presentationToStrictCompletionMs":207.5839}   |
| 7   | 1115.115 / 1092.614 | 1248.672 / 1227.587 | 133.557 / 134.973 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4318,"dispatchToBlockedOrPreparationMs":394.0372,"firstNewGenerationToCleanupDrainedMs":175.7851,"firstPhysicalMutationToFirstNewGenerationMs":675.4181,"presentationToStrictCompletionMs":215.5864}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4825,"dispatchToBlockedOrPreparationMs":365.9809,"firstNewGenerationToCleanupDrainedMs":192.2705,"firstPhysicalMutationToFirstNewGenerationMs":665.8528,"presentationToStrictCompletionMs":218.7486}   |
| 8   | 1112.824 / 1048.367 | 1248.528 / 1186.924 | 135.704 / 138.557 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2141,"dispatchToBlockedOrPreparationMs":395.597,"firstNewGenerationToCleanupDrainedMs":179.2205,"firstPhysicalMutationToFirstNewGenerationMs":670.4968,"presentationToStrictCompletionMs":224.4972}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8603,"dispatchToBlockedOrPreparationMs":343.9502,"firstNewGenerationToCleanupDrainedMs":190.2907,"firstPhysicalMutationToFirstNewGenerationMs":648.823,"presentationToStrictCompletionMs":219.2301}    |
| 9   | 1037.489 / 1031.843 | 1171.499 / 1174.671 | 134.010 / 142.827 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3261,"dispatchToBlockedOrPreparationMs":355.5748,"firstNewGenerationToCleanupDrainedMs":176.3439,"firstPhysicalMutationToFirstNewGenerationMs":636.2544,"presentationToStrictCompletionMs":217.1501}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3977,"dispatchToBlockedOrPreparationMs":360.4948,"firstNewGenerationToCleanupDrainedMs":184.3618,"firstPhysicalMutationToFirstNewGenerationMs":626.4163,"presentationToStrictCompletionMs":225.7405}   |
| 10  | 575.211 / 590.580   | 751.836 / 760.848   | 176.625 / 170.268 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0838,"dispatchToBlockedOrPreparationMs":342.7072,"firstNewGenerationToCleanupDrainedMs":224.3855,"firstPhysicalMutationToFirstNewGenerationMs":181.659,"presentationToStrictCompletionMs":176.6248}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5661,"dispatchToBlockedOrPreparationMs":360.7792,"firstNewGenerationToCleanupDrainedMs":214.9309,"firstPhysicalMutationToFirstNewGenerationMs":181.5719,"presentationToStrictCompletionMs":170.2681}   |
| 11  | 174.864 / 197.187   | 459.711 / 463.236   | 284.846 / 266.049 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4013,"dispatchToBlockedOrPreparationMs":129.9625,"firstNewGenerationToCleanupDrainedMs":284.9654,"firstPhysicalMutationToFirstNewGenerationMs":41.3813,"presentationToStrictCompletionMs":284.8461}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6416,"dispatchToBlockedOrPreparationMs":149.8394,"firstNewGenerationToCleanupDrainedMs":267.2946,"firstPhysicalMutationToFirstNewGenerationMs":41.4606,"presentationToStrictCompletionMs":266.0491}    |
| 12  | 180.788 / 171.239   | 180.788 / 171.239   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":180.7878,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.239,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 13  | 755.790 / 747.166   | 755.790 / 747.166   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":177.5634,"dispatchToBlockedOrPreparationMs":420.5629,"firstNewGenerationToCleanupDrainedMs":41.8837,"firstPhysicalMutationToFirstNewGenerationMs":115.7796,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":192.6286,"dispatchToBlockedOrPreparationMs":394.74,"firstNewGenerationToCleanupDrainedMs":41.0968,"firstPhysicalMutationToFirstNewGenerationMs":118.7009,"presentationToStrictCompletionMs":0}           |
| 14  | 1242.026 / 1222.442 | 1327.843 / 1306.234 | 85.818 / 83.792   | {"blockedOrPreparationToFirstPhysicalMutationMs":266.7799,"dispatchToBlockedOrPreparationMs":436.5879,"firstNewGenerationToCleanupDrainedMs":170.9521,"firstPhysicalMutationToFirstNewGenerationMs":453.5235,"presentationToStrictCompletionMs":131.5163} | {"blockedOrPreparationToFirstPhysicalMutationMs":260.1297,"dispatchToBlockedOrPreparationMs":447.4412,"firstNewGenerationToCleanupDrainedMs":172.4308,"firstPhysicalMutationToFirstNewGenerationMs":426.2326,"presentationToStrictCompletionMs":128.541}  |
| 15  | 694.339 / 903.650   | 602.559 / 561.963   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8021,"dispatchToBlockedOrPreparationMs":356.5456,"firstNewGenerationToCleanupDrainedMs":41.9193,"firstPhysicalMutationToFirstNewGenerationMs":199.2921,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0595,"dispatchToBlockedOrPreparationMs":360.1568,"firstNewGenerationToCleanupDrainedMs":0.3022,"firstPhysicalMutationToFirstNewGenerationMs":197.4448,"presentationToStrictCompletionMs":0}            |
| 16  | 741.027 / 731.838   | 606.049 / 602.543   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9922,"dispatchToBlockedOrPreparationMs":361.9381,"firstNewGenerationToCleanupDrainedMs":43.455,"firstPhysicalMutationToFirstNewGenerationMs":196.6633,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4262,"dispatchToBlockedOrPreparationMs":343.501,"firstNewGenerationToCleanupDrainedMs":42.7009,"firstPhysicalMutationToFirstNewGenerationMs":211.9146,"presentationToStrictCompletionMs":0}            |
| 17  | 742.216 / 719.437   | 603.352 / 587.405   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5218,"dispatchToBlockedOrPreparationMs":352.1717,"firstNewGenerationToCleanupDrainedMs":42.5529,"firstPhysicalMutationToFirstNewGenerationMs":204.1058,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4142,"dispatchToBlockedOrPreparationMs":350.0745,"firstNewGenerationToCleanupDrainedMs":42.2475,"firstPhysicalMutationToFirstNewGenerationMs":190.6686,"presentationToStrictCompletionMs":0}           |
| 18  | 748.952 / 760.866   | 617.515 / 630.628   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4239,"dispatchToBlockedOrPreparationMs":373.3099,"firstNewGenerationToCleanupDrainedMs":42.301,"firstPhysicalMutationToFirstNewGenerationMs":197.4799,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.9396,"dispatchToBlockedOrPreparationMs":378.3542,"firstNewGenerationToCleanupDrainedMs":46.4289,"firstPhysicalMutationToFirstNewGenerationMs":200.9055,"presentationToStrictCompletionMs":0}           |
| 19  | 760.402 / 781.359   | 629.783 / 650.456   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.113,"dispatchToBlockedOrPreparationMs":387.3036,"firstNewGenerationToCleanupDrainedMs":40.7638,"firstPhysicalMutationToFirstNewGenerationMs":197.6027,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":5.493,"dispatchToBlockedOrPreparationMs":388.6542,"firstNewGenerationToCleanupDrainedMs":44.3308,"firstPhysicalMutationToFirstNewGenerationMs":211.9777,"presentationToStrictCompletionMs":0}            |
| 20  | 531.149 / 530.869   | 705.867 / 739.768   | 174.719 / 208.899 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3373,"dispatchToBlockedOrPreparationMs":355.9118,"firstNewGenerationToCleanupDrainedMs":219.6812,"firstPhysicalMutationToFirstNewGenerationMs":125.9372,"presentationToStrictCompletionMs":174.7188}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5574,"dispatchToBlockedOrPreparationMs":341.9606,"firstNewGenerationToCleanupDrainedMs":261.9474,"firstPhysicalMutationToFirstNewGenerationMs":131.3026,"presentationToStrictCompletionMs":208.8989}   |
| 21  | 192.487 / 205.525   | 460.578 / 463.465   | 268.091 / 257.940 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":127.8081,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":268.0913}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":141.074,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":257.9402}              |
| 22  | 171.103 / 170.549   | 171.103 / 170.549   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.1033,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.5487,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 248.704 / 263.125   | 248.704 / 263.125   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":248.7041,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":263.1251,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 919.602 / 993.040   | 1095.201 / 1184.660 | 175.600 / 191.620 | {"blockedOrPreparationToFirstPhysicalMutationMs":275.3012,"dispatchToBlockedOrPreparationMs":455.4514,"firstNewGenerationToCleanupDrainedMs":218.3444,"firstPhysicalMutationToFirstNewGenerationMs":146.1044,"presentationToStrictCompletionMs":175.5995} | {"blockedOrPreparationToFirstPhysicalMutationMs":302.0873,"dispatchToBlockedOrPreparationMs":459.3461,"firstNewGenerationToCleanupDrainedMs":249.2883,"firstPhysicalMutationToFirstNewGenerationMs":173.9381,"presentationToStrictCompletionMs":191.6197} |
| 25  | 1292.599 / 1645.743 | 1442.459 / 1774.359 | 149.861 / 128.616 | {"blockedOrPreparationToFirstPhysicalMutationMs":307.2203,"dispatchToBlockedOrPreparationMs":407.4189,"firstNewGenerationToCleanupDrainedMs":197.5888,"firstPhysicalMutationToFirstNewGenerationMs":530.2312,"presentationToStrictCompletionMs":241.9953} | {"blockedOrPreparationToFirstPhysicalMutationMs":691.2409,"dispatchToBlockedOrPreparationMs":433.694,"firstNewGenerationToCleanupDrainedMs":170.4138,"firstPhysicalMutationToFirstNewGenerationMs":479.0101,"presentationToStrictCompletionMs":211.0144}  |
| 26  | 1213.868 / 803.459  | 1347.050 / 888.892  | 133.183 / 85.433  | {"blockedOrPreparationToFirstPhysicalMutationMs":284.4247,"dispatchToBlockedOrPreparationMs":403.937,"firstNewGenerationToCleanupDrainedMs":177.9745,"firstPhysicalMutationToFirstNewGenerationMs":480.7139,"presentationToStrictCompletionMs":180.5763}  | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7511,"dispatchToBlockedOrPreparationMs":381.6439,"firstNewGenerationToCleanupDrainedMs":167.6765,"firstPhysicalMutationToFirstNewGenerationMs":334.8207,"presentationToStrictCompletionMs":129.4467}   |
| 27  | 504.335 / 490.910   | 748.012 / 751.644   | 243.677 / 260.734 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0888,"dispatchToBlockedOrPreparationMs":355.4306,"firstNewGenerationToCleanupDrainedMs":245.4977,"firstPhysicalMutationToFirstNewGenerationMs":142.9948,"presentationToStrictCompletionMs":243.6765}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.59,"dispatchToBlockedOrPreparationMs":345.411,"firstNewGenerationToCleanupDrainedMs":261.5421,"firstPhysicalMutationToFirstNewGenerationMs":140.1008,"presentationToStrictCompletionMs":260.7342}      |
| 28  | 714.349 / 810.416   | 846.833 / 860.954   | 132.484 / 50.538  | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1202,"dispatchToBlockedOrPreparationMs":393.8802,"firstNewGenerationToCleanupDrainedMs":173.8338,"firstPhysicalMutationToFirstNewGenerationMs":275.9985,"presentationToStrictCompletionMs":179.4622}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5332,"dispatchToBlockedOrPreparationMs":391.0506,"firstNewGenerationToCleanupDrainedMs":187.5037,"firstPhysicalMutationToFirstNewGenerationMs":278.8664,"presentationToStrictCompletionMs":100.2448}   |
| 29  | 1621.757 / 1218.375 | 1765.597 / 1356.004 | 143.840 / 137.629 | {"blockedOrPreparationToFirstPhysicalMutationMs":637.9645,"dispatchToBlockedOrPreparationMs":422.8284,"firstNewGenerationToCleanupDrainedMs":195.3852,"firstPhysicalMutationToFirstNewGenerationMs":509.4186,"presentationToStrictCompletionMs":226.6717} | {"blockedOrPreparationToFirstPhysicalMutationMs":292.5704,"dispatchToBlockedOrPreparationMs":402.8201,"firstNewGenerationToCleanupDrainedMs":182.5617,"firstPhysicalMutationToFirstNewGenerationMs":478.0519,"presentationToStrictCompletionMs":228.3174} |
| 30  | 458.620 / 492.154   | 686.412 / 681.036   | 227.792 / 188.881 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0536,"dispatchToBlockedOrPreparationMs":341.0891,"firstNewGenerationToCleanupDrainedMs":228.0342,"firstPhysicalMutationToFirstNewGenerationMs":114.2349,"presentationToStrictCompletionMs":227.7921}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2695,"dispatchToBlockedOrPreparationMs":336.9345,"firstNewGenerationToCleanupDrainedMs":235.4251,"firstPhysicalMutationToFirstNewGenerationMs":104.4067,"presentationToStrictCompletionMs":188.8815}   |
| 31  | 704.150 / 577.394   | 704.150 / 577.394   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.9457,"dispatchToBlockedOrPreparationMs":410.1055,"firstNewGenerationToCleanupDrainedMs":43.1683,"firstPhysicalMutationToFirstNewGenerationMs":197.9305,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":50.1296,"dispatchToBlockedOrPreparationMs":401.3615,"firstNewGenerationToCleanupDrainedMs":39.3942,"firstPhysicalMutationToFirstNewGenerationMs":86.5087,"presentationToStrictCompletionMs":0}           |
| 32  | 192.739 / 184.484   | 463.999 / 450.355   | 271.260 / 265.870 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.0303,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":271.2596}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.8185,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":265.8704}             |
| 33  | 258.051 / 276.262   | 258.051 / 276.262   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.0512,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":276.2618,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 11 / 10                  | -1           | 503.666 / 451.448    | -52.218  | 16 / 15           | -1           |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 5 / 3             | -2           |
| 4   | 13 / 12                  | -1           | 785.229 / 670.423    | -114.806 | 18 / 17           | -1           |
| 5   | 12 / 12                  | 0            | 959.605 / 904.847    | -54.758  | 17 / 17           | 0            |
| 6   | 16 / 16                  | 0            | 854.161 / 830.726    | -23.436  | 21 / 21           | 0            |
| 7   | 18 / 16                  | -2           | 1072.887 / 1035.316  | -37.571  | 23 / 21           | -2           |
| 8   | 18 / 16                  | -2           | 1069.308 / 996.634   | -72.674  | 23 / 21           | -2           |
| 9   | 16 / 16                  | 0            | 995.155 / 990.309    | -4.846   | 21 / 21           | 0            |
| 10  | 9 / 10                   | 1            | 527.450 / 545.917    | 18.467   | 14 / 15           | 1            |
| 11  | 3 / 4                    | 1            | 174.745 / 195.942    | 21.196   | 9 / 11            | 2            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 713.906 / 706.069    | -7.836   | 10 / 10           | 0            |
| 14  | 23 / 24                  | 1            | 1156.891 / 1133.803  | -23.088  | 28 / 29           | 1            |
| 15  | 12 / 11                  | -1           | 560.640 / 561.661    | 1.021    | 15 / 19           | 4            |
| 16  | 12 / 12                  | 0            | 562.594 / 559.842    | -2.752   | 16 / 16           | 0            |
| 17  | 12 / 12                  | 0            | 560.799 / 545.157    | -15.642  | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 575.214 / 584.199    | 8.986    | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 589.019 / 606.125    | 17.106   | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 486.186 / 477.821    | -8.366   | 15 / 15           | 0            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 24  | 15 / 16                  | 1            | 876.857 / 935.371    | 58.514   | 20 / 21           | 1            |
| 25  | 23 / 29                  | 6            | 1244.870 / 1603.945  | 359.075  | 28 / 34           | 6            |
| 26  | 22 / 13                  | -9           | 1169.076 / 721.216   | -447.860 | 27 / 18           | -9           |
| 27  | 10 / 10                  | 0            | 502.514 / 490.102    | -12.412  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 672.999 / 673.450    | 0.451    | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1570.211 / 1173.442  | -396.769 | 34 / 28           | -6           |
| 30  | 10 / 10                  | 0            | 458.378 / 445.611    | -12.767  | 16 / 15           | -1           |
| 31  | 10 / 9                   | -1           | 660.982 / 538.000    | -122.982 | 11 / 10           | -1           |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C    | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ----------------- | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 118.764 / 119.890 | 1.126    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 237.279 / 199.802 | -37.477  |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 227.249 / 199.320 | -27.929  |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 383.477 / 371.552 | -11.925  |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 383.075 / 376.746 | -6.329   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 388.058 / 366.685 | -21.373  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 382.998 / 370.248 | -12.750  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 282.735 / 268.924 | -13.811  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 144.766 / 142.698 | -2.068   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 142.321 / 160.236 | 17.915   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 151.116 / 140.158 | -10.957  |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 145.514 / 148.345 | 2.832    |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 143.269 / 154.845 | 11.577   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 416.204 / 363.912 | -52.292  |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 603.126 / 94.387  | -508.739 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 989.547 / 665.784 | -323.763 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0             | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 752.175 / 764.649   | 12.474   | 1.658   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 186.179 / 167.665   | -18.514  | -9.944  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 266.961 / 247.366   | -19.595  | -7.340  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 948.929 / 1043.867  | 94.938   | 10.005  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 985.326 / 1038.184  | 52.858   | 5.365   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1150.617 / 1196.438 | 45.822   | 3.982   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1158.350 / 1202.543 | 44.192   | 3.815   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1152.575 / 1191.568 | 38.993   | 3.383   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1104.947 / 1123.080 | 18.132   | 1.641   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 820.141 / 779.290   | -40.851  | -4.981  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 430.311 / 435.637   | 5.327    | 1.238   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.146 / 183.527   | 11.382   | 6.612   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 587.880 / 641.300   | 53.420   | 9.087   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1312.764 / 1377.913 | 65.149   | 4.963   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 1391.161 / 896.172  | -494.989 | -35.581 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 781.311 / 702.990   | -78.320  | -10.024 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 728.992 / 771.931   | 42.939   | 5.890   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 728.398 / 751.162   | 22.764   | 3.125   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 727.459 / 727.324   | -0.135   | -0.019  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 747.601 / 1078.028  | 330.427  | 44.198  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 447.482 / 501.448   | 53.966   | 12.060  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 168.778 / 169.901   | 1.122    | 0.665   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 302.104 / 250.712   | -51.393  | -17.012 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1081.859 / 1133.454 | 51.595   | 4.769   | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1844.264 / 1543.613 | -300.651 | -16.302 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1394.624 / 901.361  | -493.263 | -35.369 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 728.402 / 819.338   | 90.937   | 12.484  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 920.225 / 984.498   | 64.273   | 6.985   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1446.191 / 1932.147 | 485.956  | 33.602  | 1/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 705.395 / 696.614   | -8.780   | -1.245  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 589.605 / 660.497   | 70.891   | 12.024  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 501.027 / 467.899   | -33.128  | -6.612  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 287.002 / 253.033   | -33.969  | -11.836 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 528.025 / 524.712   | 752.175 / 764.649   | 224.150 / 239.937 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0171,"dispatchToBlockedOrPreparationMs":394.494,"firstNewGenerationToCleanupDrainedMs":225.0262,"firstPhysicalMutationToFirstNewGenerationMs":129.6375,"presentationToStrictCompletionMs":224.1498}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6788,"dispatchToBlockedOrPreparationMs":398.9273,"firstNewGenerationToCleanupDrainedMs":240.7526,"firstPhysicalMutationToFirstNewGenerationMs":121.2898,"presentationToStrictCompletionMs":239.9367}   |
| 2   | 186.179 / 167.665   | 186.179 / 167.665   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":186.179,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":167.665,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 266.961 / 247.366   | 266.961 / 247.366   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":266.9607,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":247.3658,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 733.028 / 816.593   | 863.686 / 956.843   | 130.657 / 140.250 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0353,"dispatchToBlockedOrPreparationMs":366.3901,"firstNewGenerationToCleanupDrainedMs":173.1527,"firstPhysicalMutationToFirstNewGenerationMs":321.1075,"presentationToStrictCompletionMs":215.9006}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6999,"dispatchToBlockedOrPreparationMs":408.322,"firstNewGenerationToCleanupDrainedMs":184.2716,"firstPhysicalMutationToFirstNewGenerationMs":359.55,"presentationToStrictCompletionMs":227.2739}      |
| 5   | 771.941 / 812.136   | 900.685 / 952.986   | 128.744 / 140.851 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5081,"dispatchToBlockedOrPreparationMs":393.9712,"firstNewGenerationToCleanupDrainedMs":171.901,"firstPhysicalMutationToFirstNewGenerationMs":330.3049,"presentationToStrictCompletionMs":213.3851}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0876,"dispatchToBlockedOrPreparationMs":414.3874,"firstNewGenerationToCleanupDrainedMs":191.1592,"firstPhysicalMutationToFirstNewGenerationMs":343.3521,"presentationToStrictCompletionMs":226.0486}   |
| 6   | 935.862 / 963.683   | 1069.127 / 1108.473 | 133.265 / 144.789 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.402,"dispatchToBlockedOrPreparationMs":392.8358,"firstNewGenerationToCleanupDrainedMs":176.3898,"firstPhysicalMutationToFirstNewGenerationMs":496.4992,"presentationToStrictCompletionMs":214.7546}    | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1046,"dispatchToBlockedOrPreparationMs":402.0685,"firstNewGenerationToCleanupDrainedMs":193.2912,"firstPhysicalMutationToFirstNewGenerationMs":508.0083,"presentationToStrictCompletionMs":232.7553}   |
| 7   | 938.661 / 965.785   | 1074.163 / 1113.348 | 135.502 / 147.564 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.43,"dispatchToBlockedOrPreparationMs":389.6013,"firstNewGenerationToCleanupDrainedMs":179.6871,"firstPhysicalMutationToFirstNewGenerationMs":501.4443,"presentationToStrictCompletionMs":219.6894}     | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1118,"dispatchToBlockedOrPreparationMs":391.181,"firstNewGenerationToCleanupDrainedMs":195.7517,"firstPhysicalMutationToFirstNewGenerationMs":522.3038,"presentationToStrictCompletionMs":236.7582}    |
| 8   | 925.830 / 944.879   | 1070.032 / 1097.893 | 144.202 / 153.015 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5518,"dispatchToBlockedOrPreparationMs":392.5166,"firstNewGenerationToCleanupDrainedMs":187.0107,"firstPhysicalMutationToFirstNewGenerationMs":486.9532,"presentationToStrictCompletionMs":226.7447}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.849,"dispatchToBlockedOrPreparationMs":386.0721,"firstNewGenerationToCleanupDrainedMs":201.7934,"firstPhysicalMutationToFirstNewGenerationMs":506.1788,"presentationToStrictCompletionMs":246.6891}    |
| 9   | 896.889 / 892.407   | 1025.732 / 1033.092 | 128.843 / 140.686 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2917,"dispatchToBlockedOrPreparationMs":375.484,"firstNewGenerationToCleanupDrainedMs":171.8837,"firstPhysicalMutationToFirstNewGenerationMs":475.0726,"presentationToStrictCompletionMs":208.0582}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0101,"dispatchToBlockedOrPreparationMs":350.1775,"firstNewGenerationToCleanupDrainedMs":184.5374,"firstPhysicalMutationToFirstNewGenerationMs":494.3675,"presentationToStrictCompletionMs":230.6732}   |
| 10  | 628.633 / 604.626   | 820.141 / 779.290   | 191.508 / 174.664 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4867,"dispatchToBlockedOrPreparationMs":368.3691,"firstNewGenerationToCleanupDrainedMs":244.7901,"firstPhysicalMutationToFirstNewGenerationMs":203.4954,"presentationToStrictCompletionMs":191.5085}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5596,"dispatchToBlockedOrPreparationMs":361.0118,"firstNewGenerationToCleanupDrainedMs":220.7107,"firstPhysicalMutationToFirstNewGenerationMs":193.0078,"presentationToStrictCompletionMs":174.6636}   |
| 11  | 169.660 / 163.343   | 430.311 / 435.637   | 260.651 / 272.294 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.054,"dispatchToBlockedOrPreparationMs":127.2449,"firstNewGenerationToCleanupDrainedMs":261.9149,"firstPhysicalMutationToFirstNewGenerationMs":38.0969,"presentationToStrictCompletionMs":260.6506}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6609,"dispatchToBlockedOrPreparationMs":121.2179,"firstNewGenerationToCleanupDrainedMs":273.0555,"firstPhysicalMutationToFirstNewGenerationMs":37.7029,"presentationToStrictCompletionMs":272.2943}    |
| 12  | 172.146 / 183.527   | 172.146 / 183.527   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.1458,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":183.5274,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 587.880 / 641.300   | 587.880 / 641.300   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.2071,"dispatchToBlockedOrPreparationMs":400.7651,"firstNewGenerationToCleanupDrainedMs":42.2113,"firstPhysicalMutationToFirstNewGenerationMs":92.6966,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":59.0337,"dispatchToBlockedOrPreparationMs":429.1853,"firstNewGenerationToCleanupDrainedMs":47.1866,"firstPhysicalMutationToFirstNewGenerationMs":105.8945,"presentationToStrictCompletionMs":0}          |
| 14  | 1312.764 / 1171.620 | 1268.192 / 1323.721 | 0 / 152.101       | {"blockedOrPreparationToFirstPhysicalMutationMs":264.1089,"dispatchToBlockedOrPreparationMs":413.7851,"firstNewGenerationToCleanupDrainedMs":166.8453,"firstPhysicalMutationToFirstNewGenerationMs":423.4527,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":262.493,"dispatchToBlockedOrPreparationMs":397.1475,"firstNewGenerationToCleanupDrainedMs":203.3643,"firstPhysicalMutationToFirstNewGenerationMs":460.7159,"presentationToStrictCompletionMs":206.2935}  |
| 15  | 1391.161 / 896.172  | 716.003 / 558.987   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2031,"dispatchToBlockedOrPreparationMs":404.9561,"firstNewGenerationToCleanupDrainedMs":49.0496,"firstPhysicalMutationToFirstNewGenerationMs":257.7943,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1702,"dispatchToBlockedOrPreparationMs":352.6839,"firstNewGenerationToCleanupDrainedMs":0.2574,"firstPhysicalMutationToFirstNewGenerationMs":201.8751,"presentationToStrictCompletionMs":0}            |
| 16  | 781.311 / 702.990   | 605.425 / 611.687   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3446,"dispatchToBlockedOrPreparationMs":357.2443,"firstNewGenerationToCleanupDrainedMs":43.8944,"firstPhysicalMutationToFirstNewGenerationMs":199.9415,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7991,"dispatchToBlockedOrPreparationMs":368.7131,"firstNewGenerationToCleanupDrainedMs":43.5961,"firstPhysicalMutationToFirstNewGenerationMs":194.5788,"presentationToStrictCompletionMs":0}           |
| 17  | 728.992 / 771.931   | 590.849 / 595.296   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2638,"dispatchToBlockedOrPreparationMs":350.6589,"firstNewGenerationToCleanupDrainedMs":42.3749,"firstPhysicalMutationToFirstNewGenerationMs":193.5515,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":6.0713,"dispatchToBlockedOrPreparationMs":350.708,"firstNewGenerationToCleanupDrainedMs":42.6388,"firstPhysicalMutationToFirstNewGenerationMs":195.8778,"presentationToStrictCompletionMs":0}            |
| 18  | 728.398 / 751.162   | 599.703 / 619.652   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0115,"dispatchToBlockedOrPreparationMs":363.5315,"firstNewGenerationToCleanupDrainedMs":41.2919,"firstPhysicalMutationToFirstNewGenerationMs":190.8684,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5809,"dispatchToBlockedOrPreparationMs":376.7105,"firstNewGenerationToCleanupDrainedMs":43.5683,"firstPhysicalMutationToFirstNewGenerationMs":194.792,"presentationToStrictCompletionMs":0}            |
| 19  | 727.459 / 727.324   | 598.952 / 556.316   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":3.809,"dispatchToBlockedOrPreparationMs":366.5857,"firstNewGenerationToCleanupDrainedMs":40.6977,"firstPhysicalMutationToFirstNewGenerationMs":187.8597,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8053,"dispatchToBlockedOrPreparationMs":356.7758,"firstNewGenerationToCleanupDrainedMs":0.3603,"firstPhysicalMutationToFirstNewGenerationMs":194.375,"presentationToStrictCompletionMs":0}             |
| 20  | 572.177 / 901.838   | 747.601 / 1078.028  | 175.424 / 176.190 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0449,"dispatchToBlockedOrPreparationMs":398.0614,"firstNewGenerationToCleanupDrainedMs":222.886,"firstPhysicalMutationToFirstNewGenerationMs":122.6084,"presentationToStrictCompletionMs":175.4241}    | {"blockedOrPreparationToFirstPhysicalMutationMs":267.4793,"dispatchToBlockedOrPreparationMs":463.6941,"firstNewGenerationToCleanupDrainedMs":224.7261,"firstPhysicalMutationToFirstNewGenerationMs":122.1281,"presentationToStrictCompletionMs":176.1895} |
| 21  | 185.652 / 202.830   | 447.482 / 501.448   | 261.830 / 298.618 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.2604,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":261.8296}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.706,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":298.6185}              |
| 22  | 168.778 / 169.901   | 168.778 / 169.901   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":168.7782,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.9006,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 302.104 / 250.712   | 302.104 / 250.712   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":302.1045,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":250.7117,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 909.383 / 955.472   | 1081.859 / 1133.454 | 172.476 / 177.982 | {"blockedOrPreparationToFirstPhysicalMutationMs":270.7427,"dispatchToBlockedOrPreparationMs":450.7856,"firstNewGenerationToCleanupDrainedMs":214.152,"firstPhysicalMutationToFirstNewGenerationMs":146.1786,"presentationToStrictCompletionMs":172.4764}  | {"blockedOrPreparationToFirstPhysicalMutationMs":266.0538,"dispatchToBlockedOrPreparationMs":500.9016,"firstNewGenerationToCleanupDrainedMs":221.3993,"firstPhysicalMutationToFirstNewGenerationMs":145.0989,"presentationToStrictCompletionMs":177.9818} |
| 25  | 1628.967 / 1323.681 | 1760.977 / 1457.896 | 132.010 / 134.215 | {"blockedOrPreparationToFirstPhysicalMutationMs":662.9519,"dispatchToBlockedOrPreparationMs":445.3204,"firstNewGenerationToCleanupDrainedMs":173.9595,"firstPhysicalMutationToFirstNewGenerationMs":478.7455,"presentationToStrictCompletionMs":215.297}  | {"blockedOrPreparationToFirstPhysicalMutationMs":321.4216,"dispatchToBlockedOrPreparationMs":449.9428,"firstNewGenerationToCleanupDrainedMs":178.1383,"firstPhysicalMutationToFirstNewGenerationMs":508.3934,"presentationToStrictCompletionMs":219.9317} |
| 26  | 1252.176 / 763.246  | 1337.152 / 901.361  | 84.975 / 138.115  | {"blockedOrPreparationToFirstPhysicalMutationMs":268.2121,"dispatchToBlockedOrPreparationMs":420.9249,"firstNewGenerationToCleanupDrainedMs":176.3453,"firstPhysicalMutationToFirstNewGenerationMs":471.6695,"presentationToStrictCompletionMs":142.4476} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1998,"dispatchToBlockedOrPreparationMs":367.3261,"firstNewGenerationToCleanupDrainedMs":226.1821,"firstPhysicalMutationToFirstNewGenerationMs":303.6534,"presentationToStrictCompletionMs":138.1153}   |
| 27  | 502.496 / 559.830   | 728.402 / 819.338   | 225.906 / 259.509 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.46,"dispatchToBlockedOrPreparationMs":359.7919,"firstNewGenerationToCleanupDrainedMs":226.292,"firstPhysicalMutationToFirstNewGenerationMs":138.8577,"presentationToStrictCompletionMs":225.9057}      | {"blockedOrPreparationToFirstPhysicalMutationMs":4.7194,"dispatchToBlockedOrPreparationMs":406.2385,"firstNewGenerationToCleanupDrainedMs":260.7611,"firstPhysicalMutationToFirstNewGenerationMs":147.6192,"presentationToStrictCompletionMs":259.5085}   |
| 28  | 920.225 / 849.139   | 874.443 / 938.197   | 0 / 89.058        | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9464,"dispatchToBlockedOrPreparationMs":397.7672,"firstNewGenerationToCleanupDrainedMs":187.5792,"firstPhysicalMutationToFirstNewGenerationMs":286.1505,"presentationToStrictCompletionMs":0}          | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3027,"dispatchToBlockedOrPreparationMs":443.3656,"firstNewGenerationToCleanupDrainedMs":175.5081,"firstPhysicalMutationToFirstNewGenerationMs":315.0207,"presentationToStrictCompletionMs":135.3592}   |
| 29  | 1222.146 / 1762.502 | 1356.637 / 1850.173 | 134.491 / 87.671  | {"blockedOrPreparationToFirstPhysicalMutationMs":24.8193,"dispatchToBlockedOrPreparationMs":682.1023,"firstNewGenerationToCleanupDrainedMs":177.2814,"firstPhysicalMutationToFirstNewGenerationMs":472.434,"presentationToStrictCompletionMs":224.0452}   | {"blockedOrPreparationToFirstPhysicalMutationMs":717.4208,"dispatchToBlockedOrPreparationMs":454.3886,"firstNewGenerationToCleanupDrainedMs":172.8374,"firstPhysicalMutationToFirstNewGenerationMs":505.5261,"presentationToStrictCompletionMs":169.6445} |
| 30  | 530.348 / 464.694   | 705.395 / 696.614   | 175.047 / 231.920 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0201,"dispatchToBlockedOrPreparationMs":373.4736,"firstNewGenerationToCleanupDrainedMs":221.3625,"firstPhysicalMutationToFirstNewGenerationMs":107.5385,"presentationToStrictCompletionMs":175.0465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.7447,"dispatchToBlockedOrPreparationMs":350.0409,"firstNewGenerationToCleanupDrainedMs":232.4266,"firstPhysicalMutationToFirstNewGenerationMs":110.402,"presentationToStrictCompletionMs":231.9199}    |
| 31  | 589.605 / 660.497   | 589.605 / 660.497   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":56.3853,"dispatchToBlockedOrPreparationMs":401.8184,"firstNewGenerationToCleanupDrainedMs":41.187,"firstPhysicalMutationToFirstNewGenerationMs":90.2146,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":61.1083,"dispatchToBlockedOrPreparationMs":461.9829,"firstNewGenerationToCleanupDrainedMs":42.7901,"firstPhysicalMutationToFirstNewGenerationMs":94.6154,"presentationToStrictCompletionMs":0}           |
| 32  | 206.188 / 190.736   | 501.027 / 467.899   | 294.839 / 277.163 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.7737,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":294.8389}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.4716,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":277.1627}             |
| 33  | 287.002 / 253.033   | 287.002 / 253.033   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":287.0022,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":253.0332,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 527.149 / 523.896    | -3.253   | 15 / 16           | 1            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 12 / 14                  | 2            | 690.533 / 772.572    | 82.039   | 17 / 19           | 2            |
| 5   | 13 / 14                  | 1            | 728.784 / 761.827    | 33.043   | 18 / 19           | 1            |
| 6   | 17 / 18                  | 1            | 892.737 / 915.181    | 22.444   | 22 / 23           | 1            |
| 7   | 16 / 17                  | 1            | 894.476 / 917.597    | 23.121   | 21 / 22           | 1            |
| 8   | 17 / 17                  | 0            | 883.022 / 896.100    | 13.078   | 22 / 22           | 0            |
| 9   | 17 / 16                  | -1           | 853.848 / 848.555    | -5.293   | 22 / 21           | -1           |
| 10  | 9 / 9                    | 0            | 575.351 / 558.579    | -16.772  | 14 / 14           | 0            |
| 11  | 3 / 3                    | 0            | 168.396 / 162.582    | -5.814   | 10 / 10           | 0            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 13  | 9 / 9                    | 0            | 545.669 / 594.114    | 48.445   | 11 / 10           | -1           |
| 14  | 22 / 23                  | 1            | 1101.347 / 1120.356  | 19.010   | 27 / 28           | 1            |
| 15  | 12 / 12                  | 0            | 666.953 / 558.729    | -108.224 | 26 / 19           | -7           |
| 16  | 12 / 12                  | 0            | 561.530 / 568.091    | 6.561    | 17 / 16           | -1           |
| 17  | 12 / 12                  | 0            | 548.474 / 552.657    | 4.183    | 16 / 17           | 1            |
| 18  | 12 / 12                  | 0            | 558.411 / 576.083    | 17.672   | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 558.254 / 555.956    | -2.298   | 16 / 16           | 0            |
| 20  | 11 / 16                  | 5            | 524.715 / 853.302    | 328.587  | 16 / 21           | 5            |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 24  | 15 / 15                  | 0            | 867.707 / 912.054    | 44.347   | 20 / 20           | 0            |
| 25  | 29 / 23                  | -6           | 1587.018 / 1279.758  | -307.260 | 34 / 28           | -6           |
| 26  | 23 / 12                  | -11          | 1160.806 / 675.179   | -485.627 | 28 / 17           | -11          |
| 27  | 10 / 10                  | 0            | 502.110 / 558.577    | 56.467   | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 686.864 / 762.689    | 75.825   | 16 / 16           | 0            |
| 29  | 23 / 29                  | 6            | 1179.356 / 1677.335  | 497.980  | 28 / 34           | 6            |
| 30  | 11 / 9                   | -2           | 484.032 / 464.188    | -19.845  | 16 / 15           | -1           |
| 31  | 9 / 9                    | 0            | 548.418 / 617.707    | 69.288   | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 10           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 208.117 / 237.463  | 29.346   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 210.511 / 232.347  | 21.836   |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 387.741 / 400.832  | 13.091   |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 389.986 / 401.917  | 11.931   |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 379.590 / 401.944  | 22.354   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 366.803 / 390.033  | 23.230   |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 269.150 / 307.782  | 38.631   |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 193.216 / 143.903  | -49.314  |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 145.365 / 141.176  | -4.189   |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 142.350 / 144.864  | 2.514    |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 140.585 / 144.350  | 3.765    |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 136.219 / 143.964  | 7.745    |
| 20  | 0 / 1                | 1     | 0 / 5              | 5     | 0 / 334.871        | 334.871  |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 6 / 6              | 0     | 370.264 / 395.523  | 25.259   |
| 26  | 2 / 1                | -1    | 11 / 2             | -9    | 580.710 / 93.461   | -487.249 |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 2 / 3                | 1     | 11 / 16            | 5     | 663.406 / 1041.248 | 377.842  |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## Cumulative gates and other health evidence

### nvidia-2026-09-10T17-12-40-813Z / nvidia / pass 1

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":71783,"leftPath":"NativeOriginal","referenceFrame":72181,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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

### nvidia-2026-09-10T17-12-40-813Z / nvidia / pass 2

Accepted: false. Health evidence: COMPLETE.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":76532,"leftPath":"NativeOriginal","referenceFrame":76940,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

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
| nvidia | 1    | processPrivateMiB | 17067.8359375 / 16650.79296875 / -417.04296875 | 16521.3203125 / 16407.21484375 / -114.10546875 | 302.938               |
| nvidia | 1    | systemCommitMiB   | 54108.640625 / 53702.98046875 / -405.66015625  | 54414.625 / 54313.40234375 / -101.22265625     | 304.438               |
| nvidia | 1    | dxgiUsageMiB      | 4420.1171875 / 3550.13671875 / -869.98046875   | 4201.6171875 / 3527.0078125 / -674.609375      | 195.371               |
| nvidia | 1    | liveTextures      | 0 / 213 / 213                                  | 0 / 256 / 256                                  | 43                    |
| nvidia | 1    | liveTextureMiB    | 0 / 2270.9314918518066 / 2270.9314918518066    | 0 / 2424.0259971618652 / 2424.0259971618652    | 153.095               |
| nvidia | 2    | processPrivateMiB | 17079.37890625 / 16651.89453125 / -427.484375  | 16831.234375 / 16617.78515625 / -213.44921875  | 214.035               |
| nvidia | 2    | systemCommitMiB   | 54286.1328125 / 53807.703125 / -478.4296875    | 54507.67578125 / 54547.8203125 / 40.14453125   | 518.574               |
| nvidia | 2    | dxgiUsageMiB      | 3922.69921875 / 3499.73828125 / -422.9609375   | 3889.8828125 / 3566.79296875 / -323.08984375   | 99.871                |
| nvidia | 2    | liveTextures      | 0 / 207 / 207                                  | 0 / 233 / 233                                  | 26                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2265.5977058410645 / 2265.5977058410645    | 0 / 2349.4316596984863 / 2349.4316596984863    | 83.834                |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta     |
| ------------------------------------------------------- | ----------- | ----------- | --------- |
| cpu/active                                              | false       | false       | n/a       |
| cpu/compactPresentationContract/publishes               | 2233        | 2222        | -11       |
| cpu/compactPresentationContract/reuses                  | 2211        | 2200        | -11       |
| cpu/devBenchOnly                                        | true        | true        | n/a       |
| cpu/generationResourceValidation/contractInvalidations  | 71          | 70          | -1        |
| cpu/generationResourceValidation/contractPublishes      | 153         | 151         | -2        |
| cpu/generationResourceValidation/fullValidations        | 576         | 568         | -8        |
| cpu/generationResourceValidation/stableChecks           | 8444        | 8397        | -47       |
| cpu/generationResourceValidation/stableHits             | 8367        | 8316        | -51       |
| cpu/generationResourceValidation/stableMisses           | 77          | 81          | 4         |
| cpu/schemaVersion                                       | 1           | 1           | 0         |
| cpu/sessionId                                           | 1           | 1           | 0         |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0         |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4308        | 4328        | 20        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0         |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4308        | 4328        | 20        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4266        | 4286        | 20        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0         |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4308        | 4328        | 20        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0         |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4287        | 4307        | 20        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0         |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 35          | 34          | -1        |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4273        | 4294        | 21        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4215        | 4238        | 23        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 90          | -4        |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0         |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4293        | 4313        | 20        |
| cpu/strongStereoPacket/captures                         | 4636        | 4628        | -8        |
| cpu/strongStereoPacket/commitAccepts                    | 4446        | 4424        | -22       |
| cpu/strongStereoPacket/commitRejects                    | 66          | 66          | 0         |
| cpu/strongStereoPacket/commitValidations                | 4512        | 4490        | -22       |
| cpu/strongStereoPacket/cycleReuses                      | 2277        | 2275        | -2        |
| cpu/strongStereoPacket/fastSkips                        | 3980        | 4028        | 48        |
| cpu/strongStereoPacket/invalidations                    | 4854        | 4882        | 28        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 100         | -3        |
| cpu/strongStereoPacket/lifetimeReuses                   | 2256        | 2253        | -3        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.866       | 1.855       | -0.012    |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 15.900      | 68.100      | 52.200    |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.138       | 0.147       | 0.009     |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.300       | 11.900      | 10.600    |
| cpu/window/currentFrame                                 | 72183       | 18480       | -53703    |
| cpu/window/elapsedFrames                                | 4309        | 4328        | 19        |
| cpu/window/initialized                                  | true        | true        | n/a       |
| cpu/window/startFrame                                   | 67874       | 14152       | -53722    |
| gpu/active                                              | false       | false       | n/a       |
| gpu/currentFrame                                        | 72184       | 18482       | -53702    |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0         |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6185322000  | 6230998224  | 45676224  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4875        | 4911        | 36        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12383280000 | 12474725760 | 91445760  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0         |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.306       | 0.000     |
| gpu/item5ActiveFSRCopies/activePixels                   | 12031551120 | 12047940400 | 16389280  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27264724080 | 27299138000 | 34413920  |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15470       | 15490       | 20        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0         |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0         |
| gpu/item7EarlyHAM/directOutputSkips                     | 2043        | 2045        | 2         |
| gpu/item7EarlyHAM/executedClears                        | 2044        | 2052        | 8         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2044        | 2052        | 8         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0         |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4246        | 4256        | 10        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0         |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0         |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0         |
| gpu/observedFrames                                      | 4310        | 4330        | 20        |
| gpu/startFrame                                          | 67874       | 14152       | -53722    |
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
| texture/createdCount                                    | 3922        | 3974        | 52        |
| texture/createdEstimatedBytes                           | 38661165120 | 38786366328 | 125201208 |
| texture/currentCohort                                   | 0           | 0           | 0         |
| texture/destroyedCount                                  | 3709        | 3718        | 9         |
| texture/destroyedEstimatedBytes                         | 36279920860 | 36244590844 | -35330016 |
| texture/droppedTextureRecords                           | 0           | 0           | 0         |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0         |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0         |
| texture/groupCount                                      | 873         | 896         | 23        |
| texture/liveTextureRecordCount                          | 213         | 256         | 43        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0         |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0         |
| texture/niSourceTextureMatchedCount                     | 2           | 47          | 45        |
| texture/niSourceTextureMatchedEstimatedBytes            | 5592480     | 166123904   | 160531424 |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0         |
| texture/niSourceTextureResourceCount                    | 1503        | 1473        | -30       |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a       |
| texture/outstandingCount                                | 213         | 256         | 43        |
| texture/outstandingEstimatedBytes                       | 2381244260  | 2541775484  | 160531224 |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0         |
| texture/recordingFailures                               | 0           | 0           | 0         |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0         |
| texture/sessionID                                       | 1           | 1           | 0         |
| texture/supported                                       | true        | true        | n/a       |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta       |
| ------------------------------------------------------- | ----------- | ----------- | ----------- |
| cpu/active                                              | false       | false       | n/a         |
| cpu/compactPresentationContract/publishes               | 2254        | 2189        | -65         |
| cpu/compactPresentationContract/reuses                  | 2232        | 2167        | -65         |
| cpu/devBenchOnly                                        | true        | true        | n/a         |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0           |
| cpu/generationResourceValidation/contractPublishes      | 155         | 156         | 1           |
| cpu/generationResourceValidation/fullValidations        | 584         | 575         | -9          |
| cpu/generationResourceValidation/stableChecks           | 8541        | 8303        | -238        |
| cpu/generationResourceValidation/stableHits             | 8464        | 8226        | -238        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0           |
| cpu/schemaVersion                                       | 1           | 1           | 0           |
| cpu/sessionId                                           | 2           | 2           | 0           |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0           |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4357        | 4221        | -136        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0           |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4357        | 4221        | -136        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4315        | 4179        | -136        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0           |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4357        | 4221        | -136        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0           |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4336        | 4200        | -136        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0           |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0           |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4323        | 4187        | -136        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4263        | 4130        | -133        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 90          | -4          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0           |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4342        | 4206        | -136        |
| cpu/strongStereoPacket/captures                         | 4690        | 4548        | -142        |
| cpu/strongStereoPacket/commitAccepts                    | 4490        | 4359        | -131        |
| cpu/strongStereoPacket/commitRejects                    | 64          | 65          | 1           |
| cpu/strongStereoPacket/commitValidations                | 4554        | 4424        | -130        |
| cpu/strongStereoPacket/cycleReuses                      | 2305        | 2233        | -72         |
| cpu/strongStereoPacket/fastSkips                        | 4024        | 3894        | -130        |
| cpu/strongStereoPacket/invalidations                    | 4915        | 4767        | -148        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 103         | 103         | 0           |
| cpu/strongStereoPacket/lifetimeReuses                   | 2282        | 2212        | -70         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.856       | 1.879       | 0.023       |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 67.200      | 25.400      | -41.800     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.141       | 0.143       | 0.002       |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1           | 1.600       | 0.600       |
| cpu/window/currentFrame                                 | 76941       | 23112       | -53829      |
| cpu/window/elapsedFrames                                | 4357        | 4220        | -137        |
| cpu/window/initialized                                  | true        | true        | n/a         |
| cpu/window/startFrame                                   | 72584       | 18892       | -53692      |
| gpu/active                                              | false       | false       | n/a         |
| gpu/currentFrame                                        | 76941       | 23113       | -53828      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0           |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 6274136880  | 6071131440  | -203005440  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4945        | 4785        | -160        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 12561091200 | 12154665600 | -406425600  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0           |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.306       | 0.309       | 0.003       |
| gpu/item5ActiveFSRCopies/activePixels                   | 12289195480 | 11865705520 | -423489960  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 27819930920 | 26516112080 | -1303818840 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 15790       | 15110       | -680        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0           |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0           |
| gpu/item7EarlyHAM/directOutputSkips                     | 2089        | 1991        | -98         |
| gpu/item7EarlyHAM/executedClears                        | 2052        | 2016        | -36         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 2052        | 2016        | -36         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0           |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4302        | 4168        | -134        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0           |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0           |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0           |
| gpu/observedFrames                                      | 4357        | 4221        | -136        |
| gpu/startFrame                                          | 72584       | 18892       | -53692      |
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
| texture/createdCount                                    | 3898        | 3968        | 70          |
| texture/createdEstimatedBytes                           | 38610535792 | 38826844112 | 216308320   |
| texture/currentCohort                                   | 0           | 0           | 0           |
| texture/destroyedCount                                  | 3691        | 3735        | 44          |
| texture/destroyedEstimatedBytes                         | 36234884412 | 36363286460 | 128402048   |
| texture/droppedTextureRecords                           | 0           | 0           | 0           |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0           |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0           |
| texture/groupCount                                      | 861         | 887         | 26          |
| texture/liveTextureRecordCount                          | 207         | 233         | 26          |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0           |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0           |
| texture/niSourceTextureMatchedCount                     | 0           | 26          | 26          |
| texture/niSourceTextureMatchedEstimatedBytes            | 0           | 87906272    | 87906272    |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0           |
| texture/niSourceTextureResourceCount                    | 1487        | 1506        | 19          |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a         |
| texture/outstandingCount                                | 207         | 233         | 26          |
| texture/outstandingEstimatedBytes                       | 2375651380  | 2463557652  | 87906272    |
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
