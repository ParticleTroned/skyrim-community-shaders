# NVIDIA render-scale comparison: old renderer versus measured main-VR head

Both tests completed. The measured main-VR head has lower average
transition latency in both passes, but recurring recovered fidelity/vendor
failures mean **DOES_NOT_MEET_STANDARD** for improvement-or-neutral.
This assessment does not relabel the completed test as failed. Original
terminal results remain unchanged. The imposed stretch cutoff is diagnostic
only; compare actual stretch frames and duration. Raw cumulative acceptance
and the proven native-target gate mismatch are retained separately.

**B** means the old renderer based on `1595cffd`, compiled as `390fdea25`
with the preparation bridge backport. **H** means the earlier main-VR
run compiled from `348803c18`. Every delta is **H minus B**; negative
latency is faster for H. Each pass is kept separate.

## Exact source and build provenance

| Identity                           | B: old baseline                                                  | H: measured main-VR head                                         |
| ---------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------- |
| Renderer source/base               | 1595cffd6a0a770020074c3d5481d45502df58a7                         | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                         |
| Equivalent main-VR renderer commit | 9074b4676aa708ae32c26295dfd69cc8696d5efa                         | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                         |
| Actual compiled source             | 390fdea2533ae447849c5f07766c796872996a61                         | 348803c1831c8cd71ceb75a58143ad0bcb00abb2                         |
| Source describe                    | v3.19.0-pr28-266-g390fdea25                                      | v3.19.0-pr28-274-g348803c18                                      |
| Build ID                           | d20962d1f8b75609f7b97074d1ae8132450d61b477be793227ad88088f5bda0d | 07238b1fe36024d2c33081f023973c34887ce8a0fa4bd4743bf8a1c85b5ec78d |
| DLL SHA-256                        | e0c3405ab0de5ee5717b542136634684b3ce3140a672f227230d54d377f13ba5 | 2101a4f7faa417b415ebb28dfa7b93a4ef39abf32ab9879f549f25da09bbef38 |
| DLL bytes                          | 27750912                                                         | 27829760                                                         |
| Run ID                             | renderscale-tuning-nvidia-20260910T090649592Z                    | nvidia-2026-09-10T07-19-50-405Z                                  |
| Game PID                           | 26500                                                            | 16424                                                            |
| Configuration / source dirty       | Release / false                                                  | Release / false                                                  |

`1595cffd` is a review-branch commit, not the main-VR squash commit.
Its runtime sources and packaged renderer assets are identical to main-VR
`9074b4676aa708ae32c26295dfd69cc8696d5efa`; their remaining differences
are cache installer validation, tests, and documentation. The derived
`390fdea25` commit has `1595cffd` as its direct parent. Its only runtime
source edits are `MenuDevBenchBridge.cpp` and `MenuDevBenchPreflightPolicy.h`,
backporting `prepare_tuning` from `348803c18`. The audit retained 821
renderer/asset files and 57 matching reporting-source files.

This compares the cumulative renderer changes between those baselines,
including SSS changes and stereo-submit/input-freshness work. It does
not isolate one intervening commit. The runtime payloads match for 347
files; differing files are the DLL, PDB, build manifest, and
`Shaders/Wetterness/PuddleMask.hlsli`. The later working-tree head
`4eabbc4bf68e327863f202fd66de70745fc99d3e` is **not** a measured build
in this comparison.

Both runs retain successful physical-DLL, adjacent-manifest, AIO-build
receipt, and archive verification. This offline comparison verifies
those retained identities and source hashes; it does not replace the
currently loaded DLL or infer historical state from current MO2 state.

## Comparable conditions and limits

| Condition                               | B                                     | H                                     | Assessment                                                                                   |
| --------------------------------------- | ------------------------------------- | ------------------------------------- | -------------------------------------------------------------------------------------------- |
| GPU                                     | NVIDIA GeForce RTX 5070 Ti Laptop GPU | NVIDIA GeForce RTX 5070 Ti Laptop GPU | Same device identity and LUID                                                                |
| Driver version                          | not exposed                           | not exposed                           | Exact version not independently proven                                                       |
| Scene / position                        | Dragonsreach; (-448,-128,-318)        | Dragonsreach; (-448,-128,-318)        | Exact retained position matches                                                              |
| Game hour                               | 9.532                                 | 1.958                                 | Different simulation state                                                                   |
| Weather                                 | Helios_SkyrimClearTU                  | Helios_SkyrimClearTU                  | Matches                                                                                      |
| Output per eye                          | 1512 × 1680                           | 1512 × 1680                           | All 66 paired dimensions/backends match                                                      |
| DevBench host                           | 1.18.1                                | 1.18.1                                | Matches                                                                                      |
| Historical automation runner hash       | retained                              | not retained                          | Exact historical runner binary identity is unproven; routes and producer timing fields match |
| DLSS preset / FSR                       | K / explicit FSR3                     | K / explicit FSR3                     | Matches                                                                                      |
| Foveation                               | 0.3 / 0.3 / 0.7                       | 0.3 / 0.3 / 0.7                       | Exact fixture matches                                                                        |
| Pacing / strict timeout                 | 5000 / 20000 ms                       | 5000 / 20000 ms                       | Same producer timing definition                                                              |
| Maximum client gap (ms)                 | 54.370                                | 40.695                                | Both VALID; 250 ms diagnostic budget                                                         |
| Toolchain / pinned dependencies         | MSVC 19.51.36252.0; SDK 10.0.28000.0  | same                                  | Exact retained manifests match                                                               |
| Shader compiler                         | d3dcompiler_47.dll:10.0.26100.9444    | d3dcompiler_47.dll:10.0.26100.9444    | Matches                                                                                      |
| Headset mode / refresh / power state    | not fully retained                    | not fully retained                    | Do not infer a frame budget or FPS gain                                                      |
| Whole modlist / shader-cache warm state | not equivalently snapshotted          | not equivalently snapshotted          | Profile name alone does not prove equality                                                   |
| Freshness counters                      | not exposed                           | available, process lifetime           | Use scoped H deltas only; B is n/a                                                           |

The matrix routes, timing definitions, API fixture, renderer dimensions,
build inputs, and owned telemetry are matched. Process IDs, simulation
time, and starting memory differ. There is one process per build and two
ordered passes, without randomized order or independent scene repeats.
All percentage differences are descriptive observations, not a causal
performance estimate or a statistical significance claim.

## Per-pass timing and health

| Metric                                  | B pass 1 | H pass 1 | B pass 2 | H pass 2 |
| --------------------------------------- | -------- | -------- | -------- | -------- |
| Strict total of 33 transitions (s)      | 27.670   | 25.319   | 27.392   | 24.107   |
| Strict mean (ms)                        | 838.490  | 767.246  | 830.058  | 730.514  |
| Strict median (ms)                      | 786.256  | 740.453  | 773.721  | 698.477  |
| Strict p95 (ms)                         | 1665.701 | 1381.815 | 1645.407 | 1268.782 |
| Strict maximum (ms)                     | 2002.054 | 1392.319 | 2236.990 | 1658.352 |
| Presentation mean (ms)                  | 719.276  | 656.928  | 706.319  | 621.937  |
| Cleanup tail mean (ms)                  | 96.137   | 88.953   | 102.879  | 87.192   |
| Cleanup tail maximum (ms)               | 286.128  | 290.280  | 411.067  | 251.354  |
| Producer retries                        | 10       | 8        | 9        | 10       |
| Transitions with retries                | 8        | 8        | 8        | 9        |
| Selected stretch rows                   | 17       | 16       | 15       | 15       |
| Vendor-failure stretch eye observations | 0        | 2        | 0        | 2        |
| Fidelity mismatch observations          | 0        | 4        | 0        | 4        |

All four passes retained 33/33 terminal rows and 12/12 required DLSS
trace windows. Recorded terminal render and per-transition Task 2
verdicts are PASS for every row. This is distinct from the full-history
health audit below. The p95 uses linear interpolation over the 33
heterogeneous transition latencies. Totals sum transitions only and
exclude the five-second waits, setup, cooldown, and evidence cleanup.

| Pass interval                                | B pass 1 | H pass 1 | B pass 2 | H pass 2 |
| -------------------------------------------- | -------- | -------- | -------- | -------- |
| First dispatch to last strict completion (s) | 191.423  | 188.910  | 191.158  | 187.650  |

This QPC interval includes intervening pacing and evidence/control work;
it is not the sum of transition costs and excludes delayed helper shutdown.

