# PR75 NVIDIA tuning compared with main-VR, 10 September 2026

<!-- pr75-nvidia-mainvr-comparison-v1 -->

### NVIDIA comparison: main-VR without PR75 vs PR75

**PR75 averaged 810.4 ms per switch versus
849.5 ms for main-VR without PR75: 39.2 ms
lower (4.6%).** The mean was 5.75% lower in pass 1 and
3.42% lower in pass 2. The baseline's recurring fidelity/vendor-fallback
observations on rows 26 and 28 were absent in both PR75 passes.

Both builds completed 66/66 transitions with terminal PASS and 66 Task 2
PASS classifications. Applicable health changed from NOT_MET in both
baseline passes to MET in both PR75 passes. The formal improvement-or-neutral
assessment remains **INCONCLUSIVE**: weather and game hour differed, the
complete fixture fingerprint is unavailable, and no versioned tolerance
policy was supplied. These observed switch-latency deltas are descriptive.

Baseline is exact main-VR `7c8e3e656` without PR75. Candidate is exact compiled PR75
source `c615779a9` on that same main-VR base. Each group contains one process
run with two ordered 33-transition passes. No interrupted attempt is pooled
into either mean.

#### Main comparison

Lower times are better. Each timing row first averages the same routes
within each pass: 33 for completion/presentation/cleanup, 25 applicable
relatch boundaries. Stretch rows use each pass's total. The final columns
show ± one standard error across the two retained pass summaries.

| Measurement                    | Main-VR mean (2 passes) | PR75 mean (2 passes) |            Change | ±SE main-VR | ±SE PR75 |
| ------------------------------ | ----------------------: | -------------------: | ----------------: | ----------: | -------: |
| Switch completion (ms)         |                   849.5 |                810.4 |   -39.154 (-4.6%) |       ±16.5 |     ±5.8 |
| Presentation ready (ms)        |                   715.4 |                686.4 |   -29.029 (-4.1%) |       ±16.4 |     ±1.1 |
| Cleanup tail (ms)              |                   108.5 |                100.5 |    -7.920 (-7.3%) |      ±0.009 |     ±5.5 |
| Relatch proof (ms)             |                   785.6 |                752.0 |   -33.565 (-4.3%) |       ±21.8 |    ±20.1 |
| Switch completion (frames)     |                    15.5 |                 15.5 |    +0.015 (+0.1%) |      ±0.061 |     ±0.2 |
| Relatch proof (frames)         |                    13.9 |                 13.9 |    +0.020 (+0.1%) |      ±0.040 |   ±0.020 |
| Stretch episodes per pass      |                    19.0 |                 18.0 |    -1.000 (-5.3%) |        ±0.0 |     ±1.0 |
| Stretch frames per pass        |                    86.0 |                 80.0 |    -6.000 (-7.0%) |        ±3.0 |     ±3.0 |
| Stretch duration per pass (ms) |                 5,551.5 |              4,861.8 | -689.779 (-12.4%) |      ±168.4 |   ±277.7 |

SE = sample standard deviation of the two pass summaries divided by √2.
It describes within-run variation; the ordered passes are not independent
experiments. It is not a confidence interval or a significance test.

#### Each pass remains visible

| Pass | Main-VR strict mean ms | PR75 strict mean ms |  Change | Main-VR / PR75 p95 ms | Main-VR / PR75 maximum ms | Health main-VR / PR75 |
| ---- | ---------------------: | ------------------: | ------: | --------------------- | ------------------------- | --------------------- |
| 1    |                866.009 |             816.198 | -5.752% | 1454.714 / 1450.504   | 2184.558 / 1848.429       | NOT_MET / MET         |
| 2    |                833.078 |             804.581 | -3.421% | 1450.874 / 1415.251   | 1918.325 / 1844.264       | NOT_MET / MET         |

The largest slower route was **row 25, FSR3 Native AA → DLSS Hoshipa**:
1,002.6 → 1,689.4 ms
(+68.5%). It was slower in both passes. Lower overall
means do not establish that every route improved.

#### Health and recovery

Counts below are totals across each build's two passes.

| Result                                  | Main-VR without PR75 |       PR75 |
| --------------------------------------- | -------------------: | ---------: |
| Completed / terminal PASS               |              66 / 66 |    66 / 66 |
| Task 2 PASS / FAIL / INCONCLUSIVE       |           66 / 0 / 0 | 66 / 0 / 0 |
| Switches with recovered failures        |               4 / 66 |     0 / 66 |
| Fidelity mismatch observations          |                    8 |          0 |
| Vendor-failure stretch-eye observations |                    4 |          0 |
| Producer retry occurrences              |                   20 |         20 |
| Separate recovery applies / replays     |                    0 |          0 |
| Device loss                             |                    0 |          0 |
| OOM                                     |                    0 |          0 |
| Producer terminal failures              |                    0 |          0 |
| Vendor-native qualification failures    |                    0 |          0 |
| Credible liveness timeouts              |                    0 |          0 |

The baseline had two fidelity observations and one vendor-failure eye
observation on each of rows 26 (DLSS Hoshipa → FSR3 Hoshipa) and 28
(None → FSR3 UP), in each pass. They recovered within the original switch.
PR75 retained zero on both routes and throughout both complete histories.
Observation counts are not crashes or unique frames.

Raw cumulative acceptance is false in all four passes. Baseline fidelity
and fallback gates are applicable failures; they pass in both PR75 passes.
The fixed two-frame stretch cutoff remains DIAGNOSTIC_ONLY because settling
imposes stretch. The scaled-presentation gate after proven native-AA output
remains CONTRACT_MISMATCH. Both raw results, observed values, limits and
applicability are preserved. Neither excluded gate counts against health.

<details>
<summary>Every switch: two-pass mean and SE</summary>

Times are ms; negative change is lower. Each row uses two observations
of that exact route for each build. All original pass results remain in
the linked detailed report and canonical ledger.

| Row | Switch                           | Main-VR mean | PR75 mean |          Change | ±SE main-VR | ±SE PR75 |
| --- | -------------------------------- | -----------: | --------: | --------------: | ----------: | -------: |
| 1   | DLSS Hoshipa → NONE              |        702.7 |     745.0 |   +42.3 (+6.0%) |       ±14.2 |     ±7.2 |
| 2   | NONE → TAA                       |        169.8 |     177.8 |    +8.0 (+4.7%) |        ±0.4 |     ±8.4 |
| 3   | TAA → DLAA                       |        267.7 |     284.6 |   +16.8 (+6.3%) |       ±18.6 |    ±17.6 |
| 4   | DLAA → DLSS Hoshipa              |      1,018.5 |     994.3 |   -24.2 (-2.4%) |       ±14.1 |    ±45.3 |
| 5   | DLSS Hoshipa → DLSS UQ           |      1,146.7 |   1,108.7 |   -38.0 (-3.3%) |      ±119.5 |   ±123.4 |
| 6   | DLSS UQ → DLSS Quality           |      1,172.6 |   1,129.7 |   -42.9 (-3.7%) |        ±3.1 |    ±20.9 |
| 7   | DLSS Quality → DLSS Balanced     |      1,392.0 |   1,244.5 | -147.5 (-10.6%) |        ±1.4 |    ±86.2 |
| 8   | DLSS Balanced → DLSS Performance |      1,274.0 |   1,244.9 |   -29.1 (-2.3%) |       ±78.1 |    ±92.4 |
| 9   | DLSS Performance → DLSS UP       |      1,318.0 |   1,179.8 | -138.2 (-10.5%) |       ±68.6 |    ±74.8 |
| 10  | DLSS UP → DLAA                   |        842.3 |     786.0 |   -56.3 (-6.7%) |       ±72.0 |    ±34.2 |
| 11  | DLAA → TAA                       |        465.2 |     445.0 |   -20.2 (-4.3%) |       ±15.1 |    ±14.7 |
| 12  | TAA → NONE                       |        181.8 |     176.5 |    -5.3 (-2.9%) |        ±8.9 |     ±4.3 |
| 13  | NONE → FSR3 Native AA            |        721.5 |     671.8 |   -49.6 (-6.9%) |      ±101.5 |    ±84.0 |
| 14  | FSR3 Native AA → FSR3 Hoshipa    |      1,392.8 |   1,343.2 |   -49.6 (-3.6%) |       ±15.6 |    ±30.4 |
| 15  | FSR3 Hoshipa → FSR3 UQ           |        847.5 |   1,042.8 | +195.3 (+23.0%) |       ±97.7 |   ±348.4 |
| 16  | FSR3 UQ → FSR3 Quality           |        859.2 |     761.2 |  -98.0 (-11.4%) |       ±70.0 |    ±20.1 |
| 17  | FSR3 Quality → FSR3 Balanced     |        797.4 |     735.6 |   -61.8 (-7.8%) |       ±34.5 |     ±6.6 |
| 18  | FSR3 Balanced → FSR3 Performance |      1,079.0 |     738.7 | -340.4 (-31.5%) |      ±321.5 |    ±10.3 |
| 19  | FSR3 Performance → FSR3 UP       |        768.7 |     743.9 |   -24.8 (-3.2%) |        ±9.8 |    ±16.5 |
| 20  | FSR3 UP → FSR3 Native AA         |        892.7 |     726.7 | -166.0 (-18.6%) |      ±160.9 |    ±20.9 |
| 21  | FSR3 Native AA → TAA             |        533.8 |     454.0 |  -79.7 (-14.9%) |       ±38.7 |     ±6.5 |
| 22  | TAA → NONE                       |        194.9 |     169.9 |  -24.9 (-12.8%) |       ±22.2 |     ±1.2 |
| 23  | NONE → DLAA                      |        258.4 |     275.4 |   +17.0 (+6.6%) |        ±3.4 |    ±26.7 |
| 24  | DLAA → FSR3 Native AA            |      1,176.0 |   1,088.5 |   -87.4 (-7.4%) |       ±57.6 |     ±6.7 |
| 25  | FSR3 Native AA → DLSS Hoshipa    |      1,002.6 |   1,689.4 | +686.8 (+68.5%) |       ±25.9 |   ±154.8 |
| 26  | DLSS Hoshipa → FSR3 Hoshipa      |      1,525.3 |   1,394.5 |  -130.8 (-8.6%) |       ±10.7 |     ±0.1 |
| 27  | FSR3 Hoshipa → NONE              |        853.6 |     738.2 | -115.4 (-13.5%) |       ±79.4 |     ±9.8 |
| 28  | NONE → FSR3 UP                   |        932.0 |     907.0 |   -25.0 (-2.7%) |        ±7.2 |    ±13.2 |
| 29  | FSR3 UP → DLSS UP                |      2,051.4 |   1,647.3 | -404.1 (-19.7%) |      ±133.1 |   ±201.1 |
| 30  | DLSS UP → TAA                    |        785.8 |     695.9 |  -89.9 (-11.4%) |       ±41.3 |     ±9.5 |
| 31  | TAA → FSR3 Native AA             |        619.4 |     646.9 |   +27.5 (+4.4%) |        ±8.7 |    ±57.3 |
| 32  | FSR3 Native AA → NONE            |        506.5 |     482.5 |   -24.0 (-4.7%) |       ±24.9 |    ±18.5 |
| 33  | NONE → DLAA                      |        285.1 |     272.5 |   -12.6 (-4.4%) |       ±11.4 |    ±14.5 |

</details>

<details>
<summary>Exact builds, memory, evidence and limits</summary>

| Identity             | Main-VR without PR75                                               | PR75                                                               |
| -------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Compiled source      | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         | `c615779a903d7e3f8f95c7edcf303a02ad08dced`                         |
| Renderer source/base | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         | `c615779a903d7e3f8f95c7edcf303a02ad08dced`                         |
| Main-VR base         | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         |
| Build ID             | `9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4` | `cbef73dfc7a11e26e01ad8ecdca03c8d45d4c6549ddfd3a51b8a0c537499014d` |
| Run ID               | `nvidia-20260910T124329625Z`                                       | `nvidia-2026-09-10T17-12-40-813Z`                                  |

Both runs used the RTX 5070 Ti Laptop GPU, the same Dragonsreach position,
1512 × 1680 output per eye, DLSS K, explicit FSR3, and foveation
0.3/0.3/0.7. Five-second pre-dispatch pacing and the 20-second strict
deadline match. Timings exclude pacing. Baseline weather was
Helios_SkyrimClearTU at game hour 9.532; PR75 used Helios_SkyrimCloudyTU
at 17.900. Complete modlist/cache, driver, headset refresh and power
equivalence are not proven. No causal or statistically significant gain
is claimed from these ordered passes.

Memory classification is inconclusive for both builds. PR75 process-private
growth was -417.043 / -427.484 MiB, system commit -405.660 / -478.430 MiB,
and DXGI usage -869.980 / -422.961 MiB in passes 1/2. All six boundary
pressures were Normal. These negative deltas do not prove leak freedom.
Freshly reset texture trackers ended at 213 / 207 live textures. Full
start/end/cooldown values, ratios and classification inputs are retained.
Fresh resolved GPU samples are unavailable; these are switch timings,
not FPS or steady-state GPU cost.

The current physical 28,060,672-byte DLL has SHA-256
`7ec4ccdb57f42183343fd9b5fe60aab643b86ee38489309ce6ad674589a904a9`.
It matches the adjacent manifest, AIO build receipt and bound runtime
producer. The enabled profile has one loose DLL provider; Overwrite and
unmanaged Data contain no competitor. All owned captures are inactive,
the journal is flushed, and reporting and retry diagnostics are complete.

