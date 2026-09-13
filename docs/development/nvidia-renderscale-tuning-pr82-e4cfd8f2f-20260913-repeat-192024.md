# Second NVIDIA measurement for PR82

## NVIDIA health and comparison with PR73

The tables retain **all three runs**: PR73's last documented measurement,
**554e484e3** (Build **e7e9fa2d13f2**), and both September 13 runs at
**18:46 UTC (Run 1)** and **19:20 UTC (Run 2)**. Both new runs used clean
**main-VR e4cfd8f2f** (Build **2f6432ddbcbd**), packaged as the PR82 test
AIO. It does not contain PR82 implementation head **76f9418ab** and does
not establish live coverage of its accepted-draw or OCU gaze APIs.

Each run used two ordered 33-transition passes in one game process,
Dragonsreach, five-second server pacing, runtime-only settings and
foveation 0.3/0.3/0.7. The two September 13 runs also share game PID 38084
and the exact producer Build ID; no build, deployment or restart occurred
between them.

-   **Execution and health:** each new run completed 66/66 transitions
    with 66 terminal PASS. Each has Task 2 counts of 66 PASS / 0 FAIL /
    0 INCONCLUSIVE, without an aggregate verdict. Device loss, OOM, producer-terminal failure, vendor-native
    qualification failure and credible liveness timeout are each zero.
    No counted lifecycle, fidelity, retirement or fallback failure; no
    recovery reset. Captures are inactive and the evidence journal is flushed.
-   **Switch latency versus PR73:** mean durations are **0.815 / 0.769 s**
    for PR73, **0.918 / 0.897 s** for Run 1 (**+12.60% / +16.72%**) and
    **0.941 / 0.938 s** for Run 2 (**+15.44% / +22.06%**). Each new run has
    28/33 slower transitions in pass 1 and 33/33 in pass 2. The elapsed-time
    increase remains broad.
-   **Repeat check:** compared with the first September 13 run, the second
    is **+2.52% / +4.57%** on mean switch duration, with 25/33 and 18/33
    slower routes. The largest repeat increase is pass 2 row 15,
    FSR3 HoshiPa → Ultra Quality, at +500.35 ms. The full same-build
    comparison is retained separately, and both new runs remain in the
    tables alongside PR73.
-   **Stretch and retries:** Run 2 recovered all 32 selected stretch
    transitions; Run 1 recovered all 33.
    Run 2 full-pass stretch is **59 / 62 frames, 4.780 / 4.870 s**, versus PR73's
    **62 / 62 frames, 4.081 / 4.085 s**. No active tail remains.
    Retries are **14 / 15**, versus **15 / 15**: four DLSS viewport retries
    in each pass, with ten / eleven relatch requeues. Pass 1 row 20 has no
    relatch retry or stretch episode. Pass 1 row 1 retains a recovered
    two-frame episode. These are preserved observations, not failure counts.
-   **Largest Run 2 slowdowns versus PR73:** pass 1 rows 17, 28 and 14 add
    387.88, 288.26 and 265.08 ms. Pass 2 rows 15, 16 and 14 add
    686.13, 418.02 and 362.98 ms. The latter are successive FSR3 quality
    transitions; all complete and their full phase evidence is retained.
-   **Run 2 pacing diagnostic:** four of 64 between-transition intervals
    exceed
    the 250 ms allowance beyond the prescribed five-second wait:
    pass 1 before rows 28, 29, 31 and 32, at 270.88, 259.24, 263.79 and
    297.09 ms. Client dispatch overhead peaks at 49.87 ms. These gaps are
    outside the measured strict-completion interval and do not relabel
    terminal results; they remain an execution-context limitation.
-   **Limits:** formal improvement-or-neutral assessment is INCONCLUSIVE.
    Both applicable health standards are MET. The raw fixed stretch cutoff
    remains diagnostic during imposed settling; the scaled-only
    presentation gate remains a contract mismatch for proven native
    terminal presentation. Raw false cumulative acceptance is preserved.
    PR73 comparison has differing adapter LUID, scene and dependency
    records. The same-build comparison has differing scene/time records.
    Fixture fingerprints, driver versions and a versioned tolerance policy
    are unavailable. Memory classification is inconclusive, and zero
    resolved profiler samples prevent GPU-cost or FPS claims.

### Summary: PR73 baseline and both new runs