| Comparison                         | Pass 1  | Pass 2  |
| ---------------------------------- | ------- | ------- |
| H strict mean change (%)           | -8.497  | -11.992 |
| H faster / slower routes           | 21 / 12 | 30 / 3  |
| H total transition time change (s) | -2.351  | -3.285  |

| Within-build repeat change           | B      | H      |
| ------------------------------------ | ------ | ------ |
| Pass 2 versus pass 1 strict mean (%) | -1.006 | -4.788 |
| Pass 2 versus pass 1 retry count     | -1     | 2      |

## Health failures hidden by the original PASS summaries

H records **one vendor-failure stretch eye observation and two fidelity
mismatch observations at each of rows 26 and 28 in both passes**. These
are four affected transitions and eight mismatch observations total;
observations are not eight independent crashes or eight affected frames.
The per-window diagnostic deltas agree with the exact request/epoch
metrics in the owned stress record. B has zero for these counters.

| Pass / row | Route                       | B strict ms | H strict ms | H fidelity / vendor-failure eyes | Recorded / audit         |
| ---------- | --------------------------- | ----------- | ----------- | -------------------------------- | ------------------------ |
| 1 / 26     | DLSS Hoshipa → FSR3 Hoshipa | 941.710     | 1378.302    | 2 / 1                            | PASS / recovered failure |
| 1 / 28     | NONE → FSR3 UP              | 906.090     | 841.685     | 2 / 1                            | PASS / recovered failure |
| 2 / 26     | DLSS Hoshipa → FSR3 Hoshipa | 956.487     | 1227.931    | 2 / 1                            | PASS / recovered failure |
| 2 / 28     | NONE → FSR3 UP              | 954.873     | 842.921     | 2 / 1                            | PASS / recovered failure |

Their terminal receipts recovered to stable FSR3 with valid both-eye
fidelity and drained cleanup, so the terminal waiter returned satisfied.
That recovery does not erase the earlier vendor-failure fallback or
fidelity mismatch. Treat H as showing a **repeatable health regression
signal on these two routes**, pending diagnosis; the full report cannot
support an unqualified all-clear. Original per-row classifications and
raw evidence remain unchanged. No Task 2 aggregate verdict is invented.

Direct retained receipts: [H pass 1, row 26](../../artifacts/renderscale-tuning/nvidia-2026-09-10T07-19-50-405Z/raw/lane-nvidia/pass-1/transitions/26/retained.json), [H pass 1, row 28](../../artifacts/renderscale-tuning/nvidia-2026-09-10T07-19-50-405Z/raw/lane-nvidia/pass-1/transitions/28/retained.json), [H pass 2, row 26](../../artifacts/renderscale-tuning/nvidia-2026-09-10T07-19-50-405Z/raw/lane-nvidia/pass-2/transitions/26/retained.json), [H pass 2, row 28](../../artifacts/renderscale-tuning/nvidia-2026-09-10T07-19-50-405Z/raw/lane-nvidia/pass-2/transitions/28/retained.json).

| Separate terminal failure count | B pass 1 | H pass 1 | B pass 2 | H pass 2 |
| ------------------------------- | -------- | -------- | -------- | -------- |
| Device loss                     | 0        | 0        | 0        | 0        |
| OOM                             | 0        | 0        | 0        | 0        |
| Producer terminal failure       | 0        | 0        | 0        | 0        |
| DLSS lifecycle failure          | 0        | 0        | 0        | 0        |
| FSR lifecycle failure           | 0        | 0        | 0        | 0        |
| Memory trim failure             | 0        | 0        | 0        | 0        |
| Retirement fence failure        | 0        | 0        | 0        | 0        |

Vendor-native qualification failures and credible liveness timeouts are
zero in all four passes. No recovery apply, trace overflow, Task 2 phase
violation, or bounds-mismatch fallback was recorded. The zero runtime
terminal-failure count must not be used as a synonym for zero fidelity
or presentation failures.

| Raw cumulative stress gate       | B pass 1 | H pass 1 | B pass 2 | H pass 2 | Assessment use                         |
| -------------------------------- | -------- | -------- | -------- | -------- | -------------------------------------- |
| fidelity_invariants              | PASS     | FAIL     | PASS     | FAIL     | applicable health gate                 |
| presentation_fallbacks           | PASS     | FAIL     | PASS     | FAIL     | applicable health gate                 |
| presentation_stretch_frame_bound | FAIL     | FAIL     | FAIL     | FAIL     | diagnostic only: imposed settling      |
| presentation_recovered           | FAIL     | FAIL     | FAIL     | FAIL     | proven native-target contract mismatch |

All four raw cumulative records say `accepted: false`. The fixed
two-frame cutoff is inapplicable because settling imposes the stretch;
it does not count against health or improvement-or-neutral assessment.
Use the actual frame and duration comparisons below. The scaled
`presentation_recovered` gate also conflicts with the final native DLAA
target: both eyes report `NativeOriginal`, and the row-33 native vendor
proof passes. This is a retained contract mismatch. H separately fails
the applicable fidelity/fallback gates. These observations, not the two
inapplicable gates, prevent an improvement-or-neutral assessment.

## Side-by-side relatch, completion and stretch summary

Relatch proof is qualification dispatch to the first exact new generation
proof; strict completion includes the remaining qualification conditions.
There are 25 measured relatch boundaries per pass and 33 strict completions.
Missing/inapplicable relatch boundaries remain n/a. Stretch totals below
span the full owned capture. Each delta is H minus B.

| Pass | Metric               | Baseline  | Head      | H-B       |
| ---- | -------------------- | --------- | --------- | --------- |
| 1    | Relatch mean ms      | 805.314   | 708.580   | -96.734   |
| 1    | Relatch mean frames  | 14.040    | 13.160    | -0.880    |
| 1    | Relatch total ms     | 20132.851 | 17714.506 | -2418.345 |
| 1    | Relatch total frames | 351       | 329       | -22       |
| 1    | Strict mean ms       | 838.490   | 767.246   | -71.244   |
| 1    | Strict mean frames   | 15.242    | 14.970    | -0.273    |
| 1    | Strict total ms      | 27670.177 | 25319.115 | -2351.063 |
| 1    | Strict total frames  | 503       | 494       | -9        |
| 1    | Stretch episodes     | 19        | 18        | -1        |
| 1    | Stretch total frames | 79        | 74        | -5        |
| 1    | Stretch total ms     | 5066.983  | 4578.021  | -488.963  |
| 1    | Longest stretch ms   | 443.105   | 426.722   | -16.383   |
| 2    | Relatch mean ms      | 789.600   | 682.641   | -106.959  |
| 2    | Relatch mean frames  | 13.760    | 14.080    | 0.320     |
| 2    | Relatch total ms     | 19740.012 | 17066.032 | -2673.980 |
| 2    | Relatch total frames | 344       | 352       | 8         |
| 2    | Strict mean ms       | 830.058   | 730.514   | -99.545   |
| 2    | Strict mean frames   | 14.970    | 15.394    | 0.424     |
| 2    | Strict total ms      | 27391.922 | 24106.946 | -3284.976 |
| 2    | Strict total frames  | 494       | 508       | 14        |
| 2    | Stretch episodes     | 17        | 17        | 0         |
| 2    | Stretch total frames | 73        | 77        | 4         |
| 2    | Stretch total ms     | 4805.654  | 4310.798  | -494.856  |
| 2    | Longest stretch ms   | 537.562   | 417.844   | -119.719  |

## Per-transition strict timings, retries, and failures

All times are milliseconds from `qualification_dispatch`; the 5000 ms
wait before dispatch is excluded. `R` is producer retry occurrence count.
`F/V` is fidelity mismatches / vendor-failure stretch eye observations.
`S` is selected stretch maximum frames (`-` means not selected; `n/a`
means selected but maximum not exposed). Every row has recorded terminal
render PASS and Task 2 PASS; nonzero F/V overrides any interpretation
that the transition was free of transient failures. AA means native
anti-aliasing; UQ/Q/Bal/Perf/UP abbreviate the quality modes.

### Pass 1: complete 33-row comparison

