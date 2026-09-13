# NVIDIA measurement for PR82: main-VR versus PR73

## NVIDIA health and comparison with PR73

The baseline is PR73's last documented measurement, **554e484e3**
(Build **e7e9fa2d13f2**). The current measurement is clean **main-VR
e4cfd8f2f** (Build **2f6432ddbcbd**), packaged as the PR82 test AIO.
It is not PR82 implementation head **76f9418ab**. These results
describe the measured integration build; they do not establish live
coverage of PR82's accepted-draw or OCU gaze APIs.

Both runs used two ordered 33-transition passes in one game process,
Dragonsreach, five-second server pacing, runtime-only API settings and
foveation 0.3/0.3/0.7. Only these two runs are compared here.

-   **Execution and health:** 66/66 completed and 66 terminal PASS.
    Task 2 counts are 66 PASS / 0 FAIL / 0 INCONCLUSIVE, without an aggregate
    verdict. Device loss, OOM, producer-terminal failure, vendor-native
    qualification failure and credible liveness timeout are each zero.
    No counted lifecycle, fidelity, retirement or fallback failure; no
    recovery reset. Captures are inactive and evidence is fully flushed.
-   **Switch latency:** strict means rise from 815.09 / 768.53 ms to
    917.81 / 897.03 ms: **+12.60% / +16.72%**. These are descriptive
    slowdowns, not an isolated estimate of PR82's changes.
-   **Stretch:** all 33 current selected transitions recovered, versus 32
    in PR73. Full-pass totals are 64 / 62 frames and 4716.29 / 4713.13 ms,
    versus 62 / 62 frames and 4080.67 / 4085.36 ms. Current pass 1 row 1
    adds a recovered 2-frame, 175.74 ms episode. No active tail remains.
    The fixed two-frame cutoff is diagnostic during imposed settling.
    The scaled-only presentation gate is a contract mismatch for proven
    native terminal presentation. Both applicable health standards are MET;
    raw false cumulative acceptance remains preserved.
-   **Retries:** 15 per pass in both builds: four DLSS viewport retries and
    eleven relatch requeues. Retry-to-stable includes later settling;
    viewport and drain intervals overlap other phases.
-   **Slowest changes:** pass 1 rows 6, 17 and 20 add 336.46, 245.79 and
    227.85 ms. Pass 2 rows 20, 24 and 18 add 299.53, 248.43 and
    248.21 ms. The detailed comparison preserves every route and pass.
-   **Limits:** formal improvement-or-neutral assessment is INCONCLUSIVE.
    Both report the RTX 5070 Ti Laptop GPU, but adapter LUIDs differ;
    driver versions are unavailable. Scene/time/weather and dependency
    records differ. Toolchain, shader compiler and foveation match.
    Fixture fingerprints and a versioned tolerance policy are unavailable.
    Memory classification is inconclusive, with Normal pressure at all six
    retained boundaries. No resolved profiler samples are available, so
    GPU-cost and FPS claims cannot be made.

### Summary: PR73 baseline and current measurement

Every paired value is **Pass 1 / Pass 2**. Mean switch is qualification
dispatch to strict completion, excluding the preceding five-second wait.
Stretch time spans the complete owned pass capture.