The earlier PR75 baseline timeout and stale-lock startup remain separate
preserved attempts. The user authorized archival of the dead prior
process's lock and one fresh attempt; no measured row was retried or
spliced into the completed run.

The canonical ledger retains every finalized summary and comparison
field, all 66 transitions, both passes, health gates, counters, retries,
memory, resource/profiler details, provenance and these unrounded means/SE.
Field-for-field reconstruction passed; 1,056 baseline/candidate numeric
timing cells passed audit. Historical cells remain unchanged. Raw evidence
and the full scalar export remain local. The separate
`csx-render-scale-pr-v1` release qualification is still pending.

</details>
<!-- end pr75-nvidia-mainvr-comparison-v1 -->

[Canonical comparison ledger](vr-render-scale-comparison-ledger.csv).

The detailed comparison below preserves every pass and route. The
following assay report includes the full memory table and retry diagnostics.

# Upscaling switch comparison

Change assessment: **INCONCLUSIVE**. Test execution remains **COMPLETE / COMPLETE** (baseline/candidate).

This assessment describes whether the change meets the improvement-or-neutral standard. It does not rewrite terminal results or mark a completed test as failed. PR inclusion is the user's decision.

| Identity                | Baseline                                                                                | Candidate                                                                                    |
| ----------------------- | --------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| Run                     | nvidia-20260910T124329625Z                                                              | nvidia-2026-09-10T17-12-40-813Z                                                              |
| Renderer base           | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | c615779a903d7e3f8f95c7edcf303a02ad08dced                                                     |
| Main-VR base/equivalent | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                     |
| Compiled source         | 7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99                                                | c615779a903d7e3f8f95c7edcf303a02ad08dced                                                     |
| Build ID                | 9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4                        | cbef73dfc7a11e26e01ad8ecdca03c8d45d4c6549ddfd3a51b8a0c537499014d                             |
| DLL SHA-256             | bcd04e8c31248d5cfb02148266fbc2ead73d06b36bafa59cf8f971d79df24afe                        | 7ec4ccdb57f42183343fd9b5fe60aab643b86ee38489309ce6ad674589a904a9                             |
| Evidence root           | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-20260910T124329625Z | C:\src\skyrim-community-shaders\artifacts\renderscale-tuning\nvidia-2026-09-10T17-12-40-813Z |

Assessment limits: retained_context_not_matched:scene; matching_fixture_fingerprint_unavailable; explicit_versioned_tolerance_policy_missing.

## Per-pass summary

| Lane   | Pass | Rows B/C | Mean ms B/C     | Mean delta % | Retries B/C | Fidelity B/C | Vendor failures B/C | New failure rows | Health standard B/C |
| ------ | ---- | -------- | --------------- | ------------ | ----------- | ------------ | ------------------- | ---------------- | ------------------- |
| nvidia | 1    | 33/33    | 866.009/816.198 | -5.752       | 10/10       | 4/0          | 2/0                 | none             | NOT_MET/MET         |
| nvidia | 2    | 33/33    | 833.078/804.581 | -3.421       | 10/10       | 4/0          | 2/0                 | none             | NOT_MET/MET         |

## Side-by-side relatch, completion and stretch summary

Relatch proof is dispatch to the first exact new generation proof. Strict completion includes the remaining qualification/cleanup conditions. Relatch sample counts exclude missing or inapplicable boundaries; neither is replaced with zero. Stretch totals span the full owned pass capture.

| Lane   | Pass | Metric                     | Unit        | Baseline  | Candidate | Delta     | Delta % |
| ------ | ---- | -------------------------- | ----------- | --------- | --------- | --------- | ------- |
| nvidia | 1    | Relatch proof mean         | ms          | 807.337   | 772.134   | -35.203   | -4.360  |
| nvidia | 1    | Relatch proof mean         | frames      | 13.920    | 13.920    | 0         | 0       |
| nvidia | 1    | Relatch proof total        | ms          | 20183.415 | 19303.343 | -880.072  | -4.360  |
| nvidia | 1    | Relatch proof total        | frames      | 348       | 348       | 0         | 0       |
| nvidia | 1    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 1    | Strict completion mean     | ms          | 866.009   | 816.198   | -49.810   | -5.752  |
| nvidia | 1    | Strict completion mean     | frames      | 15.576    | 15.364    | -0.212    | -1.362  |
| nvidia | 1    | Strict completion total    | ms          | 28578.281 | 26934.537 | -1643.744 | -5.752  |
| nvidia | 1    | Strict completion total    | frames      | 514       | 507       | -7        | -1.362  |
| nvidia | 1    | Stretch completed episodes | episodes    | 19        | 19        | 0         | 0       |
| nvidia | 1    | Stretch completed total    | frames      | 89        | 83        | -6        | -6.742  |
| nvidia | 1    | Stretch completed total    | ms          | 5719.962  | 5139.496  | -580.466  | -10.148 |
| nvidia | 1    | Stretch longest episode    | ms          | 646.277   | 416.204   | -230.074  | -35.600 |
| nvidia | 2    | Relatch proof mean         | ms          | 763.765   | 731.838   | -31.927   | -4.180  |
| nvidia | 2    | Relatch proof mean         | frames      | 13.840    | 13.880    | 0.040     | 0.289   |
| nvidia | 2    | Relatch proof total        | ms          | 19094.136 | 18295.960 | -798.176  | -4.180  |
| nvidia | 2    | Relatch proof total        | frames      | 346       | 347       | 1         | 0.289   |
| nvidia | 2    | Relatch proof samples      | transitions | 25        | 25        | 0         | 0       |
| nvidia | 2    | Strict completion mean     | ms          | 833.078   | 804.581   | -28.497   | -3.421  |
| nvidia | 2    | Strict completion mean     | frames      | 15.455    | 15.697    | 0.242     | 1.569   |
| nvidia | 2    | Strict completion total    | ms          | 27491.579 | 26551.180 | -940.399  | -3.421  |
| nvidia | 2    | Strict completion total    | frames      | 510       | 518       | 8         | 1.569   |
| nvidia | 2    | Stretch completed episodes | episodes    | 19        | 17        | -2        | -10.526 |
| nvidia | 2    | Stretch completed total    | frames      | 83        | 77        | -6        | -7.229  |
| nvidia | 2    | Stretch completed total    | ms          | 5383.107  | 4584.015  | -799.092  | -14.844 |
| nvidia | 2    | Stretch longest episode    | ms          | 441.441   | 389.986   | -51.455   | -11.656 |