| #   | Route                       | B strict | H strict | H-B ms    | H-B %   | R B/H | F/V B → H | S B/H     |
| --- | --------------------------- | -------- | -------- | --------- | ------- | ----- | --------- | --------- |
| 1   | DLSS Hoshipa → NONE         | 696.301  | 669.500  | -26.801   | -3.849  | 0 / 0 | 0/0 → 0/0 | 1 / 1     |
| 2   | NONE → TAA                  | 163.718  | 181.499  | 17.781    | 10.861  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 3   | TAA → DLAA                  | 318.517  | 289.827  | -28.690   | -9.007  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 4   | DLAA → DLSS Hoshipa         | 984.177  | 920.565  | -63.612   | -6.463  | 0 / 0 | 0/0 → 0/0 | 2 / 2     |
| 5   | DLSS Hoshipa → DLSS UQ      | 1135.488 | 981.793  | -153.696  | -13.536 | 0 / 0 | 0/0 → 0/0 | 2 / 2     |
| 6   | DLSS UQ → DLSS Q            | 1206.601 | 1099.691 | -106.909  | -8.860  | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 7   | DLSS Q → DLSS Bal           | 1281.789 | 1392.319 | 110.530   | 8.623   | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 8   | DLSS Bal → DLSS Perf        | 1542.256 | 1343.752 | -198.504  | -12.871 | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 9   | DLSS Perf → DLSS UP         | 1375.057 | 1221.973 | -153.084  | -11.133 | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 10  | DLSS UP → DLAA              | 896.801  | 740.453  | -156.348  | -17.434 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 11  | DLAA → TAA                  | 456.750  | 486.216  | 29.467    | 6.451   | 0 / 0 | 0/0 → 0/0 | - / -     |
| 12  | TAA → NONE                  | 166.066  | 155.001  | -11.065   | -6.663  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 13  | NONE → FSR3 AA              | 783.966  | 937.889  | 153.924   | 19.634  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 14  | FSR3 AA → FSR3 Hoshipa      | 1505.166 | 1339.594 | -165.572  | -11.000 | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 15  | FSR3 Hoshipa → FSR3 UQ      | 779.274  | 802.999  | 23.725    | 3.045   | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 16  | FSR3 UQ → FSR3 Q            | 703.972  | 803.849  | 99.877    | 14.188  | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 17  | FSR3 Q → FSR3 Bal           | 821.621  | 732.811  | -88.810   | -10.809 | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 18  | FSR3 Bal → FSR3 Perf        | 786.256  | 840.052  | 53.796    | 6.842   | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 19  | FSR3 Perf → FSR3 UP         | 719.557  | 708.567  | -10.990   | -1.527  | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 20  | FSR3 UP → FSR3 AA           | 1038.179 | 655.757  | -382.422  | -36.836 | 1 / 0 | 0/0 → 0/0 | 5 / -     |
| 21  | FSR3 AA → TAA               | 473.692  | 475.970  | 2.278     | 0.481   | 0 / 0 | 0/0 → 0/0 | - / -     |
| 22  | TAA → NONE                  | 166.960  | 174.895  | 7.935     | 4.753   | 0 / 0 | 0/0 → 0/0 | - / -     |
| 23  | NONE → DLAA                 | 246.406  | 270.911  | 24.505    | 9.945   | 0 / 0 | 0/0 → 0/0 | - / -     |
| 24  | DLAA → FSR3 AA              | 832.722  | 1024.970 | 192.249   | 23.087  | 0 / 1 | 0/0 → 0/0 | - / -     |
| 25  | FSR3 AA → DLSS Hoshipa      | 2002.054 | 934.134  | -1067.920 | -53.341 | 2 / 0 | 0/0 → 0/0 | 6 / 2     |
| 26  | DLSS Hoshipa → FSR3 Hoshipa | 941.710  | 1378.302 | 436.592   | 46.362  | 0 / 1 | 0/0 → 2/1 | 2 / n/a   |
| 27  | FSR3 Hoshipa → NONE         | 746.483  | 665.093  | -81.390   | -10.903 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 28  | NONE → FSR3 UP              | 906.090  | 841.685  | -64.406   | -7.108  | 0 / 0 | 0/0 → 2/1 | - / -     |
| 29  | FSR3 UP → DLSS UP           | 1850.868 | 1387.084 | -463.784  | -25.058 | 2 / 1 | 0/0 → 0/0 | n/a / n/a |
| 30  | DLSS UP → TAA               | 789.832  | 644.207  | -145.624  | -18.437 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 31  | TAA → FSR3 AA               | 588.769  | 562.726  | -26.043   | -4.423  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 32  | FSR3 AA → NONE              | 457.618  | 417.950  | -39.668   | -8.668  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 33  | NONE → DLAA                 | 305.461  | 237.079  | -68.382   | -22.387 | 0 / 0 | 0/0 → 0/0 | - / -     |

### Pass 1: relatch/strict frames and stretch duration

| #   | Relatch frames B/H | H-B frames | Relatch ms B/H      | H-B ms   | Strict frames B/H | H-B frames |
| --- | ------------------ | ---------- | ------------------- | -------- | ----------------- | ---------- |
| 1   | 10 / 9             | -1         | 478.192 / 441.106   | -37.086  | 16 / 15           | -1         |
| 2   | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 3 / 4             | 1          |
| 3   | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 5 / 5             | 0          |
| 4   | 12 / 12            | 0          | 719.392 / 651.966   | -67.426  | 17 / 17           | 0          |
| 5   | 13 / 14            | 1          | 888.041 / 723.978   | -164.063 | 18 / 19           | 1          |
| 6   | 17 / 18            | 1          | 951.444 / 858.834   | -92.610  | 22 / 23           | 1          |
| 7   | 17 / 17            | 0          | 1028.107 / 1141.716 | 113.609  | 22 / 22           | 0          |
| 8   | 17 / 16            | -1         | 1179.625 / 1046.161 | -133.464 | 22 / 21           | -1         |
| 9   | 17 / 16            | -1         | 1054.279 / 954.916  | -99.363  | 22 / 21           | -1         |
| 10  | 10 / 9             | -1         | 668.504 / 521.571   | -146.933 | 15 / 14           | -1         |
| 11  | 4 / 3              | -1         | 170.035 / 195.032   | 24.998   | 11 / 10           | -1         |
| 12  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 4             | 0          |
| 13  | 9 / 10             | 1          | 734.898 / 894.346   | 159.448  | 10 / 11           | 1          |
| 14  | 23 / 23            | 0          | 1251.007 / 1119.129 | -131.878 | 28 / 28           | 0          |
| 15  | 13 / 11            | -2         | 727.890 / 580.086   | -147.804 | 14 / 16           | 2          |
| 16  | 13 / 12            | -1         | 655.333 / 570.552   | -84.781  | 14 / 17           | 3          |
| 17  | 13 / 12            | -1         | 678.068 / 566.302   | -111.765 | 16 / 16           | 0          |
| 18  | 13 / 12            | -1         | 642.035 / 616.321   | -25.714  | 16 / 16           | 0          |
| 19  | 13 / 12            | -1         | 623.329 / 546.731   | -76.598  | 15 / 16           | 1          |
| 20  | 15 / 10            | -5         | 814.340 / 446.679   | -367.662 | 20 / 15           | -5         |
| 21  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 11 / 11           | 0          |
| 22  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 4             | 0          |
| 23  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 4             | 0          |
| 24  | 10 / 15            | 5          | 614.472 / 827.923   | 213.451  | 15 / 20           | 5          |
| 25  | 29 / 13            | -16        | 1676.578 / 700.663  | -975.915 | 34 / 18           | -16        |
| 26  | 13 / 23            | 10         | 721.998 / 1136.961  | 414.963  | 18 / 28           | 10         |
| 27  | 10 / 10            | 0          | 504.519 / 445.482   | -59.036  | 15 / 16           | 1          |
| 28  | 11 / 11            | 0          | 694.804 / 628.678   | -66.126  | 16 / 16           | 0          |
| 29  | 29 / 23            | -6         | 1564.693 / 1149.814 | -414.879 | 34 / 28           | -6         |
| 30  | 11 / 9             | -2         | 545.473 / 427.263   | -118.210 | 16 / 15           | -1         |
| 31  | 9 / 9              | 0          | 545.795 / 522.295   | -23.500  | 10 / 10           | 0          |
| 32  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 11 / 11           | 0          |
| 33  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 5 / 3             | -2         |