| Metric                                         | PR73 baseline 554e484e3 |      Current e4cfd8f2f |
| ---------------------------------------------- | ----------------------: | ---------------------: |
| Mean switch, pass 1                            |               815.09 ms |              917.81 ms |
| Mean switch, pass 2                            |               768.53 ms |              897.03 ms |
| Mean switch change, pass 1 / 2                 |               Reference |         12.60 / 16.72% |
| Stretch frames, pass 1 / 2                     |                 62 / 62 |                64 / 62 |
| Stretch time, pass 1 / 2                       |           4.08 / 4.09 s |          4.72 / 4.71 s |
| Retries, pass 1 / 2                            |                 15 / 15 |                15 / 15 |
| Mean retry→stable, pass 1 / 2                  |      336.16 / 337.90 ms |     381.67 / 398.63 ms |
| Maximum retry→stable, pass 1 / 2               |      463.47 / 461.56 ms |     547.22 / 538.98 ms |
| Mean cleanup tail, pass 1 / 2                  |        90.97 / 91.97 ms |     110.93 / 118.75 ms |
| Median switch, pass 1 / 2                      |      798.93 / 817.13 ms |    989.51 / 1002.91 ms |
| p95 switch, pass 1 / 2                         |    1387.63 / 1194.95 ms |   1507.42 / 1369.39 ms |
| Maximum switch, pass 1 / 2                     |    1423.85 / 1205.89 ms |   1537.48 / 1379.02 ms |
| Total switch time, pass 1 / 2                  |  26898.11 / 25361.63 ms | 30287.71 / 29602.01 ms |
| Mean strict frames, pass 1 / 2                 |           15.18 / 14.82 |          14.85 / 14.58 |
| Mean relatch, pass 1 / 2                       |      736.86 / 698.56 ms |     845.35 / 813.46 ms |
| Mean relatch frames, pass 1 / 2                |           13.20 / 13.20 |          13.20 / 13.04 |
| Relatch samples, each pass                     |                   25/33 |                  25/33 |
| Stretch episodes, pass 1 / 2                   |                 18 / 18 |                19 / 18 |
| Terminal PASS, each pass                       |                   33/33 |                  33/33 |
| Task 2 PASS / FAIL / INCONCLUSIVE, both passes |              66 / 0 / 0 |             66 / 0 / 0 |
| Device loss                                    |                       0 |                      0 |
| OOM                                            |                       0 |                      0 |
| Producer terminal failure                      |                       0 |                      0 |
| Vendor-native qualification failure            |                       0 |                      0 |
| Credible liveness timeout                      |                       0 |                      0 |
| Applicable health standard, both passes        |                     MET |                    MET |
| Active stretch at stop, both passes            |                    None |                   None |
| Memory classification                          |            inconclusive |           inconclusive |
| Resolved profiler samples                      |             Unavailable |            Unavailable |

### One transition table: baseline and current, both passes

**B** = PR73 554e484e3; **C** = current e4cfd8f2f.
Within each B/C line values are **Pass 1 / Pass 2**; times are milliseconds.
All rows have terminal PASS and Task 2 PASS in both passes of both runs.
Strict and relatch start at qualification dispatch; relatch ends at first
exact new-generation proof. Missing/inapplicable proof is **—**, not zero.
Frames appear as (nf). Stretch totals here cover each row window.

**V** = DLSS viewport recycle; **R** = render-target relatch requeue.
Retry→stable preserves each event. Viewport wait is the longest concurrent
role wait; drain wait is owned Pending→Ready. Do not add overlapping
intervals. HP=HoshiPa, UQ=Ultra Quality, Q=Quality, B=Balanced,
P=Performance, UP=Ultra Performance, AA=Native AA.