## nvidia:1

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 688.545 / 737.870   | 49.325   | 7.164   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 169.425 / 169.350   | -0.075   | -0.045  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 249.126 / 302.164   | 53.038   | 21.290  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1004.374 / 1039.596 | 35.222   | 3.507   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1266.234 / 1232.115 | -34.120  | -2.695  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1175.702 / 1108.823 | -66.879  | -5.688  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1393.400 / 1330.702 | -62.698  | -4.500  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1352.171 / 1337.321 | -14.850  | -1.098  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1386.511 / 1254.639 | -131.872 | -9.511  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 770.294 / 751.836   | -18.459  | -2.396  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 450.117 / 459.711   | 9.594    | 2.131   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 190.626 / 180.788   | -9.838   | -5.161  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 822.942 / 755.790   | -67.153  | -8.160  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1377.140 / 1373.542 | -3.598   | -0.261  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 749.789 / 694.339   | -55.450  | -7.395  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 929.137 / 741.027   | -188.110 | -20.246 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 762.921 / 742.216   | -20.705  | -2.714  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 1400.519 / 748.952  | -651.567 | -46.523 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 778.498 / 760.402   | -18.096  | -2.325  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 731.796 / 705.867   | -25.928  | -3.543  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 572.501 / 460.578   | -111.922 | -19.550 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 172.732 / 171.103   | -1.628   | -0.943  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 261.855 / 248.704   | -13.151  | -5.022  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1233.573 / 1095.201 | -138.372 | -11.217 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 976.698 / 1534.594  | 557.896  | 57.121  | 0/1         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1536.005 / 1394.444 | -141.562 | -9.216  | 1/1         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 933.032 / 748.012   | -185.020 | -19.830 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 924.794 / 893.811   | -30.983  | -3.350  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 2184.558 / 1848.429 | -336.129 | -15.387 | 2/2         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 744.487 / 686.412   | -58.075  | -7.801  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 610.701 / 704.150   | 93.449   | 15.302  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 481.567 / 463.999   | -17.569  | -3.648  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 296.510 / 258.051   | -38.458  | -12.970 | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 466.266 / 504.202   | 688.545 / 737.870   | 222.279 / 233.667 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1234,"dispatchToBlockedOrPreparationMs":350.2632,"firstNewGenerationToCleanupDrainedMs":222.7267,"firstPhysicalMutationToFirstNewGenerationMs":112.4315,"presentationToStrictCompletionMs":222.2791}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2571,"dispatchToBlockedOrPreparationMs":385.2541,"firstNewGenerationToCleanupDrainedMs":234.2036,"firstPhysicalMutationToFirstNewGenerationMs":115.155,"presentationToStrictCompletionMs":233.6673}    |
| 2   | 169.425 / 169.350   | 169.425 / 169.350   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.4249,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":169.3495,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 3   | 249.126 / 302.164   | 249.126 / 302.164   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":249.1258,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":302.1637,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 790.942 / 828.588   | 920.929 / 957.035   | 129.987 / 128.447 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2853,"dispatchToBlockedOrPreparationMs":392.9859,"firstNewGenerationToCleanupDrainedMs":172.3976,"firstPhysicalMutationToFirstNewGenerationMs":351.2604,"presentationToStrictCompletionMs":213.4314}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9363,"dispatchToBlockedOrPreparationMs":390.4597,"firstNewGenerationToCleanupDrainedMs":171.8067,"firstPhysicalMutationToFirstNewGenerationMs":390.8327,"presentationToStrictCompletionMs":211.0073}   |
| 5   | 1014.085 / 1008.163 | 1168.706 / 1146.982 | 154.620 / 138.818 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2487,"dispatchToBlockedOrPreparationMs":400.2561,"firstNewGenerationToCleanupDrainedMs":207.9868,"firstPhysicalMutationToFirstNewGenerationMs":556.2139,"presentationToStrictCompletionMs":252.149}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1408,"dispatchToBlockedOrPreparationMs":369.5332,"firstNewGenerationToCleanupDrainedMs":187.3764,"firstPhysicalMutationToFirstNewGenerationMs":586.9312,"presentationToStrictCompletionMs":223.9512}   |
| 6   | 947.755 / 897.260   | 1089.396 / 1026.015 | 141.641 / 128.756 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9174,"dispatchToBlockedOrPreparationMs":408.0937,"firstNewGenerationToCleanupDrainedMs":184.7971,"firstPhysicalMutationToFirstNewGenerationMs":492.5879,"presentationToStrictCompletionMs":227.9465}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6782,"dispatchToBlockedOrPreparationMs":351.3709,"firstNewGenerationToCleanupDrainedMs":171.8544,"firstPhysicalMutationToFirstNewGenerationMs":499.112,"presentationToStrictCompletionMs":211.563}     |
| 7   | 1160.289 / 1115.115 | 1307.700 / 1248.672 | 147.411 / 133.557 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6713,"dispatchToBlockedOrPreparationMs":416.7351,"firstNewGenerationToCleanupDrainedMs":193.9378,"firstPhysicalMutationToFirstNewGenerationMs":692.3557,"presentationToStrictCompletionMs":233.111}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4318,"dispatchToBlockedOrPreparationMs":394.0372,"firstNewGenerationToCleanupDrainedMs":175.7851,"firstPhysicalMutationToFirstNewGenerationMs":675.4181,"presentationToStrictCompletionMs":215.5864}   |
| 8   | 1122.704 / 1112.824 | 1271.880 / 1248.528 | 149.177 / 135.704 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0394,"dispatchToBlockedOrPreparationMs":418.9637,"firstNewGenerationToCleanupDrainedMs":192.5665,"firstPhysicalMutationToFirstNewGenerationMs":656.3107,"presentationToStrictCompletionMs":229.4671}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2141,"dispatchToBlockedOrPreparationMs":395.597,"firstNewGenerationToCleanupDrainedMs":179.2205,"firstPhysicalMutationToFirstNewGenerationMs":670.4968,"presentationToStrictCompletionMs":224.4972}    |
| 9   | 1153.443 / 1037.489 | 1299.332 / 1171.499 | 145.889 / 134.010 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2105,"dispatchToBlockedOrPreparationMs":388.2587,"firstNewGenerationToCleanupDrainedMs":204.3861,"firstPhysicalMutationToFirstNewGenerationMs":701.4765,"presentationToStrictCompletionMs":233.0682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.3261,"dispatchToBlockedOrPreparationMs":355.5748,"firstNewGenerationToCleanupDrainedMs":176.3439,"firstPhysicalMutationToFirstNewGenerationMs":636.2544,"presentationToStrictCompletionMs":217.1501}   |
| 10  | 592.366 / 575.211   | 770.294 / 751.836   | 177.929 / 176.625 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9604,"dispatchToBlockedOrPreparationMs":355.7102,"firstNewGenerationToCleanupDrainedMs":229.2753,"firstPhysicalMutationToFirstNewGenerationMs":181.3485,"presentationToStrictCompletionMs":177.9286}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0838,"dispatchToBlockedOrPreparationMs":342.7072,"firstNewGenerationToCleanupDrainedMs":224.3855,"firstPhysicalMutationToFirstNewGenerationMs":181.659,"presentationToStrictCompletionMs":176.6248}    |
| 11  | 171.418 / 174.864   | 450.117 / 459.711   | 278.699 / 284.846 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8788,"dispatchToBlockedOrPreparationMs":128.0403,"firstNewGenerationToCleanupDrainedMs":280.2042,"firstPhysicalMutationToFirstNewGenerationMs":37.9934,"presentationToStrictCompletionMs":278.6991}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4013,"dispatchToBlockedOrPreparationMs":129.9625,"firstNewGenerationToCleanupDrainedMs":284.9654,"firstPhysicalMutationToFirstNewGenerationMs":41.3813,"presentationToStrictCompletionMs":284.8461}    |
| 12  | 190.626 / 180.788   | 190.626 / 180.788   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":190.626,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":180.7878,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 822.942 / 755.790   | 822.942 / 755.790   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":203.2194,"dispatchToBlockedOrPreparationMs":438.0838,"firstNewGenerationToCleanupDrainedMs":47.8224,"firstPhysicalMutationToFirstNewGenerationMs":133.8166,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":177.5634,"dispatchToBlockedOrPreparationMs":420.5629,"firstNewGenerationToCleanupDrainedMs":41.8837,"firstPhysicalMutationToFirstNewGenerationMs":115.7796,"presentationToStrictCompletionMs":0}         |
| 14  | 1240.733 / 1242.026 | 1329.442 / 1327.843 | 88.709 / 85.818   | {"blockedOrPreparationToFirstPhysicalMutationMs":272.3075,"dispatchToBlockedOrPreparationMs":430.0498,"firstNewGenerationToCleanupDrainedMs":174.1568,"firstPhysicalMutationToFirstNewGenerationMs":452.9278,"presentationToStrictCompletionMs":136.4068} | {"blockedOrPreparationToFirstPhysicalMutationMs":266.7799,"dispatchToBlockedOrPreparationMs":436.5879,"firstNewGenerationToCleanupDrainedMs":170.9521,"firstPhysicalMutationToFirstNewGenerationMs":453.5235,"presentationToStrictCompletionMs":131.5163} |
| 15  | 749.789 / 694.339   | 570.716 / 602.559   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1103,"dispatchToBlockedOrPreparationMs":364.6701,"firstNewGenerationToCleanupDrainedMs":0.2125,"firstPhysicalMutationToFirstNewGenerationMs":201.7232,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8021,"dispatchToBlockedOrPreparationMs":356.5456,"firstNewGenerationToCleanupDrainedMs":41.9193,"firstPhysicalMutationToFirstNewGenerationMs":199.2921,"presentationToStrictCompletionMs":0}           |
| 16  | 929.137 / 741.027   | 662.595 / 606.049   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2371,"dispatchToBlockedOrPreparationMs":385.7405,"firstNewGenerationToCleanupDrainedMs":50.9281,"firstPhysicalMutationToFirstNewGenerationMs":220.6896,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9922,"dispatchToBlockedOrPreparationMs":361.9381,"firstNewGenerationToCleanupDrainedMs":43.455,"firstPhysicalMutationToFirstNewGenerationMs":196.6633,"presentationToStrictCompletionMs":0}            |
| 17  | 762.921 / 742.216   | 616.503 / 603.352   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.8981,"dispatchToBlockedOrPreparationMs":362.8132,"firstNewGenerationToCleanupDrainedMs":45.3978,"firstPhysicalMutationToFirstNewGenerationMs":203.3939,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5218,"dispatchToBlockedOrPreparationMs":352.1717,"firstNewGenerationToCleanupDrainedMs":42.5529,"firstPhysicalMutationToFirstNewGenerationMs":204.1058,"presentationToStrictCompletionMs":0}           |
| 18  | 1400.519 / 748.952  | 1120.851 / 617.515  | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":269.9156,"dispatchToBlockedOrPreparationMs":430.8939,"firstNewGenerationToCleanupDrainedMs":44.2763,"firstPhysicalMutationToFirstNewGenerationMs":375.7652,"presentationToStrictCompletionMs":0}         | {"blockedOrPreparationToFirstPhysicalMutationMs":4.4239,"dispatchToBlockedOrPreparationMs":373.3099,"firstNewGenerationToCleanupDrainedMs":42.301,"firstPhysicalMutationToFirstNewGenerationMs":197.4799,"presentationToStrictCompletionMs":0}            |
| 19  | 778.498 / 760.402   | 635.134 / 629.783   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2943,"dispatchToBlockedOrPreparationMs":377.4468,"firstNewGenerationToCleanupDrainedMs":50.6356,"firstPhysicalMutationToFirstNewGenerationMs":202.7568,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.113,"dispatchToBlockedOrPreparationMs":387.3036,"firstNewGenerationToCleanupDrainedMs":40.7638,"firstPhysicalMutationToFirstNewGenerationMs":197.6027,"presentationToStrictCompletionMs":0}            |
| 20  | 546.689 / 531.149   | 731.796 / 705.867   | 185.107 / 174.719 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0066,"dispatchToBlockedOrPreparationMs":356.7277,"firstNewGenerationToCleanupDrainedMs":246.7222,"firstPhysicalMutationToFirstNewGenerationMs":123.3394,"presentationToStrictCompletionMs":185.1067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3373,"dispatchToBlockedOrPreparationMs":355.9118,"firstNewGenerationToCleanupDrainedMs":219.6812,"firstPhysicalMutationToFirstNewGenerationMs":125.9372,"presentationToStrictCompletionMs":174.7188}   |
| 21  | 254.898 / 192.487   | 572.501 / 460.578   | 317.603 / 268.091 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.7708,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":317.6031}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":127.8081,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":268.0913}             |
| 22  | 172.732 / 171.103   | 172.732 / 171.103   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.7317,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":171.1033,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 261.855 / 248.704   | 261.855 / 248.704   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":261.8554,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":248.7041,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 1042.196 / 919.602  | 1233.573 / 1095.201 | 191.377 / 175.600 | {"blockedOrPreparationToFirstPhysicalMutationMs":313.41,"dispatchToBlockedOrPreparationMs":517.0402,"firstNewGenerationToCleanupDrainedMs":241.0097,"firstPhysicalMutationToFirstNewGenerationMs":162.1131,"presentationToStrictCompletionMs":191.3771}   | {"blockedOrPreparationToFirstPhysicalMutationMs":275.3012,"dispatchToBlockedOrPreparationMs":455.4514,"firstNewGenerationToCleanupDrainedMs":218.3444,"firstPhysicalMutationToFirstNewGenerationMs":146.1044,"presentationToStrictCompletionMs":175.5995} |
| 25  | 747.831 / 1292.599  | 895.346 / 1442.459  | 147.514 / 149.861 | {"blockedOrPreparationToFirstPhysicalMutationMs":28.8488,"dispatchToBlockedOrPreparationMs":356.1955,"firstNewGenerationToCleanupDrainedMs":191.0269,"firstPhysicalMutationToFirstNewGenerationMs":319.2743,"presentationToStrictCompletionMs":228.8665}  | {"blockedOrPreparationToFirstPhysicalMutationMs":307.2203,"dispatchToBlockedOrPreparationMs":407.4189,"firstNewGenerationToCleanupDrainedMs":197.5888,"firstPhysicalMutationToFirstNewGenerationMs":530.2312,"presentationToStrictCompletionMs":241.9953} |
| 26  | 1384.499 / 1213.868 | 1484.709 / 1347.050 | 100.210 / 133.183 | {"blockedOrPreparationToFirstPhysicalMutationMs":278.0765,"dispatchToBlockedOrPreparationMs":483.9876,"firstNewGenerationToCleanupDrainedMs":196.3728,"firstPhysicalMutationToFirstNewGenerationMs":526.2725,"presentationToStrictCompletionMs":151.5059} | {"blockedOrPreparationToFirstPhysicalMutationMs":284.4247,"dispatchToBlockedOrPreparationMs":403.937,"firstNewGenerationToCleanupDrainedMs":177.9745,"firstPhysicalMutationToFirstNewGenerationMs":480.7139,"presentationToStrictCompletionMs":180.5763}  |
| 27  | 636.450 / 504.335   | 933.032 / 748.012   | 296.582 / 243.677 | {"blockedOrPreparationToFirstPhysicalMutationMs":5.1024,"dispatchToBlockedOrPreparationMs":436.9625,"firstNewGenerationToCleanupDrainedMs":297.3029,"firstPhysicalMutationToFirstNewGenerationMs":193.6644,"presentationToStrictCompletionMs":296.5822}   | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0888,"dispatchToBlockedOrPreparationMs":355.4306,"firstNewGenerationToCleanupDrainedMs":245.4977,"firstPhysicalMutationToFirstNewGenerationMs":142.9948,"presentationToStrictCompletionMs":243.6765}   |
| 28  | 748.294 / 714.349   | 879.078 / 846.833   | 130.784 / 132.484 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6136,"dispatchToBlockedOrPreparationMs":416.804,"firstNewGenerationToCleanupDrainedMs":174.1042,"firstPhysicalMutationToFirstNewGenerationMs":284.556,"presentationToStrictCompletionMs":176.5007}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.1202,"dispatchToBlockedOrPreparationMs":393.8802,"firstNewGenerationToCleanupDrainedMs":173.8338,"firstPhysicalMutationToFirstNewGenerationMs":275.9985,"presentationToStrictCompletionMs":179.4622}   |
| 29  | 1978.717 / 1621.757 | 2084.388 / 1765.597 | 105.670 / 143.840 | {"blockedOrPreparationToFirstPhysicalMutationMs":773.8797,"dispatchToBlockedOrPreparationMs":472.5051,"firstNewGenerationToCleanupDrainedMs":205.3727,"firstPhysicalMutationToFirstNewGenerationMs":632.6303,"presentationToStrictCompletionMs":205.8408} | {"blockedOrPreparationToFirstPhysicalMutationMs":637.9645,"dispatchToBlockedOrPreparationMs":422.8284,"firstNewGenerationToCleanupDrainedMs":195.3852,"firstPhysicalMutationToFirstNewGenerationMs":509.4186,"presentationToStrictCompletionMs":226.6717} |
| 30  | 560.371 / 458.620   | 744.487 / 686.412   | 184.117 / 227.792 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6993,"dispatchToBlockedOrPreparationMs":388.6597,"firstNewGenerationToCleanupDrainedMs":238.0266,"firstPhysicalMutationToFirstNewGenerationMs":114.1016,"presentationToStrictCompletionMs":184.1165}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0536,"dispatchToBlockedOrPreparationMs":341.0891,"firstNewGenerationToCleanupDrainedMs":228.0342,"firstPhysicalMutationToFirstNewGenerationMs":114.2349,"presentationToStrictCompletionMs":227.7921}   |
| 31  | 610.701 / 704.150   | 610.701 / 704.150   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.1454,"dispatchToBlockedOrPreparationMs":419.9648,"firstNewGenerationToCleanupDrainedMs":47.1311,"firstPhysicalMutationToFirstNewGenerationMs":92.4599,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":52.9457,"dispatchToBlockedOrPreparationMs":410.1055,"firstNewGenerationToCleanupDrainedMs":43.1683,"firstPhysicalMutationToFirstNewGenerationMs":197.9305,"presentationToStrictCompletionMs":0}          |
| 32  | 197.556 / 192.739   | 481.567 / 463.999   | 284.011 / 271.260 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":130.0718,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":284.0112}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":129.0303,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":271.2596}             |
| 33  | 296.510 / 258.051   | 296.510 / 258.051   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":296.5095,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":258.0512,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 9 / 11                   | 2            | 465.818 / 503.666    | 37.848   | 14 / 16           | 2            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 3             | -1           |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 5             | 1            |
| 4   | 12 / 13                  | 1            | 748.532 / 785.229    | 36.697   | 17 / 18           | 1            |
| 5   | 14 / 12                  | -2           | 960.719 / 959.605    | -1.114   | 19 / 17           | -2           |
| 6   | 17 / 16                  | -1           | 904.599 / 854.161    | -50.438  | 22 / 21           | -1           |
| 7   | 16 / 18                  | 2            | 1113.762 / 1072.887  | -40.875  | 21 / 23           | 2            |
| 8   | 17 / 18                  | 1            | 1079.314 / 1069.308  | -10.006  | 22 / 23           | 1            |
| 9   | 16 / 16                  | 0            | 1094.946 / 995.155   | -99.790  | 21 / 21           | 0            |
| 10  | 9 / 9                    | 0            | 541.019 / 527.450    | -13.569  | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 169.912 / 174.745    | 4.833    | 10 / 9            | -1           |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 775.120 / 713.906    | -61.214  | 10 / 10           | 0            |
| 14  | 23 / 23                  | 0            | 1155.285 / 1156.891  | 1.606    | 28 / 28           | 0            |
| 15  | 12 / 12                  | 0            | 570.504 / 560.640    | -9.864   | 16 / 15           | -1           |
| 16  | 12 / 12                  | 0            | 611.667 / 562.594    | -49.074  | 18 / 16           | -2           |
| 17  | 12 / 12                  | 0            | 571.105 / 560.799    | -10.306  | 16 / 16           | 0            |
| 18  | 22 / 12                  | -10          | 1076.575 / 575.214   | -501.361 | 29 / 16           | -13          |
| 19  | 12 / 12                  | 0            | 584.498 / 589.019    | 4.521    | 16 / 16           | 0            |
| 20  | 10 / 10                  | 0            | 485.074 / 486.186    | 1.113    | 16 / 15           | -1           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 10 / 11           | 1            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 16 / 15                  | -1           | 992.563 / 876.857    | -115.706 | 21 / 20           | -1           |
| 25  | 13 / 23                  | 10           | 704.319 / 1244.870   | 540.552  | 18 / 28           | 10           |
| 26  | 24 / 22                  | -2           | 1288.337 / 1169.076  | -119.261 | 29 / 27           | -2           |
| 27  | 10 / 10                  | 0            | 635.729 / 502.514    | -133.215 | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 704.974 / 672.999    | -31.975  | 16 / 16           | 0            |
| 29  | 29 / 29                  | 0            | 1879.015 / 1570.211  | -308.804 | 34 / 34           | 0            |
| 30  | 11 / 10                  | -1           | 506.461 / 458.378    | -48.083  | 16 / 16           | 0            |
| 31  | 9 / 10                   | 1            | 563.570 / 660.982    | 97.412   | 11 / 11           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 1 / 1                | 0     | 1 / 1              | 0     | 116.243 / 118.764  | 2.522    |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 226.448 / 237.279  | 10.831   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 225.499 / 227.249  | 1.750    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 378.865 / 383.477  | 4.611    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 379.410 / 383.075  | 3.665    |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 375.234 / 388.058  | 12.825   |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 422.382 / 382.998  | -39.385  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 293.625 / 282.735  | -10.891  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 143.915 / 144.766  | 0.850    |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 156.516 / 142.321  | -14.195  |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 152.521 / 151.116  | -1.405   |
| 18  | 1 / 1                | 0     | 13 / 3             | -10   | 646.277 / 145.514  | -500.764 |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 151.172 / 143.269  | -7.903   |
| 20  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 207.881 / 416.204  | 208.323  |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 629.135 / 603.126  | -26.009  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 3                | 0     | 16 / 16            | 0     | 1214.838 / 989.547 | -225.291 |
| 30  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 31  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 32  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 33  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |

## nvidia:2

B/C cells are baseline/candidate; times are ms excluding the pre-dispatch wait. F/V is fidelity mismatch observations / vendor-failure eye observations.

| Row | Switch                      | Strict B/C          | Delta ms | Delta % | Retries B/C | F/V B -> C | Pair    |
| --- | --------------------------- | ------------------- | -------- | ------- | ----------- | ---------- | ------- |
| 1   | DLSS Hoshipa -> NONE        | 716.918 / 752.175   | 35.257   | 4.918   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 2   | NONE -> TAA                 | 170.160 / 186.179   | 16.019   | 9.414   | 0/0         | 0/0 -> 0/0 | MATCHED |
| 3   | TAA -> DLAA                 | 286.338 / 266.961   | -19.377  | -6.767  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 4   | DLAA -> DLSS Hoshipa        | 1032.563 / 948.929  | -83.635  | -8.100  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 5   | DLSS Hoshipa -> DLSS UQ     | 1027.220 / 985.326  | -41.894  | -4.078  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 6   | DLSS UQ -> DLSS Q           | 1169.543 / 1150.617 | -18.927  | -1.618  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 7   | DLSS Q -> DLSS Bal          | 1390.699 / 1158.350 | -232.348 | -16.707 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 8   | DLSS Bal -> DLSS Perf       | 1195.918 / 1152.575 | -43.344  | -3.624  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 9   | DLSS Perf -> DLSS UP        | 1249.407 / 1104.947 | -144.459 | -11.562 | 1/1         | 0/0 -> 0/0 | MATCHED |
| 10  | DLSS UP -> DLAA             | 914.287 / 820.141   | -94.146  | -10.297 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 11  | DLAA -> TAA                 | 480.252 / 430.311   | -49.942  | -10.399 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 12  | TAA -> NONE                 | 172.881 / 172.146   | -0.735   | -0.425  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 13  | NONE -> FSR AA              | 619.982 / 587.880   | -32.102  | -5.178  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 14  | FSR AA -> FSR Hoshipa       | 1408.406 / 1312.764 | -95.642  | -6.791  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 15  | FSR Hoshipa -> FSR UQ       | 945.154 / 1391.161  | 446.006  | 47.189  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 16  | FSR UQ -> FSR Q             | 789.230 / 781.311   | -7.920   | -1.003  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 17  | FSR Q -> FSR Bal            | 831.971 / 728.992   | -102.980 | -12.378 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 18  | FSR Bal -> FSR Perf         | 757.557 / 728.398   | -29.159  | -3.849  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 19  | FSR Perf -> FSR UP          | 758.886 / 727.459   | -31.427  | -4.141  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 20  | FSR UP -> FSR AA            | 1053.683 / 747.601  | -306.082 | -29.049 | 1/0         | 0/0 -> 0/0 | MATCHED |
| 21  | FSR AA -> TAA               | 495.001 / 447.482   | -47.519  | -9.600  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 22  | TAA -> NONE                 | 217.047 / 168.778   | -48.269  | -22.239 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 23  | NONE -> DLAA                | 254.963 / 302.104   | 47.141   | 18.489  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 24  | DLAA -> FSR AA              | 1118.329 / 1081.859 | -36.470  | -3.261  | 1/1         | 0/0 -> 0/0 | MATCHED |
| 25  | FSR AA -> DLSS Hoshipa      | 1028.504 / 1844.264 | 815.760  | 79.315  | 0/2         | 0/0 -> 0/0 | MATCHED |
| 26  | DLSS Hoshipa -> FSR Hoshipa | 1514.576 / 1394.624 | -119.952 | -7.920  | 1/1         | 2/1 -> 0/0 | MATCHED |
| 27  | FSR Hoshipa -> NONE         | 774.192 / 728.402   | -45.790  | -5.915  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 28  | NONE -> FSR UP              | 939.204 / 920.225   | -18.979  | -2.021  | 0/0         | 2/1 -> 0/0 | MATCHED |
| 29  | FSR UP -> DLSS UP           | 1918.325 / 1446.191 | -472.134 | -24.612 | 2/1         | 0/0 -> 0/0 | MATCHED |
| 30  | DLSS UP -> TAA              | 827.143 / 705.395   | -121.749 | -14.719 | 0/0         | 0/0 -> 0/0 | MATCHED |
| 31  | TAA -> FSR AA               | 628.123 / 589.605   | -38.518  | -6.132  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 32  | FSR AA -> NONE              | 531.443 / 501.027   | -30.416  | -5.723  | 0/0         | 0/0 -> 0/0 | MATCHED |
| 33  | NONE -> DLAA                | 273.671 / 287.002   | 13.331   | 4.871   | 0/0         | 0/0 -> 0/0 | MATCHED |