| #   | Stretch episodes B/H | H-B | Stretch frames B/H | H-B frames | Stretch ms B/H    | H-B ms   |
| --- | -------------------- | --- | ------------------ | ---------- | ----------------- | -------- |
| 1   | 1 / 1                | 0   | 1 / 1              | 0          | 117.824 / 114.697 | -3.126   |
| 2   | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 3   | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 4   | 1 / 1                | 0   | 2 / 2              | 0          | 203.612 / 201.515 | -2.096   |
| 5   | 1 / 1                | 0   | 2 / 2              | 0          | 203.429 / 200.370 | -3.058   |
| 6   | 1 / 1                | 0   | 6 / 6              | 0          | 377.315 / 380.828 | 3.513    |
| 7   | 1 / 1                | 0   | 6 / 6              | 0          | 371.738 / 393.856 | 22.118   |
| 8   | 1 / 1                | 0   | 6 / 6              | 0          | 443.105 / 426.722 | -16.383  |
| 9   | 1 / 1                | 0   | 6 / 6              | 0          | 396.764 / 371.096 | -25.668  |
| 10  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 11  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 12  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 13  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 14  | 1 / 1                | 0   | 6 / 6              | 0          | 342.065 / 278.286 | -63.778  |
| 15  | 1 / 1                | 0   | 3 / 3              | 0          | 165.547 / 141.276 | -24.271  |
| 16  | 1 / 1                | 0   | 3 / 3              | 0          | 154.253 / 149.235 | -5.018   |
| 17  | 1 / 1                | 0   | 3 / 3              | 0          | 155.618 / 138.085 | -17.533  |
| 18  | 1 / 1                | 0   | 3 / 3              | 0          | 149.113 / 176.326 | 27.214   |
| 19  | 1 / 1                | 0   | 3 / 3              | 0          | 154.153 / 137.756 | -16.397  |
| 20  | 1 / 0                | -1  | 5 / 0              | -5         | 347.808 / 0.000   | -347.808 |
| 21  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 22  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 23  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 24  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 25  | 1 / 1                | 0   | 6 / 2              | -4         | 406.342 / 199.863 | -206.479 |
| 26  | 1 / 2                | 1   | 2 / 11             | 9          | 93.320 / 588.339  | 495.019  |
| 27  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 28  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 29  | 3 / 2                | -1  | 16 / 11            | -5         | 984.981 / 679.770 | -305.211 |
| 30  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 31  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 32  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |
| 33  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000     | 0.000    |

### Pass 1: presentation, cleanup, and replacement phases

Every cell is **B / H ms**. `D→A` is dispatch to first blocked or
preparation observation; `A→M` is that observation to physical mutation;
`M→N` is mutation to first exact new generation. `Tail` is the producer
cleanup tail, preserved independently of strict latency. These phase
observations are not exclusive CPU timers and must not be blindly summed.

| #   | Presentation        | Cleanup             | Tail              | D→A               | A→M               | M→N               |
| --- | ------------------- | ------------------- | ----------------- | ----------------- | ----------------- | ----------------- |
| 1   | 526.999 / 501.294   | 696.301 / 669.500   | 169.303 / 168.206 | 360.288 / 327.297 | 3.985 / 2.685     | 113.920 / 111.124 |
| 2   | 163.718 / 181.499   | 163.718 / 181.499   | 0 / 0             | 163.718 / 181.499 | n/a / n/a         | n/a / n/a         |
| 3   | 318.517 / 289.827   | 318.517 / 289.827   | 0 / 0             | 318.517 / 289.827 | n/a / n/a         | n/a / n/a         |
| 4   | 760.083 / 695.179   | 897.306 / 836.902   | 137.223 / 141.723 | 398.983 / 338.413 | 3.838 / 3.351     | 316.570 / 310.202 |
| 5   | 930.534 / 768.274   | 1055.507 / 896.303  | 124.974 / 128.030 | 362.084 / 397.890 | 4.543 / 4.356     | 521.414 / 321.732 |
| 6   | 994.397 / 898.584   | 1122.383 / 1017.670 | 127.986 / 119.086 | 464.385 / 369.513 | 3.540 / 3.072     | 483.519 / 486.249 |
| 7   | 1071.236 / 1184.415 | 1200.527 / 1309.015 | 129.291 / 124.600 | 348.053 / 395.118 | 3.168 / 4.191     | 676.887 / 742.407 |
| 8   | 1239.872 / 1159.017 | 1426.129 / 1256.318 | 186.257 / 97.301  | 437.421 / 323.115 | 3.819 / 2.817     | 738.384 / 720.229 |
| 9   | 1104.808 / 1000.332 | 1278.004 / 1135.000 | 173.196 / 134.668 | 379.763 / 324.685 | 4.345 / 3.268     | 670.172 / 626.963 |
| 10  | 718.083 / 566.234   | 896.801 / 740.453   | 178.719 / 174.220 | 461.895 / 334.925 | 3.694 / 2.878     | 202.915 / 183.767 |
| 11  | 170.621 / 195.936   | 456.750 / 486.216   | 286.128 / 290.280 | 126.187 / 142.803 | 3.874 / 4.122     | 39.973 / 48.108   |
| 12  | 166.066 / 155.001   | 166.066 / 155.001   | 0 / 0             | 166.066 / 155.001 | n/a / n/a         | n/a / n/a         |
| 13  | 783.966 / 937.889   | 783.966 / 937.889   | 0 / 0             | 430.353 / 454.368 | 180.735 / 218.421 | 123.810 / 221.557 |
| 14  | 1505.166 / 1339.594 | 1449.353 / 1292.678 | 0 / 0             | 398.957 / 416.897 | 318.017 / 269.730 | 534.033 / 432.502 |
| 15  | 779.274 / 802.999   | 779.274 / 622.719   | 0 / 0             | 424.930 / 379.773 | 3.930 / 3.783     | 299.030 / 196.530 |
| 16  | 703.972 / 803.849   | 703.972 / 615.393   | 0 / 0             | 379.069 / 359.032 | 4.876 / 4.789     | 271.389 / 206.731 |
| 17  | 821.621 / 732.811   | 722.659 / 607.464   | 0 / 0             | 413.067 / 375.135 | 4.379 / 3.361     | 260.622 / 187.806 |
| 18  | 786.256 / 840.052   | 692.119 / 676.686   | 0 / 0             | 384.869 / 373.473 | 4.859 / 7.076     | 252.307 / 235.771 |
| 19  | 719.557 / 708.567   | 665.375 / 585.888   | 0 / 0             | 367.134 / 355.203 | 4.275 / 4.082     | 251.921 / 187.446 |
| 20  | 861.150 / 493.734   | 1038.179 / 655.757  | 177.030 / 162.023 | 411.358 / 324.305 | 280.292 / 4.628   | 122.691 / 117.745 |
| 21  | 207.282 / 194.486   | 473.692 / 475.970   | 266.410 / 281.484 | 139.328 / 129.119 | n/a / n/a         | n/a / n/a         |
| 22  | 166.960 / 174.895   | 166.960 / 174.895   | 0 / 0             | 166.960 / 174.895 | n/a / n/a         | n/a / n/a         |
| 23  | 246.406 / 270.911   | 246.406 / 270.911   | 0 / 0             | 246.406 / 270.911 | n/a / n/a         | n/a / n/a         |
| 24  | 658.299 / 866.555   | 832.722 / 1024.970  | 174.422 / 158.415 | 460.027 / 436.850 | 4.602 / 256.054   | 149.843 / 135.018 |
| 25  | 1725.049 / 739.348  | 1881.136 / 856.854  | 156.087 / 117.506 | 475.045 / 355.059 | 679.556 / 30.902  | 521.976 / 314.702 |
| 26  | 941.710 / 1378.302  | 895.713 / 1326.476  | 0 / 0             | 406.733 / 387.335 | 4.093 / 257.067   | 311.172 / 492.560 |
| 27  | 504.858 / 446.145   | 746.483 / 665.093   | 241.625 / 218.948 | 352.171 / 313.931 | 4.268 / 3.350     | 148.080 / 128.201 |
| 28  | 906.090 / 759.193   | 860.225 / 799.619   | 0 / 40.425        | 411.161 / 373.148 | 4.405 / 2.596     | 279.238 / 252.934 |
| 29  | 1611.873 / 1190.855 | 1755.722 / 1310.255 | 143.848 / 119.400 | 405.654 / 631.074 | 670.170 / 21.097  | 488.869 / 497.643 |
| 30  | 546.018 / 428.091   | 789.832 / 644.207   | 243.814 / 216.117 | 420.651 / 316.259 | 3.933 / 2.941     | 120.889 / 108.063 |
| 31  | 588.769 / 562.726   | 588.769 / 562.726   | 0 / 0             | 401.436 / 395.482 | 53.841 / 43.783   | 90.518 / 83.029   |
| 32  | 201.423 / 174.944   | 457.618 / 417.950   | 256.194 / 243.005 | 140.193 / 117.720 | n/a / n/a         | n/a / n/a         |
| 33  | 305.461 / 237.079   | 305.461 / 237.079   | 0 / 0             | 305.461 / 237.079 | n/a / n/a         | n/a / n/a         |