|   # | Transition        | Strict ms (frames)                                                   | Relatch ms (frames)                                                 | Cleanup tail ms                          | Stretch frames (ms)                                          | Retries                  | Retry→stable ms                          | Viewport wait ms                     | Drain wait ms                        |
| --: | ----------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------- | ---------------------------------------- | ------------------------------------------------------------ | ------------------------ | ---------------------------------------- | ------------------------------------ | ------------------------------------ |
|   1 | DLSS HP → NONE    | B: 776.41 (14f) / 750.96 (15f)<br>C: 865.32 (14f) / 846.76 (14f)     | B: 527.36 (9f) / 490.71 (10f)<br>C: 591.62 (9f) / 569.55 (9f)       | B: 183.09 / 194.75<br>C: 215.42 / 217.30 | B: 0f (0.00) / 0f (0.00)<br>C: 2f (175.74) / 0f (0.00)       | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|   2 | NONE → TAA        | B: 179.99 (3f) / 172.86 (5f)<br>C: 205.39 (4f) / 211.37 (4f)         | B: — / —<br>C: — / —                                                | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|   3 | TAA → DLAA        | B: 309.03 (3f) / 286.28 (3f)<br>C: 287.68 (3f) / 318.16 (3f)         | B: — / —<br>C: — / —                                                | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|   4 | DLAA → DLSS HP    | B: 1213.64 (19f) / 1084.86 (19f)<br>C: 1195.60 (19f) / 1175.16 (18f) | B: 877.78 (14f) / 812.72 (14f)<br>C: 895.88 (14f) / 867.80 (13f)    | B: 121.85 / 92.16<br>C: 102.69 / 105.51  | B: 2f (254.10) / 2f (214.50)<br>C: 2f (233.77) / 2f (226.70) | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|   5 | DLSS HP → DLSS UQ | B: 1380.57 (19f) / 1180.40 (18f)<br>C: 1412.84 (18f) / 1208.67 (20f) | B: 1081.83 (14f) / 861.37 (13f)<br>C: 1113.21 (13f) / 906.46 (15f)  | B: 148.13 / 104.05<br>C: 155.94 / 158.06 | B: 2f (217.52) / 2f (245.00)<br>C: 2f (247.74) / 2f (241.78) | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|   6 | DLSS UQ → DLSS Q  | B: 1177.83 (22f) / 1195.03 (22f)<br>C: 1514.29 (22f) / 1379.02 (21f) | B: 891.56 (17f) / 917.43 (17f)<br>C: 1211.66 (17f) / 1068.69 (16f)  | B: 145.22 / 90.54<br>C: 107.66 / 160.53  | B: 5f (337.27) / 5f (345.61)<br>C: 5f (384.77) / 5f (406.16) | B: 1V / 1V<br>C: 1V / 1V | B: 387.61 / 403.98<br>C: 449.90 / 469.08 | B: 49.35 / 57.22<br>C: 64.36 / 62.15 | B: — / —<br>C: — / —                 |
|   7 | DLSS Q → DLSS B   | B: 1423.85 (21f) / 1205.89 (23f)<br>C: 1537.48 (23f) / 1376.85 (22f) | B: 1136.91 (16f) / 936.38 (18f)<br>C: 1233.96 (18f) / 1059.63 (17f) | B: 96.13 / 138.52<br>C: 162.16 / 106.50  | B: 5f (345.48) / 5f (354.16)<br>C: 5f (380.56) / 5f (396.32) | B: 1V / 1V<br>C: 1V / 1V | B: 398.37 / 407.17<br>C: 440.57 / 456.14 | B: 52.49 / 52.66<br>C: 59.22 / 59.18 | B: — / —<br>C: — / —                 |
|   8 | DLSS B → DLSS P   | B: 1398.22 (22f) / 1192.83 (23f)<br>C: 1502.83 (21f) / 1364.42 (21f) | B: 1114.00 (17f) / 928.00 (18f)<br>C: 1194.28 (16f) / 1037.31 (16f) | B: 144.06 / 135.58<br>C: 166.65 / 176.00 | B: 5f (337.34) / 5f (351.01)<br>C: 5f (376.17) / 5f (422.74) | B: 1V / 1V<br>C: 1V / 1V | B: 394.05 / 406.08<br>C: 434.33 / 483.23 | B: 52.38 / 51.17<br>C: 54.73 / 0.95  | B: — / —<br>C: — / —                 |
|   9 | DLSS P → DLSS UP  | B: 1367.28 (22f) / 1194.89 (22f)<br>C: 1416.12 (21f) / 1292.35 (22f) | B: 1063.34 (17f) / 915.13 (17f)<br>C: 1118.30 (16f) / 982.30 (17f)  | B: 142.87 / 152.06<br>C: 158.48 / 157.78 | B: 5f (343.31) / 5f (332.71)<br>C: 5f (373.36) / 5f (373.62) | B: 1V / 1V<br>C: 1V / 1V | B: 395.82 / 385.44<br>C: 429.19 / 433.84 | B: 48.85 / 1.41<br>C: 52.85 / 56.75  | B: — / —<br>C: — / —                 |
|  10 | DLSS UP → DLAA    | B: 798.93 (15f) / 805.41 (14f)<br>C: 950.43 (16f) / 876.40 (14f)     | B: 616.54 (11f) / 625.06 (10f)<br>C: 716.33 (12f) / 667.64 (10f)    | B: 134.68 / 133.39<br>C: 178.00 / 155.83 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  11 | DLAA → TAA        | B: 465.23 (9f) / 484.80 (10f)<br>C: 530.93 (10f) / 589.96 (10f)      | B: 176.06 (3f) / 196.96 (3f)<br>C: 214.95 (4f) / 223.84 (3f)        | B: 288.46 / 287.14<br>C: 315.70 / 364.83 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  12 | TAA → NONE        | B: 175.99 (4f) / 160.25 (4f)<br>C: 207.16 (4f) / 214.57 (4f)         | B: — / —<br>C: — / —                                                | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  13 | NONE → FSR3 AA    | B: 921.08 (11f) / 632.60 (11f)<br>C: 872.52 (11f) / 748.31 (11f)     | B: 876.51 (10f) / 588.24 (10f)<br>C: 820.19 (10f) / 688.44 (10f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  14 | FSR3 AA → FSR3 HP | B: 1155.43 (24f) / 1079.45 (23f)<br>C: 1318.28 (23f) / 1268.29 (22f) | B: 912.14 (19f) / 863.54 (18f)<br>C: 1039.20 (18f) / 1004.13 (17f)  | B: 106.07 / 127.66<br>C: 109.21 / 106.41 | B: 5f (219.26) / 5f (221.96)<br>C: 5f (265.71) / 5f (274.78) | B: 1R / 1R<br>C: 1R / 1R | B: 463.47 / 461.56<br>C: 547.22 / 538.98 | B: — / —<br>C: — / —                 | B: 49.09 / 46.40<br>C: 61.45 / 56.02 |
|  15 | FSR3 HP → FSR3 UQ | B: 1002.78 (21f) / 817.13 (17f)<br>C: 989.51 (18f) / 1002.91 (18f)   | B: 683.71 (14f) / 687.50 (14f)<br>C: 779.68 (14f) / 783.54 (14f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 4f (207.81) / 4f (223.74)<br>C: 4f (237.31) / 4f (246.90) | B: 1R / 1R<br>C: 1R / 1R | B: 255.78 / 273.15<br>C: 292.94 / 302.26 | B: — / —<br>C: — / —                 | B: 47.15 / 48.83<br>C: 55.38 / 54.39 |
|  16 | FSR3 UQ → FSR3 Q  | B: 1007.99 (21f) / 819.19 (18f)<br>C: 1044.91 (19f) / 1043.78 (19f)  | B: 704.90 (14f) / 641.10 (14f)<br>C: 781.83 (14f) / 768.33 (14f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 4f (239.81) / 4f (205.17)<br>C: 4f (234.46) / 4f (238.88) | B: 1R / 1R<br>C: 1R / 1R | B: 287.58 / 249.41<br>C: 290.39 / 293.09 | B: — / —<br>C: — / —                 | B: 47.62 / 43.52<br>C: 55.55 / 53.67 |
|  17 | FSR3 Q → FSR3 B   | B: 769.46 (18f) / 821.42 (18f)<br>C: 1015.25 (18f) / 1016.98 (18f)   | B: 605.15 (14f) / 651.71 (14f)<br>C: 800.40 (14f) / 784.48 (14f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 4f (189.40) / 4f (204.05)<br>C: 4f (235.61) / 4f (239.86) | B: 1R / 1R<br>C: 1R / 1R | B: 232.22 / 249.59<br>C: 298.73 / 295.77 | B: — / —<br>C: — / —                 | B: 42.36 / 44.91<br>C: 62.49 / 55.79 |
|  18 | FSR3 B → FSR3 P   | B: 786.13 (17f) / 817.50 (18f)<br>C: 994.25 (18f) / 1065.72 (18f)    | B: 620.03 (13f) / 634.08 (14f)<br>C: 772.66 (14f) / 850.20 (14f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 4f (194.84) / 4f (198.06)<br>C: 4f (233.68) / 4f (250.78) | B: 1R / 1R<br>C: 1R / 1R | B: 239.04 / 244.28<br>C: 290.20 / 314.59 | B: — / —<br>C: — / —                 | B: 44.06 / 46.12<br>C: 56.32 / 63.40 |
|  19 | FSR3 P → FSR3 UP  | B: 1223.30 (27f) / 939.61 (21f)<br>C: 1116.06 (20f) / 950.19 (17f)   | B: 641.68 (14f) / 647.57 (14f)<br>C: 792.70 (14f) / 792.96 (14f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 4f (196.38) / 4f (198.30)<br>C: 4f (234.22) / 4f (233.25) | B: 1R / 1R<br>C: 1R / 1R | B: 243.98 / 243.62<br>C: 292.58 / 297.83 | B: — / —<br>C: — / —                 | B: 47.27 / 44.69<br>C: 57.88 / 64.22 |
|  20 | FSR3 UP → FSR3 AA | B: 953.04 (22f) / 1009.72 (21f)<br>C: 1180.89 (22f) / 1309.25 (22f)  | B: 723.86 (16f) / 781.85 (16f)<br>C: 899.81 (16f) / 985.27 (16f)    | B: 184.45 / 177.50<br>C: 221.94 / 256.82 | B: 5f (253.68) / 5f (283.34)<br>C: 5f (308.93) / 5f (362.01) | B: 1R / 1R<br>C: 1R / 1R | B: 338.90 / 377.56<br>C: 415.93 / 480.87 | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  21 | FSR3 AA → TAA     | B: 467.71 (10f) / 453.07 (11f)<br>C: 565.78 (10f) / 569.58 (9f)      | B: — / —<br>C: — / —                                                | B: 258.57 / 263.99<br>C: 327.53 / 338.96 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  22 | TAA → NONE        | B: 158.62 (3f) / 162.81 (4f)<br>C: 206.01 (4f) / 212.87 (4f)         | B: — / —<br>C: — / —                                                | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  23 | NONE → DLAA       | B: 240.63 (3f) / 288.59 (4f)<br>C: 291.84 (3f) / 296.74 (3f)         | B: — / —<br>C: — / —                                                | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  24 | DLAA → FSR3 AA    | B: 725.34 (16f) / 796.74 (15f)<br>C: 921.32 (15f) / 1045.17 (15f)    | B: 557.47 (12f) / 610.81 (11f)<br>C: 709.97 (11f) / 803.24 (11f)    | B: 127.78 / 134.14<br>C: 160.49 / 184.36 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 1R / 1R<br>C: 1R / 1R | B: 180.68 / 199.09<br>C: 226.56 / 266.86 | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  25 | FSR3 AA → DLSS HP | B: 1127.03 (20f) / 1092.22 (20f)<br>C: 1219.53 (19f) / 1281.48 (20f) | B: 873.53 (15f) / 811.37 (15f)<br>C: 913.83 (14f) / 970.93 (15f)    | B: 127.19 / 144.82<br>C: 163.09 / 166.42 | B: 2f (213.48) / 2f (203.31)<br>C: 2f (232.64) / 2f (230.93) | B: 1R / 1R<br>C: 1R / 1R | B: 428.46 / 402.91<br>C: 445.12 / 481.77 | B: — / —<br>C: — / —                 | B: 48.17 / 44.50<br>C: 54.04 / 55.05 |
|  26 | DLSS HP → FSR3 HP | B: 1009.98 (20f) / 1068.07 (19f)<br>C: 1177.03 (19f) / 1181.03 (20f) | B: 795.04 (15f) / 795.15 (14f)<br>C: 914.98 (14f) / 910.50 (15f)    | B: 46.81 / 0.00<br>C: 53.06 / 161.18     | B: 3f (206.47) / 3f (220.36)<br>C: 3f (246.16) / 3f (246.74) | B: 1R / 1R<br>C: 1R / 1R | B: 373.86 / 387.09<br>C: 447.86 / 432.23 | B: — / —<br>C: — / —                 | B: 46.01 / 46.65<br>C: 63.62 / 55.64 |
|  27 | FSR3 HP → NONE    | B: 727.18 (16f) / 748.59 (16f)<br>C: 856.73 (16f) / 930.09 (15f)     | B: 493.67 (10f) / 512.41 (10f)<br>C: 575.90 (10f) / 615.87 (10f)    | B: 180.19 / 181.19<br>C: 221.04 / 228.50 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  28 | NONE → FSR3 UP    | B: 884.30 (16f) / 974.43 (17f)<br>C: 1101.30 (17f) / 1110.85 (17f)   | B: 622.81 (10f) / 744.00 (12f)<br>C: 834.70 (12f) / 837.87 (12f)    | B: 0.00 / 49.00<br>C: 113.58 / 115.24    | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  29 | FSR3 UP → DLSS UP | B: 1053.92 (20f) / 1039.38 (20f)<br>C: 1240.95 (20f) / 1252.20 (20f) | B: 792.25 (15f) / 779.81 (15f)<br>C: 941.07 (15f) / 905.69 (15f)    | B: 136.78 / 136.73<br>C: 160.40 / 196.27 | B: 3f (324.55) / 3f (284.08)<br>C: 3f (315.45) / 3f (321.68) | B: 1R / 1R<br>C: 1R / 1R | B: 422.59 / 377.54<br>C: 423.60 / 432.95 | B: — / —<br>C: — / —                 | B: 51.24 / 47.16<br>C: 52.82 / 54.96 |
|  30 | DLSS UP → TAA     | B: 701.80 (17f) / 720.99 (15f)<br>C: 892.78 (16f) / 857.70 (15f)     | B: 470.24 (11f) / 471.99 (9f)<br>C: 603.79 (11f) / 569.69 (9f)      | B: 175.57 / 195.31<br>C: 231.39 / 227.33 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  31 | TAA → FSR3 AA     | B: 606.60 (12f) / 601.57 (11f)<br>C: 725.62 (11f) / 734.07 (11f)     | B: 563.10 (10f) / 559.01 (10f)<br>C: 662.76 (10f) / 682.08 (10f)    | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  32 | FSR3 AA → NONE    | B: 443.47 (11f) / 482.12 (9f)<br>C: 588.11 (11f) / 563.95 (11f)      | B: — / —<br>C: — / —                                                | B: 254.25 / 296.33<br>C: 336.33 / 334.90 | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |
|  33 | NONE → DLAA       | B: 265.33 (3f) / 281.98 (3f)<br>C: 342.96 (5f) / 307.17 (3f)         | B: — / —<br>C: — / —                                                | B: 0.00 / 0.00<br>C: 0.00 / 0.00         | B: 0f (0.00) / 0f (0.00)<br>C: 0f (0.00) / 0f (0.00)         | B: 0 / 0<br>C: 0 / 0     | B: — / —<br>C: — / —                     | B: — / —<br>C: — / —                 | B: — / —<br>C: — / —                 |

## Provenance and evidence

-   Current run: renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z.
-   Baseline run: renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z.
-   Current compiled source, renderer base and main-VR base:
    e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41.
-   Baseline compiled source and renderer base:
    554e484e3957178e2d144bf35266bfdcc0948642; main-VR base
    ef7c366dd73989b2b87751c0ef975db7c6fd310f.
-   Current Build ID:
    2f6432ddbcbd05826c1fb1ccd66f25e14755d718a5e760a58cbae58b7f625efd.
-   Baseline Build ID:
    e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96.
-   Current physical DLL: 28,784,128 bytes, SHA-256
    da3a9b54ee34dee836e3a2843d33acb8d33e49ab5754be06c05f37e038fbcdb0.
    Exactly one enabled loose provider; no DLL in Overwrite or unmanaged
    Data. Adjacent manifest and AIO receipt agree with the runtime producer.
-   [Ledger 0004](vr-render-scale-ledger-0004-pr82.csv) has exactly two run
    columns. Baseline cells were copied unchanged from snapshot 0003.
-   [Current finalized run](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-run.md) includes both-pass memory
    boundaries, predicates and all result categories.
-   [Full comparison](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-comparison.md) retains every transition/pass
    delta, health gate, context and resource comparison.
-   Raw journal, every revision's scalar export (1,503,939,713 bytes),
    receipt index and command receipts remain in
    artifacts/renderscale-tuning/renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z/.
-   Empty profiler recurrence was attached to local feedback
    AUTO-20260911-154621563-951D86E2 on September 13.

## Validation

Runtime: 66/66 transitions, 66 terminal PASS and 66 Task 2 PASS;
full-history health NO_COUNTED_FAILURES; reporting COMPLETE.
Owned captures are VERIFIED_INACTIVE, evidencePending=0, dispatch pacing
VALID (maximum client gap 39.478 ms against 250 ms). Main-VR source
identity is verified; PR82-head runtime, SE/AE and visual release
qualification were not exercised by this assay.

Offline: packaged finalizer completed successfully. The repository
compare-render-scale-ledger.py wrapper compared only the selected PR73
baseline and current measurement. All 1,056 numeric timing cells passed,
including 528 current values. Both complete summaries and the complete
comparison reconstruct exactly from the ledger; all 1,228 retained
baseline metric cells are unchanged. See the
[coverage receipt](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-validation.json).

Exact commands are retained in finalization-command-result.json and
comparison-command-result.json. Comparison generation and audit took
4.433 seconds; complete ledger preparation and audit
took 8.017 seconds. No measurement was replayed.