| Row | Presentation B/C    | Cleanup B/C         | Cleanup tail B/C  | Phase durations B                                                                                                                                                                                                                                         | Phase durations C                                                                                                                                                                                                                                         |
| --- | ------------------- | ------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 485.143 / 528.025   | 716.918 / 752.175   | 231.775 / 224.150 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2651,"dispatchToBlockedOrPreparationMs":366.8156,"firstNewGenerationToCleanupDrainedMs":231.9285,"firstPhysicalMutationToFirstNewGenerationMs":114.9089,"presentationToStrictCompletionMs":231.775}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0171,"dispatchToBlockedOrPreparationMs":394.494,"firstNewGenerationToCleanupDrainedMs":225.0262,"firstPhysicalMutationToFirstNewGenerationMs":129.6375,"presentationToStrictCompletionMs":224.1498}    |
| 2   | 170.160 / 186.179   | 170.160 / 186.179   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":170.1598,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":186.179,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                     |
| 3   | 286.338 / 266.961   | 286.338 / 266.961   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":286.3376,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":266.9607,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 4   | 804.481 / 733.028   | 948.804 / 863.686   | 144.323 / 130.657 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.1201,"dispatchToBlockedOrPreparationMs":428.4029,"firstNewGenerationToCleanupDrainedMs":187.6301,"firstPhysicalMutationToFirstNewGenerationMs":328.6511,"presentationToStrictCompletionMs":228.0825}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0353,"dispatchToBlockedOrPreparationMs":366.3901,"firstNewGenerationToCleanupDrainedMs":173.1527,"firstPhysicalMutationToFirstNewGenerationMs":321.1075,"presentationToStrictCompletionMs":215.9006}   |
| 5   | 805.305 / 771.941   | 939.029 / 900.685   | 133.724 / 128.744 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4788,"dispatchToBlockedOrPreparationMs":433.4416,"firstNewGenerationToCleanupDrainedMs":177.506,"firstPhysicalMutationToFirstNewGenerationMs":324.6027,"presentationToStrictCompletionMs":221.9152}    | {"blockedOrPreparationToFirstPhysicalMutationMs":4.5081,"dispatchToBlockedOrPreparationMs":393.9712,"firstNewGenerationToCleanupDrainedMs":171.901,"firstPhysicalMutationToFirstNewGenerationMs":330.3049,"presentationToStrictCompletionMs":213.3851}    |
| 6   | 953.781 / 935.862   | 1087.266 / 1069.127 | 133.485 / 133.265 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.9058,"dispatchToBlockedOrPreparationMs":399.6181,"firstNewGenerationToCleanupDrainedMs":177.5245,"firstPhysicalMutationToFirstNewGenerationMs":506.2179,"presentationToStrictCompletionMs":215.7622}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.402,"dispatchToBlockedOrPreparationMs":392.8358,"firstNewGenerationToCleanupDrainedMs":176.3898,"firstPhysicalMutationToFirstNewGenerationMs":496.4992,"presentationToStrictCompletionMs":214.7546}    |
| 7   | 1093.288 / 938.661  | 1251.222 / 1074.163 | 157.935 / 135.502 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2632,"dispatchToBlockedOrPreparationMs":463.7802,"firstNewGenerationToCleanupDrainedMs":206.6991,"firstPhysicalMutationToFirstNewGenerationMs":576.4798,"presentationToStrictCompletionMs":297.411}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.43,"dispatchToBlockedOrPreparationMs":389.6013,"firstNewGenerationToCleanupDrainedMs":179.6871,"firstPhysicalMutationToFirstNewGenerationMs":501.4443,"presentationToStrictCompletionMs":219.6894}     |
| 8   | 968.840 / 925.830   | 1111.087 / 1070.032 | 142.246 / 144.202 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0262,"dispatchToBlockedOrPreparationMs":401.594,"firstNewGenerationToCleanupDrainedMs":187.1834,"firstPhysicalMutationToFirstNewGenerationMs":518.283,"presentationToStrictCompletionMs":227.0782}     | {"blockedOrPreparationToFirstPhysicalMutationMs":3.5518,"dispatchToBlockedOrPreparationMs":392.5166,"firstNewGenerationToCleanupDrainedMs":187.0107,"firstPhysicalMutationToFirstNewGenerationMs":486.9532,"presentationToStrictCompletionMs":226.7447}   |
| 9   | 971.186 / 896.889   | 1146.171 / 1025.732 | 174.985 / 128.843 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2317,"dispatchToBlockedOrPreparationMs":361.625,"firstNewGenerationToCleanupDrainedMs":228.7108,"firstPhysicalMutationToFirstNewGenerationMs":551.6035,"presentationToStrictCompletionMs":278.2205}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.2917,"dispatchToBlockedOrPreparationMs":375.484,"firstNewGenerationToCleanupDrainedMs":171.8837,"firstPhysicalMutationToFirstNewGenerationMs":475.0726,"presentationToStrictCompletionMs":208.0582}    |
| 10  | 692.329 / 628.633   | 914.287 / 820.141   | 221.958 / 191.508 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6875,"dispatchToBlockedOrPreparationMs":406.104,"firstNewGenerationToCleanupDrainedMs":285.7356,"firstPhysicalMutationToFirstNewGenerationMs":217.76,"presentationToStrictCompletionMs":221.9585}      | {"blockedOrPreparationToFirstPhysicalMutationMs":3.4867,"dispatchToBlockedOrPreparationMs":368.3691,"firstNewGenerationToCleanupDrainedMs":244.7901,"firstPhysicalMutationToFirstNewGenerationMs":203.4954,"presentationToStrictCompletionMs":191.5085}   |
| 11  | 200.500 / 169.660   | 480.252 / 430.311   | 279.753 / 260.651 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.6232,"dispatchToBlockedOrPreparationMs":153.3929,"firstNewGenerationToCleanupDrainedMs":281.5936,"firstPhysicalMutationToFirstNewGenerationMs":40.6427,"presentationToStrictCompletionMs":279.7528}    | {"blockedOrPreparationToFirstPhysicalMutationMs":3.054,"dispatchToBlockedOrPreparationMs":127.2449,"firstNewGenerationToCleanupDrainedMs":261.9149,"firstPhysicalMutationToFirstNewGenerationMs":38.0969,"presentationToStrictCompletionMs":260.6506}     |
| 12  | 172.881 / 172.146   | 172.881 / 172.146   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.8809,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":172.1458,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 13  | 619.982 / 587.880   | 619.982 / 587.880   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":51.4789,"dispatchToBlockedOrPreparationMs":434.7645,"firstNewGenerationToCleanupDrainedMs":42.5815,"firstPhysicalMutationToFirstNewGenerationMs":91.1569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":52.2071,"dispatchToBlockedOrPreparationMs":400.7651,"firstNewGenerationToCleanupDrainedMs":42.2113,"firstPhysicalMutationToFirstNewGenerationMs":92.6966,"presentationToStrictCompletionMs":0}           |
| 14  | 1273.888 / 1312.764 | 1361.411 / 1268.192 | 87.523 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":290.4089,"dispatchToBlockedOrPreparationMs":406.8997,"firstNewGenerationToCleanupDrainedMs":175.9205,"firstPhysicalMutationToFirstNewGenerationMs":488.1822,"presentationToStrictCompletionMs":134.5184} | {"blockedOrPreparationToFirstPhysicalMutationMs":264.1089,"dispatchToBlockedOrPreparationMs":413.7851,"firstNewGenerationToCleanupDrainedMs":166.8453,"firstPhysicalMutationToFirstNewGenerationMs":423.4527,"presentationToStrictCompletionMs":0}        |
| 15  | 945.154 / 1391.161  | 634.137 / 716.003   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.0545,"dispatchToBlockedOrPreparationMs":364.9358,"firstNewGenerationToCleanupDrainedMs":46.2897,"firstPhysicalMutationToFirstNewGenerationMs":217.8569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2031,"dispatchToBlockedOrPreparationMs":404.9561,"firstNewGenerationToCleanupDrainedMs":49.0496,"firstPhysicalMutationToFirstNewGenerationMs":257.7943,"presentationToStrictCompletionMs":0}           |
| 16  | 789.230 / 781.311   | 608.752 / 605.425   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2824,"dispatchToBlockedOrPreparationMs":358.6511,"firstNewGenerationToCleanupDrainedMs":44.2401,"firstPhysicalMutationToFirstNewGenerationMs":200.5787,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3446,"dispatchToBlockedOrPreparationMs":357.2443,"firstNewGenerationToCleanupDrainedMs":43.8944,"firstPhysicalMutationToFirstNewGenerationMs":199.9415,"presentationToStrictCompletionMs":0}           |
| 17  | 831.971 / 728.992   | 680.701 / 590.849   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.905,"dispatchToBlockedOrPreparationMs":402.5673,"firstNewGenerationToCleanupDrainedMs":48.1971,"firstPhysicalMutationToFirstNewGenerationMs":224.0314,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2638,"dispatchToBlockedOrPreparationMs":350.6589,"firstNewGenerationToCleanupDrainedMs":42.3749,"firstPhysicalMutationToFirstNewGenerationMs":193.5515,"presentationToStrictCompletionMs":0}           |
| 18  | 757.557 / 728.398   | 620.771 / 599.703   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":5.2012,"dispatchToBlockedOrPreparationMs":366.6054,"firstNewGenerationToCleanupDrainedMs":43.9078,"firstPhysicalMutationToFirstNewGenerationMs":205.0569,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0115,"dispatchToBlockedOrPreparationMs":363.5315,"firstNewGenerationToCleanupDrainedMs":41.2919,"firstPhysicalMutationToFirstNewGenerationMs":190.8684,"presentationToStrictCompletionMs":0}           |
| 19  | 758.886 / 727.459   | 624.111 / 598.952   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":4.2709,"dispatchToBlockedOrPreparationMs":375.4259,"firstNewGenerationToCleanupDrainedMs":42.8573,"firstPhysicalMutationToFirstNewGenerationMs":201.5567,"presentationToStrictCompletionMs":0}           | {"blockedOrPreparationToFirstPhysicalMutationMs":3.809,"dispatchToBlockedOrPreparationMs":366.5857,"firstNewGenerationToCleanupDrainedMs":40.6977,"firstPhysicalMutationToFirstNewGenerationMs":187.8597,"presentationToStrictCompletionMs":0}            |
| 20  | 807.760 / 572.177   | 1053.683 / 747.601  | 245.923 / 175.424 | {"blockedOrPreparationToFirstPhysicalMutationMs":268.8283,"dispatchToBlockedOrPreparationMs":412.9697,"firstNewGenerationToCleanupDrainedMs":246.2078,"firstPhysicalMutationToFirstNewGenerationMs":125.6768,"presentationToStrictCompletionMs":245.9226} | {"blockedOrPreparationToFirstPhysicalMutationMs":4.0449,"dispatchToBlockedOrPreparationMs":398.0614,"firstNewGenerationToCleanupDrainedMs":222.886,"firstPhysicalMutationToFirstNewGenerationMs":122.6084,"presentationToStrictCompletionMs":175.4241}    |
| 21  | 222.836 / 185.652   | 495.001 / 447.482   | 272.166 / 261.830 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":154.7564,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":272.1656}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":123.2604,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":261.8296}             |
| 22  | 217.047 / 168.778   | 217.047 / 168.778   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":217.0474,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":168.7782,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 23  | 254.963 / 302.104   | 254.963 / 302.104   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":254.9632,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":302.1045,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |
| 24  | 935.684 / 909.383   | 1118.329 / 1081.859 | 182.645 / 172.476 | {"blockedOrPreparationToFirstPhysicalMutationMs":277.6547,"dispatchToBlockedOrPreparationMs":458.7517,"firstNewGenerationToCleanupDrainedMs":229.2147,"firstPhysicalMutationToFirstNewGenerationMs":152.7076,"presentationToStrictCompletionMs":182.645}  | {"blockedOrPreparationToFirstPhysicalMutationMs":270.7427,"dispatchToBlockedOrPreparationMs":450.7856,"firstNewGenerationToCleanupDrainedMs":214.152,"firstPhysicalMutationToFirstNewGenerationMs":146.1786,"presentationToStrictCompletionMs":172.4764}  |
| 25  | 804.197 / 1628.967  | 943.123 / 1760.977  | 138.926 / 132.010 | {"blockedOrPreparationToFirstPhysicalMutationMs":27.795,"dispatchToBlockedOrPreparationMs":405.4911,"firstNewGenerationToCleanupDrainedMs":182.8934,"firstPhysicalMutationToFirstNewGenerationMs":326.9437,"presentationToStrictCompletionMs":224.3067}   | {"blockedOrPreparationToFirstPhysicalMutationMs":662.9519,"dispatchToBlockedOrPreparationMs":445.3204,"firstNewGenerationToCleanupDrainedMs":173.9595,"firstPhysicalMutationToFirstNewGenerationMs":478.7455,"presentationToStrictCompletionMs":215.297}  |
| 26  | 1514.576 / 1252.176 | 1459.934 / 1337.152 | 0 / 84.975        | {"blockedOrPreparationToFirstPhysicalMutationMs":275.8095,"dispatchToBlockedOrPreparationMs":439.8071,"firstNewGenerationToCleanupDrainedMs":227.9014,"firstPhysicalMutationToFirstNewGenerationMs":516.4164,"presentationToStrictCompletionMs":0}        | {"blockedOrPreparationToFirstPhysicalMutationMs":268.2121,"dispatchToBlockedOrPreparationMs":420.9249,"firstNewGenerationToCleanupDrainedMs":176.3453,"firstPhysicalMutationToFirstNewGenerationMs":471.6695,"presentationToStrictCompletionMs":142.4476} |
| 27  | 527.127 / 502.496   | 774.192 / 728.402   | 247.065 / 225.906 | {"blockedOrPreparationToFirstPhysicalMutationMs":3.8776,"dispatchToBlockedOrPreparationMs":372.2902,"firstNewGenerationToCleanupDrainedMs":248.1193,"firstPhysicalMutationToFirstNewGenerationMs":149.9049,"presentationToStrictCompletionMs":247.0652}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.46,"dispatchToBlockedOrPreparationMs":359.7919,"firstNewGenerationToCleanupDrainedMs":226.292,"firstPhysicalMutationToFirstNewGenerationMs":138.8577,"presentationToStrictCompletionMs":225.9057}      |
| 28  | 798.736 / 920.225   | 887.620 / 874.443   | 88.884 / 0        | {"blockedOrPreparationToFirstPhysicalMutationMs":3.6003,"dispatchToBlockedOrPreparationMs":413.0077,"firstNewGenerationToCleanupDrainedMs":176.1734,"firstPhysicalMutationToFirstNewGenerationMs":294.8381,"presentationToStrictCompletionMs":140.4682}   | {"blockedOrPreparationToFirstPhysicalMutationMs":2.9464,"dispatchToBlockedOrPreparationMs":397.7672,"firstNewGenerationToCleanupDrainedMs":187.5792,"firstPhysicalMutationToFirstNewGenerationMs":286.1505,"presentationToStrictCompletionMs":0}          |
| 29  | 1701.862 / 1222.146 | 1838.119 / 1356.637 | 136.257 / 134.491 | {"blockedOrPreparationToFirstPhysicalMutationMs":721.0348,"dispatchToBlockedOrPreparationMs":432.8313,"firstNewGenerationToCleanupDrainedMs":178.7958,"firstPhysicalMutationToFirstNewGenerationMs":505.4568,"presentationToStrictCompletionMs":216.4628} | {"blockedOrPreparationToFirstPhysicalMutationMs":24.8193,"dispatchToBlockedOrPreparationMs":682.1023,"firstNewGenerationToCleanupDrainedMs":177.2814,"firstPhysicalMutationToFirstNewGenerationMs":472.434,"presentationToStrictCompletionMs":224.0452}   |
| 30  | 588.638 / 530.348   | 827.143 / 705.395   | 238.505 / 175.047 | {"blockedOrPreparationToFirstPhysicalMutationMs":4.3175,"dispatchToBlockedOrPreparationMs":459.0829,"firstNewGenerationToCleanupDrainedMs":239.1926,"firstPhysicalMutationToFirstNewGenerationMs":124.5504,"presentationToStrictCompletionMs":238.5052}   | {"blockedOrPreparationToFirstPhysicalMutationMs":3.0201,"dispatchToBlockedOrPreparationMs":373.4736,"firstNewGenerationToCleanupDrainedMs":221.3625,"firstPhysicalMutationToFirstNewGenerationMs":107.5385,"presentationToStrictCompletionMs":175.0465}   |
| 31  | 628.123 / 589.605   | 628.123 / 589.605   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":52.1465,"dispatchToBlockedOrPreparationMs":435.4388,"firstNewGenerationToCleanupDrainedMs":44.028,"firstPhysicalMutationToFirstNewGenerationMs":96.5097,"presentationToStrictCompletionMs":0}            | {"blockedOrPreparationToFirstPhysicalMutationMs":56.3853,"dispatchToBlockedOrPreparationMs":401.8184,"firstNewGenerationToCleanupDrainedMs":41.187,"firstPhysicalMutationToFirstNewGenerationMs":90.2146,"presentationToStrictCompletionMs":0}            |
| 32  | 210.815 / 206.188   | 531.443 / 501.027   | 320.628 / 294.839 | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":138.7934,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":320.6276}             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":136.7737,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":294.8389}             |
| 33  | 273.671 / 287.002   | 273.671 / 287.002   | 0 / 0             | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":273.6711,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    | {"blockedOrPreparationToFirstPhysicalMutationMs":null,"dispatchToBlockedOrPreparationMs":287.0022,"firstNewGenerationToCleanupDrainedMs":null,"firstPhysicalMutationToFirstNewGenerationMs":null,"presentationToStrictCompletionMs":0}                    |

| Row | Relatch proof frames B/C | Delta frames | Relatch proof ms B/C | Delta ms | Strict frames B/C | Delta frames |
| --- | ------------------------ | ------------ | -------------------- | -------- | ----------------- | ------------ |
| 1   | 10 / 10                  | 0            | 484.990 / 527.149    | 42.159   | 15 / 15           | 0            |
| 2   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 3   | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 3             | 0            |
| 4   | 14 / 12                  | -2           | 761.174 / 690.533    | -70.641  | 19 / 17           | -2           |
| 5   | 13 / 13                  | 0            | 761.523 / 728.784    | -32.739  | 18 / 18           | 0            |
| 6   | 17 / 17                  | 0            | 909.742 / 892.737    | -17.005  | 22 / 22           | 0            |
| 7   | 18 / 16                  | -2           | 1044.523 / 894.476   | -150.048 | 23 / 21           | -2           |
| 8   | 17 / 17                  | 0            | 923.903 / 883.022    | -40.882  | 22 / 22           | 0            |
| 9   | 16 / 17                  | 1            | 917.460 / 853.848    | -63.612  | 21 / 22           | 1            |
| 10  | 10 / 9                   | -1           | 628.552 / 575.351    | -53.200  | 15 / 14           | -1           |
| 11  | 3 / 3                    | 0            | 198.659 / 168.396    | -30.263  | 9 / 10            | 1            |
| 12  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 13  | 9 / 9                    | 0            | 577.400 / 545.669    | -31.731  | 10 / 11           | 1            |
| 14  | 23 / 22                  | -1           | 1185.491 / 1101.347  | -84.144  | 28 / 27           | -1           |
| 15  | 12 / 12                  | 0            | 587.847 / 666.953    | 79.106   | 20 / 26           | 6            |
| 16  | 12 / 12                  | 0            | 564.512 / 561.530    | -2.982   | 17 / 17           | 0            |
| 17  | 12 / 12                  | 0            | 632.504 / 548.474    | -84.029  | 16 / 16           | 0            |
| 18  | 12 / 12                  | 0            | 576.864 / 558.411    | -18.452  | 16 / 16           | 0            |
| 19  | 12 / 12                  | 0            | 581.254 / 558.254    | -22.999  | 16 / 16           | 0            |
| 20  | 16 / 11                  | -5           | 807.475 / 524.715    | -282.760 | 21 / 16           | -5           |
| 21  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 11           | 0            |
| 22  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |
| 23  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 3 / 4             | 1            |
| 24  | 15 / 15                  | 0            | 889.114 / 867.707    | -21.407  | 20 / 20           | 0            |
| 25  | 13 / 29                  | 16           | 760.230 / 1587.018   | 826.788  | 18 / 34           | 16           |
| 26  | 23 / 23                  | 0            | 1232.033 / 1160.806  | -71.226  | 28 / 28           | 0            |
| 27  | 10 / 10                  | 0            | 526.073 / 502.110    | -23.963  | 16 / 16           | 0            |
| 28  | 11 / 11                  | 0            | 711.446 / 686.864    | -24.582  | 16 / 16           | 0            |
| 29  | 29 / 23                  | -6           | 1659.323 / 1179.356  | -479.967 | 34 / 28           | -6           |
| 30  | 10 / 11                  | 1            | 587.951 / 484.032    | -103.919 | 16 / 16           | 0            |
| 31  | 9 / 9                    | 0            | 584.095 / 548.418    | -35.677  | 10 / 10           | 0            |
| 32  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 11 / 10           | -1           |
| 33  | n/a / n/a                | n/a          | n/a / n/a            | n/a      | 4 / 4             | 0            |