### Pass 2: complete 33-row comparison

| #   | Route                       | B strict | H strict | H-B ms   | H-B %   | R B/H | F/V B → H | S B/H     |
| --- | --------------------------- | -------- | -------- | -------- | ------- | ----- | --------- | --------- |
| 1   | DLSS Hoshipa → NONE         | 773.721  | 734.886  | -38.834  | -5.019  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 2   | NONE → TAA                  | 185.643  | 177.782  | -7.861   | -4.234  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 3   | TAA → DLAA                  | 273.757  | 260.525  | -13.231  | -4.833  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 4   | DLAA → DLSS Hoshipa         | 1052.778 | 906.681  | -146.097 | -13.877 | 0 / 0 | 0/0 → 0/0 | 2 / 2     |
| 5   | DLSS Hoshipa → DLSS UQ      | 1043.415 | 904.298  | -139.117 | -13.333 | 0 / 0 | 0/0 → 0/0 | 2 / 2     |
| 6   | DLSS UQ → DLSS Q            | 1198.608 | 1039.935 | -158.673 | -13.238 | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 7   | DLSS Q → DLSS Bal           | 1228.456 | 1064.043 | -164.413 | -13.384 | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 8   | DLSS Bal → DLSS Perf        | 1254.604 | 1205.586 | -49.019  | -3.907  | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 9   | DLSS Perf → DLSS UP         | 1152.681 | 1112.825 | -39.856  | -3.458  | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 10  | DLSS UP → DLAA              | 910.029  | 729.996  | -180.033 | -19.783 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 11  | DLAA → TAA                  | 630.672  | 431.684  | -198.988 | -31.552 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 12  | TAA → NONE                  | 173.368  | 154.506  | -18.862  | -10.880 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 13  | NONE → FSR3 AA              | 599.476  | 567.794  | -31.682  | -5.285  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 14  | FSR3 AA → FSR3 Hoshipa      | 1392.052 | 1214.111 | -177.940 | -12.783 | 1 / 1 | 0/0 → 0/0 | 6 / 6     |
| 15  | FSR3 Hoshipa → FSR3 UQ      | 741.365  | 722.015  | -19.349  | -2.610  | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 16  | FSR3 UQ → FSR3 Q            | 777.131  | 623.909  | -153.222 | -19.716 | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 17  | FSR3 Q → FSR3 Bal           | 710.865  | 699.393  | -11.472  | -1.614  | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 18  | FSR3 Bal → FSR3 Perf        | 659.517  | 670.129  | 10.612   | 1.609   | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 19  | FSR3 Perf → FSR3 UP         | 722.691  | 698.477  | -24.213  | -3.350  | 0 / 0 | 0/0 → 0/0 | 3 / 3     |
| 20  | FSR3 UP → FSR3 AA           | 699.146  | 643.825  | -55.320  | -7.913  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 21  | FSR3 AA → TAA               | 436.173  | 400.298  | -35.875  | -8.225  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 22  | TAA → NONE                  | 165.289  | 154.918  | -10.371  | -6.274  | 0 / 0 | 0/0 → 0/0 | - / -     |
| 23  | NONE → DLAA                 | 281.954  | 230.219  | -51.735  | -18.349 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 24  | DLAA → FSR3 AA              | 1101.494 | 1039.184 | -62.309  | -5.657  | 1 / 1 | 0/0 → 0/0 | - / -     |
| 25  | FSR3 AA → DLSS Hoshipa      | 2236.990 | 1658.352 | -578.637 | -25.867 | 1 / 2 | 0/0 → 0/0 | 6 / 6     |
| 26  | DLSS Hoshipa → FSR3 Hoshipa | 956.487  | 1227.931 | 271.444  | 28.379  | 0 / 1 | 0/0 → 2/1 | 2 / n/a   |
| 27  | FSR3 Hoshipa → NONE         | 798.961  | 696.952  | -102.010 | -12.768 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 28  | NONE → FSR3 UP              | 954.873  | 842.921  | -111.952 | -11.724 | 0 / 0 | 0/0 → 2/1 | - / -     |
| 29  | FSR3 UP → DLSS UP           | 2025.440 | 1330.059 | -695.381 | -34.332 | 2 / 1 | 0/0 → 0/0 | n/a / n/a |
| 30  | DLSS UP → TAA               | 787.284  | 618.367  | -168.917 | -21.456 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 31  | TAA → FSR3 AA               | 653.466  | 697.904  | 44.439   | 6.800   | 0 / 0 | 0/0 → 0/0 | - / -     |
| 32  | FSR3 AA → NONE              | 522.424  | 412.420  | -110.004 | -21.056 | 0 / 0 | 0/0 → 0/0 | - / -     |
| 33  | NONE → DLAA                 | 291.117  | 235.021  | -56.096  | -19.269 | 0 / 0 | 0/0 → 0/0 | - / -     |

### Pass 2: relatch/strict frames and stretch duration

| #   | Relatch frames B/H | H-B frames | Relatch ms B/H      | H-B ms   | Strict frames B/H | H-B frames |
| --- | ------------------ | ---------- | ------------------- | -------- | ----------------- | ---------- |
| 1   | 9 / 11             | 2          | 534.859 / 501.615   | -33.244  | 14 / 16           | 2          |
| 2   | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 3             | -1         |
| 3   | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 3             | -1         |
| 4   | 12 / 13            | 1          | 791.181 / 677.929   | -113.252 | 17 / 18           | 1          |
| 5   | 13 / 13            | 0          | 713.554 / 661.619   | -51.935  | 18 / 18           | 0          |
| 6   | 17 / 16            | -1         | 942.607 / 773.969   | -168.639 | 22 / 21           | -1         |
| 7   | 16 / 16            | 0          | 953.828 / 818.587   | -135.241 | 21 / 21           | 0          |
| 8   | 17 / 18            | 1          | 995.872 / 941.970   | -53.902  | 22 / 23           | 1          |
| 9   | 18 / 17            | -1         | 892.036 / 873.003   | -19.033  | 23 / 22           | -1         |
| 10  | 10 / 10            | 0          | 650.089 / 531.953   | -118.136 | 16 / 15           | -1         |
| 11  | 4 / 4              | 0          | 218.746 / 179.485   | -39.262  | 10 / 11           | 1          |
| 12  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 3             | -1         |
| 13  | 9 / 9              | 0          | 556.812 / 524.191   | -32.621  | 10 / 10           | 0          |
| 14  | 23 / 23            | 0          | 1175.770 / 1002.109 | -173.661 | 28 / 28           | 0          |
| 15  | 13 / 12            | -1         | 693.771 / 522.056   | -171.715 | 14 / 17           | 3          |
| 16  | 13 / 12            | -1         | 607.192 / 505.675   | -101.517 | 17 / 15           | -2         |
| 17  | 13 / 12            | -1         | 612.115 / 518.072   | -94.043  | 15 / 16           | 1          |
| 18  | 13 / 12            | -1         | 616.752 / 514.498   | -102.254 | 14 / 16           | 2          |
| 19  | 13 / 12            | -1         | 598.203 / 541.471   | -56.733  | 16 / 16           | 0          |
| 20  | 10 / 10            | 0          | 479.291 / 442.238   | -37.053  | 15 / 15           | 0          |
| 21  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 9 / 10            | 1          |
| 22  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 4 / 4             | 0          |
| 23  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 3 / 3             | 0          |
| 24  | 16 / 16            | 0          | 886.368 / 820.886   | -65.481  | 21 / 21           | 0          |
| 25  | 23 / 29            | 6          | 1906.152 / 1421.498 | -484.654 | 28 / 34           | 6          |
| 26  | 12 / 22            | 10         | 726.326 / 1035.738  | 309.412  | 17 / 27           | 10         |
| 27  | 10 / 11            | 1          | 545.764 / 461.584   | -84.180  | 16 / 17           | 1          |
| 28  | 11 / 11            | 0          | 732.866 / 628.408   | -104.458 | 16 / 16           | 0          |
| 29  | 29 / 24            | -5         | 1764.573 / 1101.750 | -662.823 | 34 / 29           | -5         |
| 30  | 11 / 9             | -2         | 537.828 / 406.552   | -131.276 | 17 / 14           | -3         |
| 31  | 9 / 10             | 1          | 607.455 / 659.176   | 51.721   | 11 / 12           | 1          |
| 32  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 11 / 11           | 0          |
| 33  | n/a / n/a          | n/a        | n/a / n/a           | n/a      | 3 / 3             | 0          |