Paired values are **Pass 1 / Pass 2**. Switch durations and full-pass
stretch duration use **seconds**. Short cleanup/retry intervals remain
in **milliseconds**, with units shown. Strict completion excludes the
preceding five-second wait. Relatch means include 25 measured routes;
the remaining eight have no applicable/exposed exact-generation boundary.

| Metric                                         | PR73 baseline 554e484e3 | Run 1 e4cfd8f2f · 18:46 UTC | Run 2 e4cfd8f2f · 19:20 UTC |
| ---------------------------------------------- | ----------------------: | --------------------------: | --------------------------: |
| Mean switch, pass 1                            |                 0.815 s |                     0.918 s |                     0.941 s |
| Mean switch, pass 2                            |                 0.769 s |                     0.897 s |                     0.938 s |
| Mean switch change vs PR73, pass 1 / 2         |               Reference |           +12.60% / +16.72% |           +15.44% / +22.06% |
| Stretch frames, pass 1 / 2                     |                 62 / 62 |                     64 / 62 |                     59 / 62 |
| Stretch time, pass 1 / 2                       |         4.081 / 4.085 s |             4.716 / 4.713 s |             4.780 / 4.870 s |
| Retries, pass 1 / 2                            |                 15 / 15 |                     15 / 15 |                     14 / 15 |
| Mean retry→stable, pass 1 / 2                  |      336.16 / 337.90 ms |          381.67 / 398.63 ms |          413.17 / 411.15 ms |
| Maximum retry→stable, pass 1 / 2               |      463.47 / 461.56 ms |          547.22 / 538.98 ms |          574.52 / 668.08 ms |
| Mean cleanup tail, pass 1 / 2                  |        90.97 / 91.97 ms |          110.93 / 118.75 ms |          107.34 / 106.06 ms |
| Median switch, pass 1 / 2                      |         0.799 / 0.817 s |             0.990 / 1.003 s |             1.042 / 1.010 s |
| p95 switch, pass 1 / 2                         |         1.388 / 1.195 s |             1.507 / 1.369 s |             1.405 / 1.388 s |
| Maximum switch, pass 1 / 2                     |         1.424 / 1.206 s |             1.537 / 1.379 s |             1.484 / 1.503 s |
| Total switch time, pass 1 / 2                  |       26.898 / 25.362 s |           30.288 / 29.602 s |           31.050 / 30.956 s |
| Mean strict frames, pass 1 / 2                 |           15.18 / 14.82 |               14.85 / 14.58 |               14.76 / 15.00 |
| Mean relatch, pass 1 / 2                       |      736.86 / 698.56 ms |          845.35 / 813.46 ms |          843.30 / 847.31 ms |
| Mean relatch frames, pass 1 / 2                |           13.20 / 13.20 |               13.20 / 13.04 |               13.12 / 13.28 |
| Relatch samples, each pass                     |                   25/33 |                       25/33 |                       25/33 |
| Stretch episodes, pass 1 / 2                   |                 18 / 18 |                     19 / 18 |                     18 / 18 |
| Terminal PASS, each pass                       |                   33/33 |                       33/33 |                       33/33 |
| Task 2 PASS / FAIL / INCONCLUSIVE, both passes |              66 / 0 / 0 |                  66 / 0 / 0 |                  66 / 0 / 0 |
| Device loss                                    |                       0 |                           0 |                           0 |
| OOM                                            |                       0 |                           0 |                           0 |
| Producer terminal failure                      |                       0 |                           0 |                           0 |
| Vendor-native qualification failure            |                       0 |                           0 |                           0 |
| Credible liveness timeout                      |                       0 |                           0 |                           0 |
| Applicable health standard, both passes        |                     MET |                         MET |                         MET |
| Active stretch at stop, both passes            |                    None |                        None |                        None |
| Memory classification                          |            inconclusive |                inconclusive |                inconclusive |
| Resolved profiler samples                      |             Unavailable |                 Unavailable |                 Unavailable |

### One transition table: all three runs, both passes

**B** = PR73 554e484e3; **R1** = e4cfd8f2f at 18:46 UTC;
**R2** = e4cfd8f2f at 19:20 UTC. Within each B/R1/R2 line values are
**Pass 1 / Pass 2**. All rows have terminal PASS and Task 2 PASS in
all three runs and both passes.
Strict and relatch are in **seconds**; cleanup, stretch, retry and
provider-wait columns are in **milliseconds**. Frames appear as (nf).