| Row | Stretch episodes B/C | Delta | Stretch frames B/C | Delta | Stretch ms B/C     | Delta ms |
| --- | -------------------- | ----- | ------------------ | ----- | ------------------ | -------- |
| 1   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 2   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 3   | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 4   | 1 / 1                | 0     | 2 / 2              | 0     | 213.825 / 208.117  | -5.708   |
| 5   | 1 / 1                | 0     | 2 / 2              | 0     | 210.089 / 210.511  | 0.422    |
| 6   | 1 / 1                | 0     | 6 / 6              | 0     | 380.853 / 387.741  | 6.888    |
| 7   | 1 / 1                | 0     | 6 / 6              | 0     | 441.441 / 389.986  | -51.455  |
| 8   | 1 / 1                | 0     | 6 / 6              | 0     | 401.860 / 379.590  | -22.270  |
| 9   | 1 / 1                | 0     | 6 / 6              | 0     | 429.952 / 366.803  | -63.149  |
| 10  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 11  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 12  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 13  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 14  | 1 / 1                | 0     | 6 / 6              | 0     | 308.404 / 269.150  | -39.253  |
| 15  | 1 / 1                | 0     | 3 / 3              | 0     | 158.148 / 193.216  | 35.069   |
| 16  | 1 / 1                | 0     | 3 / 3              | 0     | 143.759 / 145.365  | 1.607    |
| 17  | 1 / 1                | 0     | 3 / 3              | 0     | 165.514 / 142.350  | -23.164  |
| 18  | 1 / 1                | 0     | 3 / 3              | 0     | 155.220 / 140.585  | -14.635  |
| 19  | 1 / 1                | 0     | 3 / 3              | 0     | 149.943 / 136.219  | -13.724  |
| 20  | 1 / 0                | -1    | 5 / 0              | -5    | 341.305 / 0        | -341.305 |
| 21  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 22  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 23  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 24  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 25  | 1 / 1                | 0     | 2 / 6              | 4     | 210.712 / 370.264  | 159.552  |
| 26  | 2 / 2                | 0     | 11 / 11            | 0     | 617.137 / 580.710  | -36.427  |
| 27  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 28  | 0 / 0                | 0     | 0 / 0              | 0     | 0 / 0              | 0        |
| 29  | 3 / 2                | -1    | 16 / 11            | -5    | 1054.946 / 663.406 | -391.540 |
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

## Side-by-side memory boundaries

Memory classification is retained independently. Negative deltas do not prove leak freedom. Fresh tracker counts are not process-wide net allocation counts.

| Lane   | Pass | Metric            | B start/end/change                             | C start/end/change                             | Change difference C-B |
| ------ | ---- | ----------------- | ---------------------------------------------- | ---------------------------------------------- | --------------------- |
| nvidia | 1    | processPrivateMiB | 16894.5078125 / 16681.24609375 / -213.26171875 | 17067.8359375 / 16650.79296875 / -417.04296875 | -203.781              |
| nvidia | 1    | systemCommitMiB   | 55422.94921875 / 55060.37890625 / -362.5703125 | 54108.640625 / 53702.98046875 / -405.66015625  | -43.090               |
| nvidia | 1    | dxgiUsageMiB      | 5583.1171875 / 3750.015625 / -1833.1015625     | 4420.1171875 / 3550.13671875 / -869.98046875   | 963.121               |
| nvidia | 1    | liveTextures      | 0 / 261 / 261                                  | 0 / 213 / 213                                  | -48                   |
| nvidia | 1    | liveTextureMiB    | 0 / 2429.8961448669434 / 2429.8961448669434    | 0 / 2270.9314918518066 / 2270.9314918518066    | -158.965              |
| nvidia | 2    | processPrivateMiB | 17097.22265625 / 16617.34765625 / -479.875     | 17079.37890625 / 16651.89453125 / -427.484375  | 52.391                |
| nvidia | 2    | systemCommitMiB   | 55505.6015625 / 54815.92578125 / -689.67578125 | 54286.1328125 / 53807.703125 / -478.4296875    | 211.246               |
| nvidia | 2    | dxgiUsageMiB      | 4066.75390625 / 3643.64453125 / -423.109375    | 3922.69921875 / 3499.73828125 / -422.9609375   | 0.148                 |
| nvidia | 2    | liveTextures      | 0 / 211 / 211                                  | 0 / 207 / 207                                  | -4                    |
| nvidia | 2    | liveTextureMiB    | 0 / 2274.9311332702637 / 2274.9311332702637    | 0 / 2265.5977058410645 / 2265.5977058410645    | -9.333                |

## Side-by-side CPU/GPU/resource observations

Counters span different observed frame counts and changing methods. They are not whole-frame timings. Unresolved profiler totals are n/a; fresh resolved sample qualification is separate.

<details><summary>nvidia pass 1: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2168        | 2233        | 65         |
| cpu/compactPresentationContract/reuses                  | 2146        | 2211        | 65         |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 71          | 1          |
| cpu/generationResourceValidation/contractPublishes      | 155         | 153         | -2         |
| cpu/generationResourceValidation/fullValidations        | 581         | 576         | -5         |
| cpu/generationResourceValidation/stableChecks           | 8236        | 8444        | 208        |
| cpu/generationResourceValidation/stableHits             | 8159        | 8367        | 208        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 1           | 1           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4149        | 4308        | 159        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4149        | 4308        | 159        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4107        | 4266        | 159        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4149        | 4308        | 159        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4128        | 4287        | 159        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 35          | 1          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4115        | 4273        | 158        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4054        | 4215        | 161        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 94          | 94          | 0          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4134        | 4293        | 159        |
| cpu/strongStereoPacket/captures                         | 4492        | 4636        | 144        |
| cpu/strongStereoPacket/commitAccepts                    | 4317        | 4446        | 129        |
| cpu/strongStereoPacket/commitRejects                    | 65          | 66          | 1          |
| cpu/strongStereoPacket/commitValidations                | 4382        | 4512        | 130        |
| cpu/strongStereoPacket/cycleReuses                      | 2205        | 2277        | 72         |
| cpu/strongStereoPacket/fastSkips                        | 3806        | 3980        | 174        |
| cpu/strongStereoPacket/invalidations                    | 4706        | 4854        | 148        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 103         | -1         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2183        | 2256        | 73         |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.867       | 1.866       | -0.000     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 22.700      | 15.900      | -6.800     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.138       | 0.005      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.600       | 1.300       | -0.300     |
| cpu/window/currentFrame                                 | 53015       | 72183       | 19168      |
| cpu/window/elapsedFrames                                | 4148        | 4309        | 161        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 48867       | 67874       | 19007      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 53017       | 72184       | 19167      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5974703856  | 6185322000  | 210618144  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4709        | 4875        | 166        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11961613440 | 12383280000 | 421666560  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.297       | 0.306       | 0.009      |
| gpu/item5ActiveFSRCopies/activePixels                   | 11119322840 | 12031551120 | 912228280  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26297233960 | 27264724080 | 967490120  |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14730       | 15470       | 740        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1971        | 2043        | 72         |
| gpu/item7EarlyHAM/executedClears                        | 1974        | 2044        | 70         |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1974        | 2044        | 70         |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4104        | 4246        | 142        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4150        | 4310        | 160        |
| gpu/startFrame                                          | 48867       | 67874       | 19007      |
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
| texture/createdCount                                    | 3962        | 3922        | -40        |
| texture/createdEstimatedBytes                           | 38815224912 | 38661165120 | -154059792 |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3701        | 3709        | 8          |
| texture/destroyedEstimatedBytes                         | 36267294132 | 36279920860 | 12626728   |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 886         | 873         | -13        |
| texture/liveTextureRecordCount                          | 261         | 213         | -48        |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 50          | 2           | -48        |
| texture/niSourceTextureMatchedEstimatedBytes            | 172279000   | 5592480     | -166686520 |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1536        | 1503        | -33        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 261         | 213         | -48        |
| texture/outstandingEstimatedBytes                       | 2547930780  | 2381244260  | -166686520 |
| texture/outstandingUnknownEstimateCount                 | 0           | 0           | 0          |
| texture/recordingFailures                               | 0           | 0           | 0          |
| texture/sentinelAllocationFailures                      | 0           | 0           | 0          |
| texture/sessionID                                       | 1           | 1           | 0          |
| texture/supported                                       | true        | true        | n/a        |

</details>

<details><summary>nvidia pass 2: all captured scalar counters</summary>