| #   | Stretch episodes B/H | H-B | Stretch frames B/H | H-B frames | Stretch ms B/H     | H-B ms   |
| --- | -------------------- | --- | ------------------ | ---------- | ------------------ | -------- |
| 1   | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 2   | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 3   | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 4   | 1 / 1                | 0   | 2 / 2              | 0          | 237.080 / 193.586  | -43.493  |
| 5   | 1 / 1                | 0   | 2 / 2              | 0          | 227.874 / 193.446  | -34.428  |
| 6   | 1 / 1                | 0   | 6 / 6              | 0          | 410.076 / 350.457  | -59.619  |
| 7   | 1 / 1                | 0   | 6 / 6              | 0          | 431.069 / 366.733  | -64.336  |
| 8   | 1 / 1                | 0   | 6 / 6              | 0          | 402.433 / 417.844  | 15.411   |
| 9   | 1 / 1                | 0   | 6 / 6              | 0          | 378.428 / 388.059  | 9.631    |
| 10  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 11  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 12  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 13  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 14  | 1 / 1                | 0   | 6 / 6              | 0          | 275.278 / 247.670  | -27.608  |
| 15  | 1 / 1                | 0   | 3 / 3              | 0          | 157.869 / 132.586  | -25.283  |
| 16  | 1 / 1                | 0   | 3 / 3              | 0          | 142.171 / 128.082  | -14.089  |
| 17  | 1 / 1                | 0   | 3 / 3              | 0          | 152.821 / 133.435  | -19.386  |
| 18  | 1 / 1                | 0   | 3 / 3              | 0          | 143.143 / 131.632  | -11.510  |
| 19  | 1 / 1                | 0   | 3 / 3              | 0          | 140.963 / 128.674  | -12.288  |
| 20  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 21  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 22  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 23  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 24  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 25  | 1 / 1                | 0   | 6 / 6              | 0          | 537.562 / 353.264  | -184.298 |
| 26  | 1 / 2                | 1   | 2 / 11             | 9          | 105.403 / 533.118  | 427.715  |
| 27  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 28  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 29  | 3 / 2                | -1  | 16 / 11            | -5         | 1063.485 / 612.211 | -451.274 |
| 30  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 31  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 32  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |
| 33  | 0 / 0                | 0   | 0 / 0              | 0          | 0.000 / 0.000      | 0.000    |

### Pass 2: presentation, cleanup, and replacement phases

Every cell is **B / H ms**. `D→A` is dispatch to first blocked or
preparation observation; `A→M` is that observation to physical mutation;
`M→N` is mutation to first exact new generation. `Tail` is the producer
cleanup tail, preserved independently of strict latency. These phase
observations are not exclusive CPU timers and must not be blindly summed.

| #   | Presentation        | Cleanup             | Tail              | D→A                | A→M               | M→N               |
| --- | ------------------- | ------------------- | ----------------- | ------------------ | ----------------- | ----------------- |
| 1   | 535.318 / 502.328   | 773.721 / 734.886   | 238.403 / 232.558 | 394.044 / 385.655  | 3.969 / 3.149     | 136.847 / 112.811 |
| 2   | 185.643 / 177.782   | 185.643 / 177.782   | 0 / 0             | 185.643 / 177.782  | n/a / n/a         | n/a / n/a         |
| 3   | 273.757 / 260.525   | 273.757 / 260.525   | 0 / 0             | 273.757 / 260.525  | n/a / n/a         | n/a / n/a         |
| 4   | 839.067 / 716.662   | 969.420 / 829.553   | 130.353 / 112.891 | 416.896 / 380.628  | 4.302 / 2.865     | 369.983 / 294.436 |
| 5   | 775.617 / 700.205   | 958.192 / 819.578   | 182.575 / 119.374 | 360.877 / 356.848  | 4.666 / 3.314     | 348.011 / 301.457 |
| 6   | 987.350 / 813.263   | 1115.568 / 953.048  | 128.218 / 139.784 | 409.264 / 323.552  | 4.252 / 2.802     | 529.091 / 447.615 |
| 7   | 998.709 / 859.186   | 1138.165 / 980.442  | 139.456 / 121.255 | 395.124 / 346.576  | 3.943 / 3.625     | 554.760 / 468.386 |
| 8   | 1037.818 / 985.673  | 1168.613 / 1120.593 | 130.795 / 134.920 | 464.071 / 408.503  | 4.541 / 3.633     | 527.260 / 529.834 |
| 9   | 936.617 / 911.436   | 1070.546 / 1028.647 | 133.929 / 117.211 | 402.811 / 366.761  | 4.088 / 3.061     | 485.137 / 503.181 |
| 10  | 706.727 / 576.899   | 910.029 / 729.996   | 203.302 / 153.098 | 422.869 / 344.405  | 4.924 / 2.752     | 222.297 / 184.795 |
| 11  | 219.605 / 180.330   | 630.672 / 431.684   | 411.067 / 251.354 | 154.140 / 135.727  | 4.277 / 3.153     | 60.330 / 40.604   |
| 12  | 173.368 / 154.506   | 173.368 / 154.506   | 0 / 0             | 173.368 / 154.506  | n/a / n/a         | n/a / n/a         |
| 13  | 599.476 / 567.794   | 599.476 / 567.794   | 0 / 0             | 417.925 / 380.978  | 48.709 / 51.074   | 90.178 / 92.139   |
| 14  | 1392.052 / 1082.938 | 1345.444 / 1166.144 | 0 / 83.207        | 450.169 / 360.644  | 289.698 / 253.132 | 435.903 / 388.332 |
| 15  | 741.365 / 722.015   | 741.365 / 567.055   | 0 / 0             | 413.567 / 330.129  | 3.965 / 4.478     | 276.239 / 187.449 |
| 16  | 777.131 / 623.909   | 649.521 / 542.328   | 0 / 0             | 362.187 / 322.249  | 3.896 / 4.096     | 241.109 / 179.330 |
| 17  | 710.865 / 699.393   | 659.007 / 557.760   | 0 / 0             | 349.889 / 331.592  | 4.616 / 3.479     | 257.610 / 183.001 |
| 18  | 659.517 / 670.129   | 659.517 / 552.224   | 0 / 0             | 373.397 / 331.165  | 4.191 / 3.276     | 239.164 / 180.058 |
| 19  | 722.691 / 698.477   | 639.680 / 579.160   | 0 / 0             | 350.890 / 355.396  | 3.835 / 3.791     | 243.478 / 182.284 |
| 20  | 525.271 / 486.824   | 699.146 / 643.825   | 173.874 / 157.002 | 345.305 / 325.801  | 3.816 / 3.584     | 130.171 / 112.853 |
| 21  | 184.216 / 168.741   | 436.173 / 400.298   | 251.957 / 231.557 | 125.137 / 114.578  | n/a / n/a         | n/a / n/a         |
| 22  | 165.289 / 154.918   | 165.289 / 154.918   | 0 / 0             | 165.289 / 154.918  | n/a / n/a         | n/a / n/a         |
| 23  | 281.954 / 230.219   | 281.954 / 230.219   | 0 / 0             | 281.954 / 230.219  | n/a / n/a         | n/a / n/a         |
| 24  | 928.229 / 861.456   | 1101.494 / 1039.184 | 173.265 / 177.728 | 461.375 / 412.073  | 270.687 / 253.735 | 154.306 / 155.078 |
| 25  | 1973.783 / 1461.034 | 2143.097 / 1578.508 | 169.314 / 117.475 | 1138.652 / 370.430 | 50.674 / 592.203  | 716.827 / 458.865 |
| 26  | 956.487 / 1227.931  | 907.231 / 1186.869  | 0 / 0             | 388.918 / 358.942  | 4.582 / 250.725   | 332.826 / 426.071 |
| 27  | 546.828 / 529.267   | 798.961 / 696.952   | 252.133 / 167.684 | 385.438 / 353.591  | 3.928 / 3.484     | 156.399 / 104.509 |
| 28  | 954.873 / 842.921   | 908.025 / 795.130   | 0 / 0             | 426.022 / 359.176  | 4.329 / 2.618     | 302.515 / 266.614 |
| 29  | 1808.043 / 1139.899 | 1940.967 / 1253.666 | 132.924 / 113.767 | 530.939 / 652.647  | 727.476 / 22.365  | 506.158 / 426.738 |
| 30  | 538.229 / 407.171   | 787.284 / 618.367   | 249.056 / 211.196 | 417.456 / 306.806  | 4.165 / 2.704     | 116.208 / 97.042  |
| 31  | 653.466 / 697.904   | 653.466 / 697.904   | 0 / 0             | 453.411 / 406.124  | 57.297 / 58.653   | 96.747 / 194.400  |
| 32  | 228.048 / 177.159   | 522.424 / 412.420   | 294.376 / 235.261 | 153.082 / 123.612  | n/a / n/a         | n/a / n/a         |
| 33  | 291.117 / 235.021   | 291.117 / 235.021   | 0 / 0             | 291.117 / 235.021  | n/a / n/a         | n/a / n/a         |