Relatch is qualification dispatch to first exact new-generation proof.
**—** means no applicable observed interval, never zero cost.
**V** = DLSS viewport recycle; **R** = render-target relatch requeue.
Retry→stable retains each event; viewport wait is the longest concurrent
role wait; drain wait is owned Pending→Ready. These intervals overlap
other phases and must not be added. HP=HoshiPa, UQ=Ultra Quality,
Q=Quality, B=Balanced, P=Performance, UP=Ultra Performance, AA=Native AA.

|   # | Transition        | Strict s (frames)                                                                              | Relatch s (frames)                                                                             | Cleanup tail ms                                                  | Stretch frames (ms)                                                                            | Retries                                  | Retry→stable ms                                                  | Viewport wait ms                                          | Drain wait ms                                              |
| --: | ----------------- | ---------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------------- | --------------------------------------------------------- | ---------------------------------------------------------- |
|   1 | DLSS HP → NONE    | B: 0.776 (14f) / 0.751 (15f)<br>R1: 0.865 (14f) / 0.847 (14f)<br>R2: 0.885 (16f) / 0.991 (15f) | B: 0.527 (9f) / 0.491 (10f)<br>R1: 0.592 (9f) / 0.570 (9f)<br>R2: 0.606 (10f) / 0.667 (10f)    | B: 183.09 / 194.75<br>R1: 215.42 / 217.30<br>R2: 217.40 / 250.21 | B: 0f (0.00) / 0f (0.00)<br>R1: 2f (175.74) / 0f (0.00)<br>R2: 2f (149.88) / 0f (0.00)         | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   2 | NONE → TAA        | B: 0.180 (3f) / 0.173 (5f)<br>R1: 0.205 (4f) / 0.211 (4f)<br>R2: 0.213 (4f) / 0.241 (4f)       | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   3 | TAA → DLAA        | B: 0.309 (3f) / 0.286 (3f)<br>R1: 0.288 (3f) / 0.318 (3f)<br>R2: 0.305 (3f) / 0.290 (3f)       | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   4 | DLAA → DLSS HP    | B: 1.214 (19f) / 1.085 (19f)<br>R1: 1.196 (19f) / 1.175 (18f)<br>R2: 1.317 (19f) / 1.227 (20f) | B: 0.878 (14f) / 0.813 (14f)<br>R1: 0.896 (14f) / 0.868 (13f)<br>R2: 0.991 (14f) / 0.918 (15f) | B: 121.85 / 92.16<br>R1: 102.69 / 105.51<br>R2: 110.40 / 108.16  | B: 2f (254.10) / 2f (214.50)<br>R1: 2f (233.77) / 2f (226.70)<br>R2: 2f (268.32) / 2f (229.89) | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   5 | DLSS HP → DLSS UQ | B: 1.381 (19f) / 1.180 (18f)<br>R1: 1.413 (18f) / 1.209 (20f)<br>R2: 1.308 (20f) / 1.307 (19f) | B: 1.082 (14f) / 0.861 (13f)<br>R1: 1.113 (13f) / 0.906 (15f)<br>R2: 0.998 (15f) / 0.992 (14f) | B: 148.13 / 104.05<br>R1: 155.94 / 158.06<br>R2: 108.17 / 160.16 | B: 2f (217.52) / 2f (245.00)<br>R1: 2f (247.74) / 2f (241.78)<br>R2: 2f (239.24) / 2f (266.92) | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   6 | DLSS UQ → DLSS Q  | B: 1.178 (22f) / 1.195 (22f)<br>R1: 1.514 (22f) / 1.379 (21f)<br>R2: 1.394 (22f) / 1.337 (21f) | B: 0.892 (17f) / 0.917 (17f)<br>R1: 1.212 (17f) / 1.069 (16f)<br>R2: 1.080 (17f) / 1.037 (16f) | B: 145.22 / 90.54<br>R1: 107.66 / 160.53<br>R2: 166.67 / 105.83  | B: 5f (337.27) / 5f (345.61)<br>R1: 5f (384.77) / 5f (406.16)<br>R2: 5f (428.95) / 5f (397.03) | B: 1V / 1V<br>R1: 1V / 1V<br>R2: 1V / 1V | B: 387.61 / 403.98<br>R1: 449.90 / 469.08<br>R2: 492.49 / 468.74 | B: 49.35 / 57.22<br>R1: 64.36 / 62.15<br>R2: 62.69 / 1.59 | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   7 | DLSS Q → DLSS B   | B: 1.424 (21f) / 1.206 (23f)<br>R1: 1.537 (23f) / 1.377 (22f)<br>R2: 1.395 (22f) / 1.318 (21f) | B: 1.137 (16f) / 0.936 (18f)<br>R1: 1.234 (18f) / 1.060 (17f)<br>R2: 1.070 (17f) / 1.010 (16f) | B: 96.13 / 138.52<br>R1: 162.16 / 106.50<br>R2: 113.58 / 156.67  | B: 5f (345.48) / 5f (354.16)<br>R1: 5f (380.56) / 5f (396.32)<br>R2: 5f (407.20) / 5f (386.72) | B: 1V / 1V<br>R1: 1V / 1V<br>R2: 1V / 1V | B: 398.37 / 407.17<br>R1: 440.57 / 456.14<br>R2: 476.44 / 458.67 | B: 52.49 / 52.66<br>R1: 59.22 / 59.18<br>R2: 68.79 / 1.81 | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   8 | DLSS B → DLSS P   | B: 1.398 (22f) / 1.193 (23f)<br>R1: 1.503 (21f) / 1.364 (21f)<br>R2: 1.484 (23f) / 1.320 (21f) | B: 1.114 (17f) / 0.928 (18f)<br>R1: 1.194 (16f) / 1.037 (16f)<br>R2: 1.159 (18f) / 1.009 (16f) | B: 144.06 / 135.58<br>R1: 166.65 / 176.00<br>R2: 106.27 / 109.46 | B: 5f (337.34) / 5f (351.01)<br>R1: 5f (376.17) / 5f (422.74)<br>R2: 5f (421.93) / 5f (384.46) | B: 1V / 1V<br>R1: 1V / 1V<br>R2: 1V / 1V | B: 394.05 / 406.08<br>R1: 434.33 / 483.23<br>R2: 495.23 / 449.53 | B: 52.38 / 51.17<br>R1: 54.73 / 0.95<br>R2: 70.01 / 61.66 | B: — / —<br>R1: — / —<br>R2: — / —                         |
|   9 | DLSS P → DLSS UP  | B: 1.367 (22f) / 1.195 (22f)<br>R1: 1.416 (21f) / 1.292 (22f)<br>R2: 1.376 (22f) / 1.352 (22f) | B: 1.063 (17f) / 0.915 (17f)<br>R1: 1.118 (16f) / 0.982 (17f)<br>R2: 1.059 (17f) / 1.026 (17f) | B: 142.87 / 152.06<br>R1: 158.48 / 157.78<br>R2: 118.31 / 109.63 | B: 5f (343.31) / 5f (332.71)<br>R1: 5f (373.36) / 5f (373.62)<br>R2: 5f (381.52) / 5f (403.85) | B: 1V / 1V<br>R1: 1V / 1V<br>R2: 1V / 1V | B: 395.82 / 385.44<br>R1: 429.19 / 433.84<br>R2: 455.35 / 468.82 | B: 48.85 / 1.41<br>R1: 52.85 / 56.75<br>R2: 70.52 / 1.45  | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  10 | DLSS UP → DLAA    | B: 0.799 (15f) / 0.805 (14f)<br>R1: 0.950 (16f) / 0.876 (14f)<br>R2: 0.946 (16f) / 0.978 (15f) | B: 0.617 (11f) / 0.625 (10f)<br>R1: 0.716 (12f) / 0.668 (10f)<br>R2: 0.716 (11f) / 0.740 (11f) | B: 134.68 / 133.39<br>R1: 178.00 / 155.83<br>R2: 175.81 / 179.88 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  11 | DLAA → TAA        | B: 0.465 (9f) / 0.485 (10f)<br>R1: 0.531 (10f) / 0.590 (10f)<br>R2: 0.582 (9f) / 0.581 (9f)    | B: 0.176 (3f) / 0.197 (3f)<br>R1: 0.215 (4f) / 0.224 (3f)<br>R2: 0.228 (3f) / 0.222 (3f)       | B: 288.46 / 287.14<br>R1: 315.70 / 364.83<br>R2: 352.93 / 357.24 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  12 | TAA → NONE        | B: 0.176 (4f) / 0.160 (4f)<br>R1: 0.207 (4f) / 0.215 (4f)<br>R2: 0.215 (4f) / 0.212 (4f)       | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  13 | NONE → FSR3 AA    | B: 0.921 (11f) / 0.633 (11f)<br>R1: 0.873 (11f) / 0.748 (11f)<br>R2: 0.780 (11f) / 0.766 (11f) | B: 0.877 (10f) / 0.588 (10f)<br>R1: 0.820 (10f) / 0.688 (10f)<br>R2: 0.725 (10f) / 0.712 (10f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  14 | FSR3 AA → FSR3 HP | B: 1.155 (24f) / 1.079 (23f)<br>R1: 1.318 (23f) / 1.268 (22f)<br>R2: 1.421 (23f) / 1.442 (23f) | B: 0.912 (19f) / 0.864 (18f)<br>R1: 1.039 (18f) / 1.004 (17f)<br>R2: 1.082 (18f) / 1.167 (18f) | B: 106.07 / 127.66<br>R1: 109.21 / 106.41<br>R2: 0.00 / 0.00     | B: 5f (219.26) / 5f (221.96)<br>R1: 5f (265.71) / 5f (274.78)<br>R2: 5f (273.53) / 5f (354.23) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 463.47 / 461.56<br>R1: 547.22 / 538.98<br>R2: 574.52 / 668.08 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 49.09 / 46.40<br>R1: 61.45 / 56.02<br>R2: 57.25 / 61.65 |
|  15 | FSR3 HP → FSR3 UQ | B: 1.003 (21f) / 0.817 (17f)<br>R1: 0.990 (18f) / 1.003 (18f)<br>R2: 1.059 (18f) / 1.503 (26f) | B: 0.684 (14f) / 0.688 (14f)<br>R1: 0.780 (14f) / 0.784 (14f)<br>R2: 0.812 (14f) / 0.848 (14f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 4f (207.81) / 4f (223.74)<br>R1: 4f (237.31) / 4f (246.90)<br>R2: 4f (258.50) / 4f (283.40) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 255.78 / 273.15<br>R1: 292.94 / 302.26<br>R2: 315.70 / 342.19 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 47.15 / 48.83<br>R1: 55.38 / 54.39<br>R2: 56.65 / 59.01 |
|  16 | FSR3 UQ → FSR3 Q  | B: 1.008 (21f) / 0.819 (18f)<br>R1: 1.045 (19f) / 1.044 (19f)<br>R2: 1.114 (19f) / 1.237 (18f) | B: 0.705 (14f) / 0.641 (14f)<br>R1: 0.782 (14f) / 0.768 (14f)<br>R2: 0.839 (14f) / 1.048 (15f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 4f (239.81) / 4f (205.17)<br>R1: 4f (234.46) / 4f (238.88)<br>R2: 4f (261.86) / 4f (256.97) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 287.58 / 249.41<br>R1: 290.39 / 293.09<br>R2: 326.15 / 326.29 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 47.62 / 43.52<br>R1: 55.55 / 53.67<br>R2: 63.99 / 68.32 |
|  17 | FSR3 Q → FSR3 B   | B: 0.769 (18f) / 0.821 (18f)<br>R1: 1.015 (18f) / 1.017 (18f)<br>R2: 1.157 (18f) / 1.097 (18f) | B: 0.605 (14f) / 0.652 (14f)<br>R1: 0.800 (14f) / 0.784 (14f)<br>R2: 0.907 (14f) / 0.875 (14f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 4f (189.40) / 4f (204.05)<br>R1: 4f (235.61) / 4f (239.86)<br>R2: 4f (328.54) / 4f (272.01) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 232.22 / 249.59<br>R1: 298.73 / 295.77<br>R2: 387.28 / 342.91 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 42.36 / 44.91<br>R1: 62.49 / 55.79<br>R2: 57.53 / 70.58 |
|  18 | FSR3 B → FSR3 P   | B: 0.786 (17f) / 0.818 (18f)<br>R1: 0.994 (18f) / 1.066 (18f)<br>R2: 1.044 (17f) / 1.010 (18f) | B: 0.620 (13f) / 0.634 (14f)<br>R1: 0.773 (14f) / 0.850 (14f)<br>R2: 0.830 (13f) / 0.799 (14f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 4f (194.84) / 4f (198.06)<br>R1: 4f (233.68) / 4f (250.78)<br>R2: 4f (274.88) / 4f (248.13) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 239.04 / 244.28<br>R1: 290.20 / 314.59<br>R2: 334.78 / 303.82 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 44.06 / 46.12<br>R1: 56.32 / 63.40<br>R2: 59.57 / 55.27 |
|  19 | FSR3 P → FSR3 UP  | B: 1.223 (27f) / 0.940 (21f)<br>R1: 1.116 (20f) / 0.950 (17f)<br>R2: 1.167 (19f) / 1.090 (19f) | B: 0.642 (14f) / 0.648 (14f)<br>R1: 0.793 (14f) / 0.793 (14f)<br>R2: 0.818 (14f) / 0.773 (14f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 4f (196.38) / 4f (198.30)<br>R1: 4f (234.22) / 4f (233.25)<br>R2: 4f (254.09) / 4f (237.17) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 243.98 / 243.62<br>R1: 292.58 / 297.83<br>R2: 312.34 / 292.22 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 47.27 / 44.69<br>R1: 57.88 / 64.22<br>R2: 57.80 / 54.70 |
|  20 | FSR3 UP → FSR3 AA | B: 0.953 (22f) / 1.010 (21f)<br>R1: 1.181 (22f) / 1.309 (22f)<br>R2: 1.042 (15f) / 1.216 (21f) | B: 0.724 (16f) / 0.782 (16f)<br>R1: 0.900 (16f) / 0.985 (16f)<br>R2: 0.658 (10f) / 0.921 (16f) | B: 184.45 / 177.50<br>R1: 221.94 / 256.82<br>R2: 300.60 / 227.80 | B: 5f (253.68) / 5f (283.34)<br>R1: 5f (308.93) / 5f (362.01)<br>R2: 0f (0.00) / 5f (323.68)   | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 0 / 1R  | B: 338.90 / 377.56<br>R1: 415.93 / 480.87<br>R2: — / 433.83      | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  21 | FSR3 AA → TAA     | B: 0.468 (10f) / 0.453 (11f)<br>R1: 0.566 (10f) / 0.570 (9f)<br>R2: 0.638 (11f) / 0.559 (10f)  | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 258.57 / 263.99<br>R1: 327.53 / 338.96<br>R2: 375.43 / 327.89 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  22 | TAA → NONE        | B: 0.159 (3f) / 0.163 (4f)<br>R1: 0.206 (4f) / 0.213 (4f)<br>R2: 0.227 (4f) / 0.205 (4f)       | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  23 | NONE → DLAA       | B: 0.241 (3f) / 0.289 (4f)<br>R1: 0.292 (3f) / 0.297 (3f)<br>R2: 0.296 (3f) / 0.324 (3f)       | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  24 | DLAA → FSR3 AA    | B: 0.725 (16f) / 0.797 (15f)<br>R1: 0.921 (15f) / 1.045 (15f)<br>R2: 0.986 (16f) / 0.978 (16f) | B: 0.557 (12f) / 0.611 (11f)<br>R1: 0.710 (11f) / 0.803 (11f)<br>R2: 0.764 (12f) / 0.744 (12f) | B: 127.78 / 134.14<br>R1: 160.49 / 184.36<br>R2: 170.22 / 173.42 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 180.68 / 199.09<br>R1: 226.56 / 266.86<br>R2: 241.21 / 231.72 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  25 | FSR3 AA → DLSS HP | B: 1.127 (20f) / 1.092 (20f)<br>R1: 1.220 (19f) / 1.281 (20f)<br>R2: 1.354 (20f) / 1.292 (20f) | B: 0.874 (15f) / 0.811 (15f)<br>R1: 0.914 (14f) / 0.971 (15f)<br>R2: 1.028 (15f) / 0.983 (15f) | B: 127.19 / 144.82<br>R1: 163.09 / 166.42<br>R2: 177.36 / 163.81 | B: 2f (213.48) / 2f (203.31)<br>R1: 2f (232.64) / 2f (230.93)<br>R2: 2f (235.05) / 2f (227.83) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 428.46 / 402.91<br>R1: 445.12 / 481.77<br>R2: 478.67 / 467.55 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 48.17 / 44.50<br>R1: 54.04 / 55.05<br>R2: 58.06 / 55.97 |
|  26 | DLSS HP → FSR3 HP | B: 1.010 (20f) / 1.068 (19f)<br>R1: 1.177 (19f) / 1.181 (20f)<br>R2: 1.262 (21f) / 1.284 (20f) | B: 0.795 (15f) / 0.795 (14f)<br>R1: 0.915 (14f) / 0.910 (15f)<br>R2: 0.983 (16f) / 1.016 (15f) | B: 46.81 / 0.00<br>R1: 53.06 / 161.18<br>R2: 0.00 / 0.00         | B: 3f (206.47) / 3f (220.36)<br>R1: 3f (246.16) / 3f (246.74)<br>R2: 3f (241.94) / 3f (249.89) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 373.86 / 387.09<br>R1: 447.86 / 432.23<br>R2: 429.32 / 454.90 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 46.01 / 46.65<br>R1: 63.62 / 55.64<br>R2: 57.70 / 73.09 |
|  27 | FSR3 HP → NONE    | B: 0.727 (16f) / 0.749 (16f)<br>R1: 0.857 (16f) / 0.930 (15f)<br>R2: 0.921 (16f) / 0.901 (16f) | B: 0.494 (10f) / 0.512 (10f)<br>R1: 0.576 (10f) / 0.616 (10f)<br>R2: 0.602 (10f) / 0.587 (10f) | B: 180.19 / 181.19<br>R1: 221.04 / 228.50<br>R2: 229.00 / 227.00 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  28 | NONE → FSR3 UP    | B: 0.884 (16f) / 0.974 (17f)<br>R1: 1.101 (17f) / 1.111 (17f)<br>R2: 1.173 (17f) / 1.136 (17f) | B: 0.623 (10f) / 0.744 (12f)<br>R1: 0.835 (12f) / 0.838 (12f)<br>R2: 0.903 (12f) / 0.872 (12f) | B: 0.00 / 49.00<br>R1: 113.58 / 115.24<br>R2: 56.43 / 104.63     | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  29 | FSR3 UP → DLSS UP | B: 1.054 (20f) / 1.039 (20f)<br>R1: 1.241 (20f) / 1.252 (20f)<br>R2: 1.271 (20f) / 1.292 (20f) | B: 0.792 (15f) / 0.780 (15f)<br>R1: 0.941 (15f) / 0.906 (15f)<br>R2: 0.946 (15f) / 0.982 (15f) | B: 136.78 / 136.73<br>R1: 160.40 / 196.27<br>R2: 113.17 / 165.66 | B: 3f (324.55) / 3f (284.08)<br>R1: 3f (315.45) / 3f (321.68)<br>R2: 3f (354.80) / 3f (347.65) | B: 1R / 1R<br>R1: 1R / 1R<br>R2: 1R / 1R | B: 422.59 / 377.54<br>R1: 423.60 / 432.95<br>R2: 464.88 / 458.04 | B: — / —<br>R1: — / —<br>R2: — / —                        | B: 51.24 / 47.16<br>R1: 52.82 / 54.96<br>R2: 56.33 / 55.71 |
|  30 | DLSS UP → TAA     | B: 0.702 (17f) / 0.721 (15f)<br>R1: 0.893 (16f) / 0.858 (15f)<br>R2: 0.910 (14f) / 0.856 (15f) | B: 0.470 (11f) / 0.472 (9f)<br>R1: 0.604 (11f) / 0.570 (9f)<br>R2: 0.574 (9f) / 0.565 (10f)    | B: 175.57 / 195.31<br>R1: 231.39 / 227.33<br>R2: 268.70 / 229.34 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  31 | TAA → FSR3 AA     | B: 0.607 (12f) / 0.602 (11f)<br>R1: 0.726 (11f) / 0.734 (11f)<br>R2: 0.759 (11f) / 0.719 (11f) | B: 0.563 (10f) / 0.559 (10f)<br>R1: 0.663 (10f) / 0.682 (10f)<br>R2: 0.706 (10f) / 0.669 (10f) | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  32 | FSR3 AA → NONE    | B: 0.443 (11f) / 0.482 (9f)<br>R1: 0.588 (11f) / 0.564 (11f)<br>R2: 0.686 (11f) / 0.600 (11f)  | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 254.25 / 296.33<br>R1: 336.33 / 334.90<br>R2: 381.85 / 343.28 | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |
|  33 | NONE → DLAA       | B: 0.265 (3f) / 0.282 (3f)<br>R1: 0.343 (5f) / 0.307 (3f)<br>R2: 0.363 (3f) / 0.295 (4f)       | B: — / —<br>R1: — / —<br>R2: — / —                                                             | B: 0.00 / 0.00<br>R1: 0.00 / 0.00<br>R2: 0.00 / 0.00             | B: 0f (0.00) / 0f (0.00)<br>R1: 0f (0.00) / 0f (0.00)<br>R2: 0f (0.00) / 0f (0.00)             | B: 0 / 0<br>R1: 0 / 0<br>R2: 0 / 0       | B: — / —<br>R1: — / —<br>R2: — / —                               | B: — / —<br>R1: — / —<br>R2: — / —                        | B: — / —<br>R1: — / —<br>R2: — / —                         |

## Provenance and complete evidence

Current run: renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z. Previous same-build run:
renderscale-tuning-nvidia-2026-09-13T18-46-33-800Z. PR73 baseline:
renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z.

Current compiled source, renderer base and main-VR base are
e4cfd8f2f842380586a1f0e6bb0dd4ded6538d41. Current Build ID is
2f6432ddbcbd05826c1fb1ccd66f25e14755d718a5e760a58cbae58b7f625efd.
PR73 source/renderer base is 554e484e3957178e2d144bf35266bfdcc0948642,
main-VR base ef7c366dd73989b2b87751c0ef975db7c6fd310f, and Build ID
e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96.

The runtime remains PID 38084. The known profile path was reused from
the preserved first-run MO2 inspection; the current enabled mod list,
exact physical DLL and adjacent manifest were checked directly again.
There is one enabled loose DLL provider and no provider in Overwrite
or unmanaged Data. The physical DLL is 28,784,128 bytes with SHA-256
da3a9b54ee34dee836e3a2843d33acb8d33e49ab5754be06c05f37e038fbcdb0.
Its AIO receipt and manifest agree with the preserved runtime producer.

-   [Ledger 0005](vr-render-scale-ledger-0005-pr82.csv) retains the PR73
    baseline and both current-build runs. Earlier snapshots are unchanged.
    Its three columns preserve both requested comparisons; the PR tables
    show only PR73 and the second run.
-   [Complete second-run results](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-repeat-192024-run.md).
-   [Every PR73-relative transition/pass delta](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-repeat-192024-comparison.md).
-   [Every first-run-relative transition/pass delta](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-repeat-192024-first-run-comparison.md).
-   [Pacing intervals with exact QPC endpoints](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-repeat-192024-pacing.json).
-   [Field coverage and timing verification](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-repeat-192024-validation.json).

The generated diagnostic reports and machine-readable ledger retain their
original explicitly labeled producer units. The presentation above uses
seconds for switch durations and milliseconds for shorter intervals.
Raw journal, full scalar CSV, all revisions, receipt hashes, deployment
proof and command receipts remain local under artifacts/renderscale-tuning/
renderscale-tuning-nvidia-2026-09-13T19-20-24-614Z/. The profiler and pacing recurrences were attached to local
feedback AUTO-20260911-154621563-951D86E2 and
AUTO-20260911-160853983-FFE590F5; neither caused a replay.

## Validation

Both passes completed 33/33 rows. Terminal and Task 2 counts are 66 PASS,
0 FAIL, 0 INCONCLUSIVE. Full-history health is NO_COUNTED_FAILURES;
reporting COMPLETE; cleanup verified inactive; pending evidence zero.
No PR82-head, SE/AE runtime or visual release qualification is claimed.

The offline finalizer succeeded. The repository comparison wrapper ran
once for PR73 and once for the first same-build run, after ledger staging.
All 1,056 numeric timing cells passed for each comparison; 528 are from
the second run. All three complete summaries and both complete
comparisons reconstruct exactly from ledger 0005; every original cell of
snapshot 0004 is preserved. The CSV is 101,265,210 bytes, below
100 MiB. Comparison generation/audits took 8.912 s;
ledger preparation and complete coverage/timing audits took
16.155 s. Exact commands and results are retained in
finalization-command-result.json, pr73-comparison-command-result.json
and first_run-comparison-command-result.json.