| Metric (captured units)                                 | Baseline    | Candidate   | Delta      |
| ------------------------------------------------------- | ----------- | ----------- | ---------- |
| cpu/active                                              | false       | false       | n/a        |
| cpu/compactPresentationContract/publishes               | 2143        | 2254        | 111        |
| cpu/compactPresentationContract/reuses                  | 2121        | 2232        | 111        |
| cpu/devBenchOnly                                        | true        | true        | n/a        |
| cpu/generationResourceValidation/contractInvalidations  | 70          | 70          | 0          |
| cpu/generationResourceValidation/contractPublishes      | 156         | 155         | -1         |
| cpu/generationResourceValidation/fullValidations        | 570         | 584         | 14         |
| cpu/generationResourceValidation/stableChecks           | 8160        | 8541        | 381        |
| cpu/generationResourceValidation/stableHits             | 8083        | 8464        | 381        |
| cpu/generationResourceValidation/stableMisses           | 77          | 77          | 0          |
| cpu/schemaVersion                                       | 1           | 1           | 0          |
| cpu/sessionId                                           | 2           | 2           | 0          |
| cpu/stateProportionalSafety/boundsGuard/candidates      | 0           | 0           | 0          |
| cpu/stateProportionalSafety/boundsGuard/fastSkips       | 4106        | 4357        | 251        |
| cpu/stateProportionalSafety/deferredRecovery/candidates | 0           | 0           | 0          |
| cpu/stateProportionalSafety/deferredRecovery/fastSkips  | 4106        | 4357        | 251        |
| cpu/stateProportionalSafety/engineRetirement/fastSkips  | 4064        | 4315        | 251        |
| cpu/stateProportionalSafety/engineRetirement/services   | 42          | 42          | 0          |
| cpu/stateProportionalSafety/memoryTelemetry/candidates  | 4106        | 4357        | 251        |
| cpu/stateProportionalSafety/memoryTelemetry/fastSkips   | 0           | 0           | 0          |
| cpu/stateProportionalSafety/memoryTrim/fastSkips        | 4085        | 4336        | 251        |
| cpu/stateProportionalSafety/memoryTrim/services         | 21          | 21          | 0          |
| cpu/stateProportionalSafety/nativeGuard/candidates      | 34          | 34          | 0          |
| cpu/stateProportionalSafety/nativeGuard/fastSkips       | 4072        | 4323        | 251        |
| cpu/stateProportionalSafety/postMutationGuard/fastSkips | 4017        | 4263        | 246        |
| cpu/stateProportionalSafety/postMutationGuard/services  | 90          | 94          | 4          |
| cpu/stateProportionalSafety/promotion/candidates        | 15          | 15          | 0          |
| cpu/stateProportionalSafety/promotion/fastSkips         | 4091        | 4342        | 251        |
| cpu/strongStereoPacket/captures                         | 4444        | 4690        | 246        |
| cpu/strongStereoPacket/commitAccepts                    | 4266        | 4490        | 224        |
| cpu/strongStereoPacket/commitRejects                    | 66          | 64          | -2         |
| cpu/strongStereoPacket/commitValidations                | 4332        | 4554        | 222        |
| cpu/strongStereoPacket/cycleReuses                      | 2180        | 2305        | 125        |
| cpu/strongStereoPacket/fastSkips                        | 3768        | 4024        | 256        |
| cpu/strongStereoPacket/invalidations                    | 4653        | 4915        | 262        |
| cpu/strongStereoPacket/lifetimeRebuilds                 | 104         | 103         | -1         |
| cpu/strongStereoPacket/lifetimeReuses                   | 2160        | 2282        | 122        |
| cpu/strongStereoPacket/queueHoldAverageMicroseconds     | 1.869       | 1.856       | -0.013     |
| cpu/strongStereoPacket/queueHoldMaximumMicroseconds     | 36.700      | 67.200      | 30.500     |
| cpu/strongStereoPacket/queueWaitAverageMicroseconds     | 0.133       | 0.141       | 0.008      |
| cpu/strongStereoPacket/queueWaitMaximumMicroseconds     | 1.500       | 1           | -0.500     |
| cpu/window/currentFrame                                 | 57516       | 76941       | 19425      |
| cpu/window/elapsedFrames                                | 4107        | 4357        | 250        |
| cpu/window/initialized                                  | true        | true        | n/a        |
| cpu/window/startFrame                                   | 53409       | 72584       | 19175      |
| gpu/active                                              | false       | false       | n/a        |
| gpu/currentFrame                                        | 57516       | 76941       | 19425      |
| gpu/item10PeripheryTAAHistory/avoidedPixelRatio         | 0.501       | 0.501       | 0          |
| gpu/item10PeripheryTAAHistory/croppedPixels             | 5901114384  | 6274136880  | 373022496  |
| gpu/item10PeripheryTAAHistory/dispatches                | 4651        | 4945        | 294        |
| gpu/item10PeripheryTAAHistory/fullEyePixels             | 11814284160 | 12561091200 | 746807040  |
| gpu/item10PeripheryTAAHistory/pixelRatio                | 0.499       | 0.499       | 0          |
| gpu/item5ActiveFSRCopies/activePixelRatio               | 0.307       | 0.306       | -0.000     |
| gpu/item5ActiveFSRCopies/activePixels                   | 11542473520 | 12289195480 | 746721960  |
| gpu/item5ActiveFSRCopies/avoidedPixels                  | 26077296080 | 27819930920 | 1742634840 |
| gpu/item5ActiveFSRCopies/copyCalls                      | 14810       | 15790       | 980        |
| gpu/item6RuntimeFSRStereo/batchAttempts                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchFailures                 | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchNotHandled               | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchReuses                   | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/batchSuccesses                | 0           | 0           | 0          |
| gpu/item6RuntimeFSRStereo/interopTransactionsAvoided    | 0           | 0           | 0          |
| gpu/item7EarlyHAM/directOutputSkips                     | 1955        | 2089        | 134        |
| gpu/item7EarlyHAM/executedClears                        | 1948        | 2052        | 104        |
| gpu/item7EarlyHAM/protectedPostProcessInputs            | 1948        | 2052        | 104        |
| gpu/item8MirrorWriteback/blitPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/consumerEyeObservations        | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/copyPairs                      | 0           | 0           | 0          |
| gpu/item8MirrorWriteback/skippedEyeObservations         | 4064        | 4302        | 238        |
| gpu/item9SpatialComposite/centerRectPixels              | 0           | 0           | 0          |
| gpu/item9SpatialComposite/dispatches                    | 0           | 0           | 0          |
| gpu/item9SpatialComposite/fullEyePixels                 | 0           | 0           | 0          |
| gpu/observedFrames                                      | 4107        | 4357        | 250        |
| gpu/startFrame                                          | 53409       | 72584       | 19175      |
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
| texture/createdCount                                    | 3914        | 3898        | -16        |
| texture/createdEstimatedBytes                           | 38675267808 | 38610535792 | -64732016  |
| texture/currentCohort                                   | 0           | 0           | 0          |
| texture/destroyedCount                                  | 3703        | 3691        | -12        |
| texture/destroyedEstimatedBytes                         | 36289829620 | 36234884412 | -54945208  |
| texture/droppedTextureRecords                           | 0           | 0           | 0          |
| texture/faceGenAssignmentFailures                       | 0           | 0           | 0          |
| texture/faceGenTintAssignmentCount                      | 0           | 0           | 0          |
| texture/groupCount                                      | 863         | 861         | -2         |
| texture/liveTextureRecordCount                          | 211         | 207         | -4         |
| texture/maxTrackedLiveTextures                          | 16384       | 16384       | 0          |
| texture/maxTrackedTextureGroups                         | 4096        | 4096        | 0          |
| texture/niSourceTextureMatchedCount                     | 4           | 0           | -4         |
| texture/niSourceTextureMatchedEstimatedBytes            | 9786808     | 0           | -9786808   |
| texture/niSourceTextureOwnerRecordsDropped              | 0           | 0           | 0          |
| texture/niSourceTextureResourceCount                    | 1509        | 1487        | -22        |
| texture/niSourceTextureTraversalLimitReached            | false       | false       | n/a        |
| texture/outstandingCount                                | 211         | 207         | -4         |
| texture/outstandingEstimatedBytes                       | 2385438188  | 2375651380  | -9786808   |
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
| toolchain                | true   |
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
-   Presentation stretch: **31 selected, 31 recovered PASS, 0 unrecovered**

Task 2 is deliberately not aggregated. Reporting failure does not rewrite the render result, and a render pass does not hide missing per-transition evidence. Every raw JSON value is available in `evidence-values.csv`.

## Per-pass switch health and performance

Terminal PASS means the waiter reached its terminal condition. Full-history health, cumulative acceptance, evidence completeness, and change assessment remain separate. Recovered failures stay visible and do not relabel a completed test.

| Lane   | Pass | Rows | Terminal PASS/FAIL | Strict mean ms | p95 ms   | Max ms   | Mean strict frames | Mean relatch ms | Mean relatch frames | Stretch episodes | Stretch frames | Stretch ms | Retries | Fidelity | Vendor-failure eyes | Health standard | Evidence |
| ------ | ---- | ---- | ------------------ | -------------- | -------- | -------- | ------------------ | --------------- | ------------------- | ---------------- | -------------- | ---------- | ------- | -------- | ------------------- | --------------- | -------- |
| nvidia | 1    | 33   | 33/0               | 816.198        | 1450.504 | 1848.429 | 15.364             | 772.134         | 13.920              | 19               | 83             | 5139.496   | 10      | 0        | 0                   | MET             | COMPLETE |
| nvidia | 2    | 33   | 33/0               | 804.581        | 1415.251 | 1844.264 | 15.697             | 731.838         | 13.880              | 17               | 77             | 4584.015   | 10      | 0        | 0                   | MET             | COMPLETE |

### nvidia pass 1

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":71783,"leftPath":"NativeOriginal","referenceFrame":72181,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

### nvidia pass 2

Cumulative accepted: **false**. Evidence gaps: none.

| Raw unmet gate                   | Observed                                                                                                                                          | Limit                                                                                 | Assessment role   | Reason                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------- |
| presentation_stretch_frame_bound | {"activeFrames":0,"activeFramesAtStop":0,"maximumCompletedFrames":6,"maximumObservedFrames":6}                                                    | {"maximumFrames":2}                                                                   | DIAGNOSTIC_ONLY   | Fixed stretch cutoff is inapplicable to imposed settling; compare measured frames and duration. |
| presentation_recovered           | {"lastBothEyesVendorFrame":76532,"leftPath":"NativeOriginal","referenceFrame":76940,"rightPath":"NativeOriginal","stableVendorPresentation":true} | {"bothEyes":true,"maximumAgeFrames":2,"path":"VendorEvaluated","stableContract":true} | CONTRACT_MISMATCH | Scaled-presentation gate conflicts with the proven native terminal target.                      |

| Row | Recovered | Nonzero failure observations | Receipt |
| --- | --------- | ---------------------------- | ------- |

All counters, gate observations and limits, owned metrics, phase timings, CPU/GPU workload, memory, resource and retry evidence remain in summary.json. Profiler totals without resolved fresh samples cannot establish GPU cost or FPS.

## Memory confirmation

### nvidia

Memory outcome: **inconclusive** (inconclusive); completed passes: 2/2; cooldown: 10000 ms.

| Metric                             | Pass 1 start | Pass 1 end | Pass 1 delta | Cooldown start | Cooldown end | Cooldown delta | Pass 2 start | Pass 2 end | Pass 2 delta | Pass 2 / pass 1 growth |
| ---------------------------------- | -----------: | ---------: | -----------: | -------------: | -----------: | -------------: | -----------: | ---------: | -----------: | ---------------------: |
| Process private MiB                |    17067.836 |  16650.793 |     -417.043 |      16991.227 |    16994.063 |          2.836 |    17079.379 |  16651.895 |     -427.484 |                   n.d. |
| System commit MiB                  |    54108.641 |   53702.98 |      -405.66 |      54058.602 |    54129.871 |          71.27 |    54286.133 |  53807.703 |      -478.43 |                   n.d. |
| DXGI process usage MiB             |     4420.117 |   3550.137 |      -869.98 |       3807.699 |     3808.578 |          0.879 |     3922.699 |   3499.738 |     -422.961 |                   n.d. |
| Memory pressure                    |       Normal |     Normal |         n.d. |         Normal |       Normal |           n.d. |       Normal |     Normal |         n.d. |                   n.d. |
| Live tracked textures              |            0 |        213 |          213 |            213 |          213 |              0 |            0 |        207 |          207 |                  0.972 |
| Estimated live tracked texture MiB |            0 |   2270.931 |     2270.931 |       2270.931 |     2270.931 |              0 |            0 |   2265.598 |     2265.598 |                  0.998 |

Predicate inputs (unrounded):