## Repeatability and the largest changes

H is faster in both passes at 20 routes: 1, 3, 4, 5, 6, 8, 9, 10, 12, 14, 17, 19, 20, 25, 27, 28, 29, 30, 32, 33.
H is slower in both passes at routes 18, 26.

| Route                       | Pass 1 B/H ms       | Pass 2 B/H ms       | Pass 1 H-B | Pass 2 H-B |
| --------------------------- | ------------------- | ------------------- | ---------- | ---------- |
| FSR3 AA → DLSS Hoshipa      | 2002.054 / 934.134  | 2236.990 / 1658.352 | -1067.920  | -578.637   |
| FSR3 UP → DLSS UP           | 1850.868 / 1387.084 | 2025.440 / 1330.059 | -463.784   | -695.381   |
| DLSS Hoshipa → FSR3 Hoshipa | 941.710 / 1378.302  | 956.487 / 1227.931  | 436.592    | 271.444    |
| NONE → FSR3 UP              | 906.090 / 841.685   | 954.873 / 842.921   | -64.406    | -111.952   |
| TAA → FSR3 AA               | 588.769 / 562.726   | 653.466 / 697.904   | -26.043    | 44.439     |

Routes 25 and 29 supply the largest repeatable latency reductions. At
row 25 pass 1, H has zero retries versus B’s two, and the observed
blocked/preparation-to-mutation interval is about 649 ms shorter. In
pass 2 H has two retries versus B’s one, yet still completes faster;
retry count alone does not explain total latency. Row 29 has one H
retry versus two B retries in each pass. Row 26 is slower on H in both
passes and carries the repeated fidelity/fallback failure.

| Destination group           | Rows/pass | P1 B/H mean ms      | P1 H-B % | P2 B/H mean ms      | P2 H-B % |
| --------------------------- | --------- | ------------------- | -------- | ------------------- | -------- |
| DLAA destinations           | 4         | 441.796 / 384.568   | -12.954  | 439.214 / 363.940   | -17.138  |
| Scaled DLSS destinations    | 8         | 1422.286 / 1160.164 | -18.430  | 1399.121 / 1152.722 | -17.611  |
| FSR3 Native AA destinations | 4         | 810.909 / 795.336   | -1.920   | 763.395 / 737.177   | -3.434   |
| Scaled FSR3 destinations    | 8         | 895.456 / 930.982   | 3.967    | 864.372 / 837.361   | -3.125   |
| TAA destinations            | 4         | 470.998 / 446.973   | -5.101   | 509.943 / 407.033   | -20.181  |
| None destinations           | 5         | 446.686 / 416.488   | -6.760   | 486.752 / 430.736   | -11.508  |

## Retry and presentation detail

All retry occurrences in these runs are Backend deferrals. Pressure
and retirement deferrals are zero. Counts were independently matched
to non-overwritten stress events using the exact Build ID, stress
session, request ID, and transition epoch. Retry-to-Applied/Stable
intervals below are frame spans, not milliseconds or isolated backoff.
Coalesced events do not expose each retry’s QPC timestamp.

| Pass/# | Route                       | Retries B/H | Retry→Applied frames B/H | Retry→Stable frames B/H |
| ------ | --------------------------- | ----------- | ------------------------ | ----------------------- |
| 1/6    | DLSS UQ → DLSS Q            | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 1/7    | DLSS Q → DLSS Bal           | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 1/8    | DLSS Bal → DLSS Perf        | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 1/9    | DLSS Perf → DLSS UP         | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 1/14   | FSR3 AA → FSR3 Hoshipa      | 1 / 1       | 6 / 6                    | 14 / 14                 |
| 1/20   | FSR3 UP → FSR3 AA           | 1 / 0       | 6 / n/a                  | 7 / n/a                 |
| 1/24   | DLAA → FSR3 AA              | 0 / 1       | n/a / 6                  | n/a / 7                 |
| 1/25   | FSR3 AA → DLSS Hoshipa      | 2 / 0       | 12 / n/a                 | 20 / n/a                |
| 1/26   | DLSS Hoshipa → FSR3 Hoshipa | 0 / 1       | n/a / 6                  | n/a / 14                |
| 1/29   | FSR3 UP → DLSS UP           | 2 / 1       | 12 / 6                   | 20 / 14                 |
| 2/6    | DLSS UQ → DLSS Q            | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 2/7    | DLSS Q → DLSS Bal           | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 2/8    | DLSS Bal → DLSS Perf        | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 2/9    | DLSS Perf → DLSS UP         | 1 / 1       | 0 / 0                    | 8 / 8                   |
| 2/14   | FSR3 AA → FSR3 Hoshipa      | 1 / 1       | 6 / 6                    | 14 / 14                 |
| 2/24   | DLAA → FSR3 AA              | 1 / 1       | 6 / 6                    | 7 / 7                   |
| 2/25   | FSR3 AA → DLSS Hoshipa      | 1 / 2       | 6 / 12                   | 14 / 20                 |
| 2/26   | DLSS Hoshipa → FSR3 Hoshipa | 0 / 1       | n/a / 6                  | n/a / 14                |
| 2/29   | FSR3 UP → DLSS UP           | 2 / 1       | 12 / 6                   | 20 / 14                 |

| Pass-wide stretch observation | B P1     | H P1     | B P2     | H P2     |
| ----------------------------- | -------- | -------- | -------- | -------- |
| Completed episodes            | 19       | 18       | 17       | 17       |
| Completed frames              | 79       | 74       | 73       | 77       |
| Completed duration ms         | 5066.983 | 4578.021 | 4805.654 | 4310.798 |
| Maximum episode ms            | 443.105  | 426.722  | 537.562  | 417.844  |
| Maximum episode frames        | 6        | 6        | 6        | 6        |
| Active at stop                | False    | False    | False    | False    |

Pass-wide stretch episodes include the full owned capture, whereas
selected-stretch rows are counted at their physical-mutation boundary.
They are different denominators. All selected rows recovered and no
stretch episode remained active at stop. The four H vendor-failure
observations are separate from allowed presentation stretch.

## Memory and resource health

Both repeats classify memory as **inconclusive**. B pass-1 private
usage increases slightly while system commit falls; H has decreasing
private and commit usage in both passes. The joint positive pass-1
growth prerequisite is false for both builds. Negative deltas do not
prove leak freedom. Exact-profile steady-state memory trends are
`evaluated: false` in all four stress captures.

| Memory / resources       | B P1 start/end/Δ                | H P1 start/end/Δ                 | B P2 start/end/Δ                 | H P2 start/end/Δ                 |
| ------------------------ | ------------------------------- | -------------------------------- | -------------------------------- | -------------------------------- |
| Process private MiB      | 16638.969 / 16673.613 / 34.645  | 17082.270 / 16608.777 / -473.492 | 17053.371 / 16583.574 / -469.797 | 16990.129 / 16624.867 / -365.262 |
| System commit MiB        | 52446.473 / 52377.207 / -69.266 | 49931.020 / 49272.242 / -658.777 | 53006.414 / 52802.812 / -203.602 | 49759.613 / 49175.984 / -583.629 |
| DXGI usage MiB           | 5592.391 / 4010.410 / -1581.980 | 4083.844 / 3340.594 / -743.250   | 4327.898 / 3658.172 / -669.727   | 3657.332 / 3244.277 / -413.055   |
| Tracked live textures    | 0 / 261 / 261                   | 0 / 217 / 217                    | 0 / 209 / 209                    | 0 / 207 / 207                    |
| Tracked live texture MiB | 0 / 2429.896 / 2429.896         | 0 / 2281.598 / 2281.598          | 0 / 2270.931 / 2270.931          | 0 / 2265.598 / 2265.598          |

| 10-second cooldown | B start/end/Δ                   | H start/end/Δ                  |
| ------------------ | ------------------------------- | ------------------------------ |
| processPrivateMiB  | 17018.992 / 17021.715 / 2.723   | 16946.500 / 16950.551 / 4.051  |
| systemCommitMiB    | 52734.500 / 52855.316 / 120.816 | 49638.043 / 49672.988 / 34.945 |
| dxgiUsageMiB       | 4267.973 / 4267.973 / 0         | 3598.156 / 3598.156 / 0        |
| liveTextures       | 261 / 261 / 0                   | 217 / 217 / 0                  |
| liveTextureMiB     | 2429.896 / 2429.896 / 0         | 2281.598 / 2281.598 / 0        |

Every retained start/end/cooldown pressure is Normal; per-transition
peak pressure, peak private/commit/DXGI usage, retirement counts, trim
counts, and terminal memory are included in the CSV and interactive
row details. Texture trackers reset at the start of each pass, so growth
from zero counts allocations observed by that tracker, not a net
process-wide leak. No texture attach, recording, sentinel allocation,
unknown-size, or dropped-record error was recorded.

## CPU/GPU telemetry and what cannot be timed

The four profiler snapshots are enabled but contain `timerCount: 0`,
`captured: 0`, and no resolved frame samples. Their zero `totalsMs`
values are **unavailable timing evidence**, not zero CPU/GPU cost.
A steady-state FPS or whole-frame GPU performance comparison cannot be
computed from these captures. The available CPU data are instrumentation
counters and queue-lock durations; GPU data are workload counters.
Counts span different numbers of observed frames and changing methods.

| CPU observation                         | B P1    | H P1    | B P2    | H P2    |
| --------------------------------------- | ------- | ------- | ------- | ------- |
| Observed frames                         | 4167    | 4450    | 4038    | 4641    |
| Queue hold mean µs                      | 1.860   | 1.872   | 1.891   | 1.817   |
| Queue hold max µs                       | 133.600 | 17.200  | 27.500  | 20.300  |
| Queue wait mean µs                      | 0.139   | 0.136   | 0.139   | 0.141   |
| Queue wait max µs                       | 15.100  | 1.200   | 1.300   | 20.300  |
| Stereo commit accepts                   | 4282    | 4549    | 4181    | 4768    |
| Stereo commit rejects                   | 66      | 65      | 63      | 64      |
| Lifetime rebuilds                       | 104     | 100     | 101     | 103     |
| Lifetime reuses                         | 2168    | 2303    | 2118    | 2422    |
| Full validations / 1000 observed frames | 132.229 | 125.618 | 137.444 | 124.542 |

Queue maxima are individual observations and do not establish a
repeatable speedup. Commit rejects are ownership-validation outcomes,
not automatically rendering failures. Full counters remain in
`telemetry.csv` and `comparison.json`.

| GPU workload observation          | B P1  | H P1  | B P2  | H P2  |
| --------------------------------- | ----- | ----- | ----- | ----- |
| Observed frames                   | 4168  | 4450  | 4039  | 4642  |
| Active FSR copy calls             | 14830 | 15840 | 14730 | 16940 |
| Active FSR pixel ratio            | 0.307 | 0.307 | 0.310 | 0.306 |
| FSR stereo batch attempts         | 34    | 0     | 34    | 0     |
| FSR stereo batch successes        | 30    | 0     | 30    | 0     |
| FSR stereo batch failures         | 0     | 0     | 0     | 0     |
| FSR stereo batch reuses           | 20    | 0     | 20    | 0     |
| Periphery TAA dispatches          | 4704  | 5059  | 4586  | 5281  |
| Periphery TAA avoided pixel ratio | 0.501 | 0.501 | 0.501 | 0.501 |
| Early HAM clears                  | 1978  | 2134  | 1900  | 2176  |
| Mirror writeback copy pairs       | 0     | 0     | 0     | 0     |

B records 34 runtime-FSR stereo batch attempts and 30 successes in
each pass; H records zero. This is an observed execution-path
difference, not proof of GPU milliseconds saved or a missing FSR
backend: target-correlated native/scaled FSR3 execution proofs pass.
H’s process-lifetime freshness counters were differenced across each
owned pass below. B lacks the counters and remains n/a.

| Freshness counter pass delta                     | B P1/P2 | H P1 | H P2 |
| ------------------------------------------------ | ------- | ---- | ---- |
| /boundaryOutcomes/FlagsMismatch                  | n/a     | 9148 | 9520 |
| /boundaryOutcomes/accepted                       | n/a     | 0    | 0    |
| /methods/dlss/inputOutcomes/MissingOuterBoundary | n/a     | 2602 | 2642 |
| /methods/fsr/inputOutcomes/MissingOuterBoundary  | n/a     | 2206 | 2374 |
| /methods/dlss/inputOutcomes/accepted             | n/a     | 0    | 0    |
| /methods/fsr/inputOutcomes/accepted              | n/a     | 0    | 0    |
| /methods/dlss/work/fallbackOutputHits            | n/a     | 0    | 0    |
| /methods/fsr/work/fallbackOutputHits             | n/a     | 0    | 0    |
| /methods/dlss/work/vendorEyeAttempts             | n/a     | 2500 | 2534 |
| /methods/fsr/work/vendorEyeAttempts              | n/a     | 2134 | 2302 |

The recorded H path never accepts the newer freshness gate in these
windows; FlagsMismatch and MissingOuterBoundary dominate its outcome
counters. These counters describe admission to the newer input-reuse
path, not crashes or failed vendor evaluations. They do not alone
establish the cause of the row-26/28 failure or the latency changes.

Preparation traces retain admission/early-exit observations for this
public-API fixture. `wrong_origin` and `non_direct_edit` describe
preparation ineligibility; the authoritative replacement mutation can
still be admitted. Do not interpret zero compilation/prewarm fields
on those early-exit events as a measured absence of all shader work.
The row details retain the preparation events and five independent
replacement phase durations with missing values labeled n/a.

## Evidence, validation, and reporting corrections

This is an offline comparison. No game transition, profile change,
new capture, or source/build mutation was performed. Both runs verified
their owned captures inactive. H’s helper shutdown stalled after its
measurements and was recovered later; that delay is excluded from
performance metrics. B’s worker completed normally. Both required a
supplemental memory summary after the packaged finalizer omitted it.

The old summaries’ “66 PASS” statements describe the stored terminal
classification. They are incomplete as full-history health summaries:
they omit H’s repeated transient fidelity/fallback failures and both
builds’ cumulative stress acceptance failures. This report adds that
missing health interpretation while preserving every original receipt.

Validation matched 132 retained transition receipts, 132 owned request/
epoch metrics, and all 132 retry counts to unoverwritten stress events;
all 66 paired routes, dimensions, and backends match. It checked 1,056
numeric timing cells against the existing [canonical ledger](vr-render-scale-comparison-ledger.csv),
verified 158 indexed evidence files, and confirmed the ledger hash unchanged.
The toolchain, dependency pins, shader compiler, adapter identity, and
fixture comparisons pass. Known environmental gaps remain explicit.

The omitted health findings were recorded locally through the automation
feedback controller: `AUTO-20260910-094450638-501F8B29`.

Reproduce locally with:

```text
python artifacts/renderscale-baseline-vs-head-20260910/compare.py
python artifacts/renderscale-baseline-vs-head-20260910/render-report.py
```

The comparison is an analysis export, not a replacement timing ledger.
Use the canonical ledger for durable historical timing data.

-   [Interactive comparison](../../artifacts/renderscale-baseline-vs-head-20260910/comparison.html): pass and metric selection, route search, health filters, complete row details.
-   [Per-transition CSV](../../artifacts/renderscale-baseline-vs-head-20260910/transitions.csv): all 132 rows with exact build identities, timings, retries, failure counters, memory, and evidence paths.
-   [Paired deltas CSV](../../artifacts/renderscale-baseline-vs-head-20260910/pairs.csv) and [pass summaries](../../artifacts/renderscale-baseline-vs-head-20260910/passes.csv).
-   [CPU/GPU/resource telemetry](../../artifacts/renderscale-baseline-vs-head-20260910/telemetry.csv), [structured comparison](../../artifacts/renderscale-baseline-vs-head-20260910/comparison.json), and [validation/source hashes](../../artifacts/renderscale-baseline-vs-head-20260910/validation.json).
-   [Maintained automatic comparison output](../../artifacts/switch-health-reporting/real-comparison/comparison.md) and [canonical-ledger verification](../../artifacts/switch-health-reporting/real-comparison/ledger-validation.json), generated using [the reporting workflow](vr-render-scale-comparison-reporting.md). PR inclusion remains the user’s decision.