```json
{
    "available": true,
    "reason": null,
    "pass1": {
        "processPrivateMiB": -417.04296875,
        "systemCommitMiB": -405.66015625,
        "dxgiUsageMiB": -869.98046875,
        "liveTextures": 213,
        "liveTextureMiB": 2270.9314918518066
    },
    "pass2": {
        "processPrivateMiB": -427.484375,
        "systemCommitMiB": -478.4296875,
        "dxgiUsageMiB": -422.9609375,
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

## Transitions

Retry waits end at the first observed preparation-ready result. Ready-to-candidate includes stereo qualification and the settling guard; it is not isolated retry overhead. Overlapping viewport waits are not added. Unavailable results are n.d. and the affected reporting outcome is n/a. All retrieved values remain in raw evidence and evidence-values.csv; reporting provenance never aborts or replays measurements.

| Lane   | Pass | Row | Retry telemetry      | Retries | Retry reasons                  | Viewport wait                                            | Ready-to-candidate             | Actual backend | Render | Stability | Task 2 | Trace evidence       | Stretch frames | Stretch recovery   | Recovery   | Authority | Reported violations | Missing evidence | Invalid producer evidence |
| ------ | ---: | --: | -------------------- | ------- | ------------------------------ | -------------------------------------------------------- | ------------------------------ | -------------- | ------ | --------- | ------ | -------------------- | -------------- | ------------------ | ---------- | --------- | ------------------- | ---------------- | ------------------------- |
| nvidia |    1 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | 1              | 67885/504.2025 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   4 | available (complete) | 0       | complete                       | none observed                                            | 130.860 ms                     | dlss           | PASS   | stable    | PASS   | 57 records / 2 pages | 2              | 68263/828.5883 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   5 | available (complete) | 0       | complete                       | none observed                                            | 121.962 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 68398/1008.1634 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 48.740 ms; SubmitStageFoveatedCenter: 48.497 ms | 220.559 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 68537/897.2599 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 55.712 ms; SubmitStageFoveatedCenter: 55.694 ms | 220.939 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 68677/1115.1154 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.773 ms; SubmitStageFoveatedCenter: 1.721 ms   | 270.356 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 68817/1112.8239 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 52.775 ms; SubmitStageFoveatedCenter: 52.724 ms | 215.347 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 68957/1037.4894 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 6              | 69620/1242.0256 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 69754/694.3394 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 69888/741.0274 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 70021/742.2161 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 70155/748.9525 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 70288/760.4015 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  25 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 295.480 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 71083/1292.5987 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 71228/1213.8676 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  29 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 280.471 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 71650/1621.7571 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    1 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   1 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   2 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   3 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 8 records / 1 pages  | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   4 | available (complete) | 0       | complete                       | none observed                                            | 107.916 ms                     | dlss           | PASS   | stable    | PASS   | 61 records / 2 pages | 2              | 72974/733.0283 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   5 | available (complete) | 0       | complete                       | none observed                                            | 104.648 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 2              | 73109/771.9409 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   6 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 51.670 ms; SubmitStageFoveatedCenter: 51.428 ms | 232.351 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 73250/935.8621 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   7 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 55.937 ms; SubmitStageFoveatedCenter: 55.911 ms | 220.139 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 73386/938.661 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   8 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 54.791 ms; SubmitStageFoveatedCenter: 54.664 ms | 219.275 ms                     | dlss           | PASS   | stable    | PASS   | 69 records / 3 pages | 6              | 73526/925.8302 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |   9 | available (complete) | 1       | dlss_viewport_recycle          | FullEye: 1.656 ms; SubmitStageFoveatedCenter: 1.601 ms   | 261.254 ms                     | dlss           | PASS   | stable    | PASS   | 65 records / 3 pages | 6              | 73665/896.8892 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  10 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 64 records / 2 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  11 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  12 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  13 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  14 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 6              | 74336/1312.7639 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  15 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 74481/1391.1609 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  16 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 74616/781.3105 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  17 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 74749/728.9919 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  18 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 74883/728.3982 ms  | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  19 | available (complete) | 0       | complete                       | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | 3              | 75018/727.459 ms   | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  20 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  21 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  22 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  23 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  24 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  25 | available (complete) | 2       | render_target_relatch_requeued | none observed                                            | 267.656 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | 6              | 75827/1628.9671 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  26 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | n.d. (no viewport observation) | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | not_exposed    | 75976/1252.1764 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  27 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  28 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  29 | available (complete) | 1       | render_target_relatch_requeued | none observed                                            | 260.906 ms                     | dlss           | PASS   | stable    | PASS   | 25 records / 1 pages | not_exposed    | 76397/1222.1459 ms | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  30 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  31 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | fsr_runtime    | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  32 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | none           | PASS   | stable    | PASS   | not_applicable       | none           | none               | not_needed | MATCHED   | none                | none             | none                      |
| nvidia |    2 |  33 | available (complete) | 0       | complete                       | none observed                                            | n.d.                           | dlss           | PASS   | stable    | PASS   | 12 records / 1 pages | none           | none               | not_needed | MATCHED   | none                | none             | none                      |

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
| nvidia |    2 |  18 | {"method":"fsr","qualityMode":4,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":5,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  19 | {"method":"fsr","qualityMode":5,"renderScaleMode":true}   | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":6,"renderScaleMode":true} |                  3 | yes            |
| nvidia |    2 |  25 | {"method":"fsr","qualityMode":0,"renderScaleMode":false}  | {"dlssProfile":"K","method":"dlss","qualityMode":1,"renderScaleMode":true}  |                  6 | yes            |
| nvidia |    2 |  26 | {"method":"dlss","qualityMode":1,"renderScaleMode":true}  | {"fsrRuntime":"fsr3","method":"fsr","qualityMode":1,"renderScaleMode":true} |        not_exposed | yes            |
| nvidia |    2 |  29 | {"method":"fsr","qualityMode":6,"renderScaleMode":true}   | {"dlssProfile":"K","method":"dlss","qualityMode":6,"renderScaleMode":true}  |        not_exposed | yes            |

## Validation evidence

-   Retained clean Release AIO build receipt for `c615779a9`: configuration,
    build/package, archive integrity and payload/provenance verification
    passed. DevBench bridge and universal SE/AE/VR support were enabled.
    The receipt retains CMake deprecation/unused-option warnings and an
    existing CommonLib hde64 C4701 warning; this was not a warning-free build.
-   Physical deployment reverified for this assay against its manifest,
    AIO receipt, SHA-256, size, enabled profile and runtime Build ID.
-   `renderscale-tuning nvidia`: two uninterrupted 33-transition passes in
    Skyrim VR, 66 terminal PASS; Task 2 counts 66/0/0; applicable health MET
    in both passes; reporting COMPLETE. Exact producer identities above.
-   Packaged offline finalizer and repository
    `tools/compare-render-scale-ledger.py`: complete evidence extraction,
    deployment and comparison validation; 1,056 numeric timing cells audited.
    Complete summary/comparison/statistical reconstruction and historical-cell
    preservation passed.
-   The source review and written `FSREyeDispatch` coverage described above
    remain retained. Controller tests, shader tests and SE/AE runtime tests
    were not run for this measurement task; the build receipt also records
    project tests and hooks as not run by prior user instruction.
-   Scoped `pwsh ./tools/pre-commit.ps1 run --files` for the three documentation files passed after formatting; `pwsh ./tools/git.ps1 diff --cached --check` passed.
-   Separate generated `csx-render-scale-pr-v1` qualification: pending.
    This tuning assay does not substitute for that protocol. Draft state is
    unchanged.

Local full-summary/paired-comparison reconstruction and timing audit:
`artifacts/renderscale-tuning/nvidia-2026-09-10T17-12-40-813Z/complete-ledger-validation.json`.

## Paired candidate passes by transition group

Each row retains its original pass and route. Strict, presentation and
cleanup-tail times are milliseconds; relatch is dispatch to first new
generation proof. Missing relatch is inapplicable, not zero. Detailed
creator/mutation, native restoration and stale-provider proofs remain in
the full comparison and the complete ledger detail cells.

### NVIDIA DLSS and DLAA destinations

| Row | Route                              | P1 / P2 render | P1 / P2 Task 2 | P1 / P2 strict ms   | P1 / P2 presentation ms | P1 / P2 cleanup tail ms | P1 / P2 relatch ms                        |
| --- | ---------------------------------- | -------------- | -------------- | ------------------- | ----------------------- | ----------------------- | ----------------------------------------- |
| 3   | TAA â†’ DLAA                       | PASS / PASS    | PASS / PASS    | 302.164 / 266.961   | 302.164 / 266.961       | 0.000 / 0.000           | n/a; not applicable / n/a; not applicable |
| 4   | DLAA â†’ DLSS Hoshipa              | PASS / PASS    | PASS / PASS    | 1039.596 / 948.929  | 828.588 / 733.028       | 128.447 / 130.657       | 785.229 / 690.533                         |
| 5   | DLSS Hoshipa â†’ DLSS UQ           | PASS / PASS    | PASS / PASS    | 1232.115 / 985.326  | 1008.163 / 771.941      | 138.818 / 128.744       | 959.605 / 728.784                         |
| 6   | DLSS UQ â†’ DLSS Quality           | PASS / PASS    | PASS / PASS    | 1108.823 / 1150.617 | 897.260 / 935.862       | 128.756 / 133.265       | 854.161 / 892.737                         |
| 7   | DLSS Quality â†’ DLSS Balanced     | PASS / PASS    | PASS / PASS    | 1330.702 / 1158.350 | 1115.115 / 938.661      | 133.557 / 135.502       | 1072.887 / 894.476                        |
| 8   | DLSS Balanced â†’ DLSS Performance | PASS / PASS    | PASS / PASS    | 1337.321 / 1152.575 | 1112.824 / 925.830      | 135.704 / 144.202       | 1069.308 / 883.022                        |
| 9   | DLSS Performance â†’ DLSS UP       | PASS / PASS    | PASS / PASS    | 1254.639 / 1104.947 | 1037.489 / 896.889      | 134.010 / 128.843       | 995.155 / 853.848                         |
| 10  | DLSS UP â†’ DLAA                   | PASS / PASS    | PASS / PASS    | 751.836 / 820.141   | 575.211 / 628.633       | 176.625 / 191.508       | 527.450 / 575.351                         |
| 23  | NONE â†’ DLAA                      | PASS / PASS    | PASS / PASS    | 248.704 / 302.104   | 248.704 / 302.104       | 0.000 / 0.000           | n/a; not applicable / n/a; not applicable |
| 25  | FSR3 Native AA â†’ DLSS Hoshipa    | PASS / PASS    | PASS / PASS    | 1534.594 / 1844.264 | 1292.599 / 1628.967     | 149.861 / 132.010       | 1244.870 / 1587.018                       |
| 29  | FSR3 UP â†’ DLSS UP                | PASS / PASS    | PASS / PASS    | 1848.429 / 1446.191 | 1621.757 / 1222.146     | 143.840 / 134.491       | 1570.211 / 1179.356                       |
| 33  | NONE â†’ DLAA                      | PASS / PASS    | PASS / PASS    | 258.051 / 287.002   | 258.051 / 287.002       | 0.000 / 0.000           | n/a; not applicable / n/a; not applicable |

### NVIDIA FSR3 destinations

| Row | Route                              | P1 / P2 render | P1 / P2 Task 2 | P1 / P2 strict ms   | P1 / P2 presentation ms | P1 / P2 cleanup tail ms | P1 / P2 relatch ms  |
| --- | ---------------------------------- | -------------- | -------------- | ------------------- | ----------------------- | ----------------------- | ------------------- |
| 13  | NONE â†’ FSR3 Native AA            | PASS / PASS    | PASS / PASS    | 755.790 / 587.880   | 755.790 / 587.880       | 0.000 / 0.000           | 713.906 / 545.669   |
| 14  | FSR3 Native AA â†’ FSR3 Hoshipa    | PASS / PASS    | PASS / PASS    | 1373.542 / 1312.764 | 1242.026 / 1312.764     | 85.818 / 0.000          | 1156.891 / 1101.347 |
| 15  | FSR3 Hoshipa â†’ FSR3 UQ           | PASS / PASS    | PASS / PASS    | 694.339 / 1391.161  | 694.339 / 1391.161      | 0.000 / 0.000           | 560.640 / 666.953   |
| 16  | FSR3 UQ â†’ FSR3 Quality           | PASS / PASS    | PASS / PASS    | 741.027 / 781.311   | 741.027 / 781.311       | 0.000 / 0.000           | 562.594 / 561.530   |
| 17  | FSR3 Quality â†’ FSR3 Balanced     | PASS / PASS    | PASS / PASS    | 742.216 / 728.992   | 742.216 / 728.992       | 0.000 / 0.000           | 560.799 / 548.474   |
| 18  | FSR3 Balanced â†’ FSR3 Performance | PASS / PASS    | PASS / PASS    | 748.952 / 728.398   | 748.952 / 728.398       | 0.000 / 0.000           | 575.214 / 558.411   |
| 19  | FSR3 Performance â†’ FSR3 UP       | PASS / PASS    | PASS / PASS    | 760.402 / 727.459   | 760.402 / 727.459       | 0.000 / 0.000           | 589.019 / 558.254   |
| 20  | FSR3 UP â†’ FSR3 Native AA         | PASS / PASS    | PASS / PASS    | 705.867 / 747.601   | 531.149 / 572.177       | 174.719 / 175.424       | 486.186 / 524.715   |
| 24  | DLAA â†’ FSR3 Native AA            | PASS / PASS    | PASS / PASS    | 1095.201 / 1081.859 | 919.602 / 909.383       | 175.600 / 172.476       | 876.857 / 867.707   |
| 26  | DLSS Hoshipa â†’ FSR3 Hoshipa      | PASS / PASS    | PASS / PASS    | 1394.444 / 1394.624 | 1213.868 / 1252.176     | 133.183 / 84.975        | 1169.076 / 1160.806 |
| 28  | NONE â†’ FSR3 UP                   | PASS / PASS    | PASS / PASS    | 893.811 / 920.225   | 714.349 / 920.225       | 132.484 / 0.000         | 672.999 / 686.864   |
| 31  | TAA â†’ FSR3 Native AA             | PASS / PASS    | PASS / PASS    | 704.150 / 589.605   | 704.150 / 589.605       | 0.000 / 0.000           | 660.982 / 548.418   |

### NVIDIA provider crossings

| Row | Route                           | P1 / P2 render | P1 / P2 Task 2 | P1 / P2 strict ms   | P1 / P2 presentation ms | P1 / P2 cleanup tail ms | P1 / P2 relatch ms  |
| --- | ------------------------------- | -------------- | -------------- | ------------------- | ----------------------- | ----------------------- | ------------------- |
| 24  | DLAA â†’ FSR3 Native AA         | PASS / PASS    | PASS / PASS    | 1095.201 / 1081.859 | 919.602 / 909.383       | 175.600 / 172.476       | 876.857 / 867.707   |
| 25  | FSR3 Native AA â†’ DLSS Hoshipa | PASS / PASS    | PASS / PASS    | 1534.594 / 1844.264 | 1292.599 / 1628.967     | 149.861 / 132.010       | 1244.870 / 1587.018 |
| 26  | DLSS Hoshipa â†’ FSR3 Hoshipa   | PASS / PASS    | PASS / PASS    | 1394.444 / 1394.624 | 1213.868 / 1252.176     | 133.183 / 84.975        | 1169.076 / 1160.806 |
| 29  | FSR3 UP â†’ DLSS UP             | PASS / PASS    | PASS / PASS    | 1848.429 / 1446.191 | 1621.757 / 1222.146     | 143.840 / 134.491       | 1570.211 / 1179.356 |

### NVIDIA TAA and None destinations

| Row | Route                   | P1 / P2 render | P1 / P2 Task 2 | P1 / P2 strict ms | P1 / P2 presentation ms | P1 / P2 cleanup tail ms | P1 / P2 relatch ms                        |
| --- | ----------------------- | -------------- | -------------- | ----------------- | ----------------------- | ----------------------- | ----------------------------------------- |
| 1   | DLSS Hoshipa â†’ NONE   | PASS / PASS    | PASS / PASS    | 737.870 / 752.175 | 504.202 / 528.025       | 233.667 / 224.150       | 503.666 / 527.149                         |
| 2   | NONE â†’ TAA            | PASS / PASS    | PASS / PASS    | 169.350 / 186.179 | 169.350 / 186.179       | 0.000 / 0.000           | n/a; not applicable / n/a; not applicable |
| 11  | DLAA â†’ TAA            | PASS / PASS    | PASS / PASS    | 459.711 / 430.311 | 174.864 / 169.660       | 284.846 / 260.651       | 174.745 / 168.396                         |
| 12  | TAA â†’ NONE            | PASS / PASS    | PASS / PASS    | 180.788 / 172.146 | 180.788 / 172.146       | 0.000 / 0.000           | n/a; not applicable / n/a; not applicable |
| 21  | FSR3 Native AA â†’ TAA  | PASS / PASS    | PASS / PASS    | 460.578 / 447.482 | 192.487 / 185.652       | 268.091 / 261.830       | n/a; not applicable / n/a; not applicable |
| 22  | TAA â†’ NONE            | PASS / PASS    | PASS / PASS    | 171.103 / 168.778 | 171.103 / 168.778       | 0.000 / 0.000           | n/a; not applicable / n/a; not applicable |
| 27  | FSR3 Hoshipa â†’ NONE   | PASS / PASS    | PASS / PASS    | 748.012 / 728.402 | 504.335 / 502.496       | 243.677 / 225.906       | 502.514 / 502.110                         |
| 30  | DLSS UP â†’ TAA         | PASS / PASS    | PASS / PASS    | 686.412 / 705.395 | 458.620 / 530.348       | 227.792 / 175.047       | 458.378 / 484.032                         |
| 32  | FSR3 Native AA â†’ NONE | PASS / PASS    | PASS / PASS    | 463.999 / 501.027 | 192.739 / 206.188       | 271.260 / 294.839       | n/a; not applicable / n/a; not applicable |
