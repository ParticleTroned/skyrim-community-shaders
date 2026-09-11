# PR65 NVIDIA comparison: older baseline and both main-VR repeats

<!-- pr65-baseline-repeat-means-v1 -->

### NVIDIA comparison: older baseline vs main-VR with PR65

**Main-VR averaged 799.2 ms per switch versus 834.3 ms for the older
baseline: 35.1 ms faster (4.2%).** The result varied between repeats:
repeat 1 was 10.2% faster than baseline; repeat 2 was 1.8% slower.
Recovered fidelity/vendor failures occurred in **all four main-VR
passes**, on the same two routes. The baseline had none.

All transitions completed and reached terminal PASS. The recurring
failures mean the main-VR result **does not meet the improvement-or-neutral
standard**, despite its lower combined average.

**Groups:** baseline = the two passes of the older renderer, compiled as
`390fdea25`; main-VR repeat 1 = two passes of `348803c18`; main-VR repeat
2 = two passes of `7c8e3e656`. Both main-VR builds include PR65. Repeat 2
adds retry instrumentation. The combined main-VR mean uses all four
passes equally; the baseline uses its two actual passes once.

#### Main comparison

Lower times are better. “Switch completion” means the strict waiter has
finished; “relatch proof” is the first exact new-generation proof.
The final two columns show **± one standard error (SE) across pass
summaries**, in the same units as the row.

| Measurement                    | Baseline mean (2 passes) | Main-VR mean (4 passes) | Change vs baseline | ±SE baseline | ±SE main-VR |
| ------------------------------ | -----------------------: | ----------------------: | -----------------: | -----------: | ----------: |
| Switch completion (ms)         |                    834.3 |                   799.2 |      -35.1 (-4.2%) |         ±4.2 |       ±30.8 |
| Presentation ready (ms)        |                    712.8 |                   677.4 |      -35.4 (-5.0%) |         ±6.5 |       ±24.0 |
| Cleanup tail (ms)              |                     99.5 |                    98.3 |       -1.2 (-1.3%) |         ±3.4 |        ±5.9 |
| Relatch proof (ms)             |                    797.5 |                   740.6 |      -56.9 (-7.1%) |         ±7.9 |       ±28.0 |
| Switch completion (frames)     |                     15.1 |                    15.3 |       +0.2 (+1.6%) |         ±0.1 |        ±0.1 |
| Relatch proof (frames)         |                     13.9 |                    13.8 |       -0.1 (-1.1%) |         ±0.1 |        ±0.2 |
| Stretch episodes per pass      |                     18.0 |                    18.2 |       +0.2 (+1.4%) |         ±1.0 |        ±0.5 |
| Stretch frames per pass        |                     76.0 |                    80.8 |       +4.8 (+6.2%) |         ±3.0 |        ±3.3 |
| Stretch duration per pass (ms) |                  4,936.3 |                 4,998.0 |      +61.7 (+1.2%) |       ±130.7 |      ±331.4 |

Each timing value first averages the same routes within a pass: 33
switches for completion/presentation/cleanup and 25 applicable relatch
boundaries. Stretch rows average the total for each pass.

#### The two main-VR repeats

This table shows why the combined result should be read with its
repeat-to-repeat variation. Its final three columns retain the requested
SE for each two-pass repeat and for all four passes together.

| Measurement                    | Repeat 1 mean | Repeat 2 mean | All 4 passes mean | ±SE repeat 1 | ±SE repeat 2 | ±SE all 4 |
| ------------------------------ | ------------: | ------------: | ----------------: | -----------: | -----------: | --------: |
| Switch completion (ms)         |         748.9 |         849.5 |             799.2 |        ±18.4 |        ±16.5 |     ±30.8 |
| Presentation ready (ms)        |         639.4 |         715.4 |             677.4 |        ±17.5 |        ±16.4 |     ±24.0 |
| Cleanup tail (ms)              |          88.1 |         108.5 |              98.3 |         ±0.9 |       ±0.009 |      ±5.9 |
| Relatch proof (ms)             |         695.6 |         785.6 |             740.6 |        ±13.0 |        ±21.8 |     ±28.0 |
| Switch completion (frames)     |          15.2 |          15.5 |              15.3 |         ±0.2 |         ±0.1 |      ±0.1 |
| Relatch proof (frames)         |          13.6 |          13.9 |              13.8 |         ±0.5 |       ±0.040 |      ±0.2 |
| Stretch episodes per pass      |          17.5 |          19.0 |              18.2 |         ±0.5 |         ±0.0 |      ±0.5 |
| Stretch frames per pass        |          75.5 |          86.0 |              80.8 |         ±1.5 |         ±3.0 |      ±3.3 |
| Stretch duration per pass (ms) |       4,444.4 |       5,551.5 |           4,998.0 |       ±133.6 |       ±168.4 |    ±331.4 |

<details>
<summary>How the means and SE are calculated</summary>

Mean = arithmetic mean of the retained pass summaries. SE = sample
standard deviation of those summaries divided by √n: n = 2 for the
baseline and each repeat, n = 4 for combined main-VR. Calculations use
unrounded values. SE is not a confidence interval or a significance test.

The four main-VR passes come from two process runs, with two ordered
passes in each. They are not four independent experiments. The requested
pass-based SE is descriptive. Using the two independent repeat means
instead gives a main-VR switch-time SE of **±50.3 ms**, compared with
**±30.8 ms** across the four pass means.

The baseline contains one process run with two passes; it was not rerun
for repeat 2. This grouping cannot establish a precise between-build
confidence interval. Different simulation times and instrumentation
also limit causal interpretation. It measures cumulative main-VR changes,
including PR65, rather than isolating this PR’s contribution.

</details>

#### Failures and recovery remain visible

The counts below are totals, not averages. Each main-VR pass had four
fidelity mismatch observations and two vendor-failure stretch-eye
observations. They occurred on:

-   **Row 26:** DLSS Hoshipa → FSR3 Hoshipa.
-   **Row 28:** None → FSR3 Ultra Performance.

Each affected switch recovered within its original transition and
finished with valid both-eye output and drained cleanup. No extra
recovery apply or replay was required.

| Result                                  | Baseline: 2 passes | Main-VR: all 4 passes |
| --------------------------------------- | -----------------: | --------------------: |
| Completed / terminal PASS               |            66 / 66 |             132 / 132 |
| Task 2 PASS / FAIL / INCONCLUSIVE       |         66 / 0 / 0 |           132 / 0 / 0 |
| Switches with recovered failures        |             0 / 66 |               8 / 132 |
| Fidelity mismatch observations          |                  0 |                    16 |
| Vendor-failure stretch-eye observations |                  0 |                     8 |
| Producer retry occurrences              |                 19 |                    38 |
| Separate recovery applies / replays     |                  0 |                     0 |

Device loss, OOM, producer terminal failures, vendor-native qualification
failures and credible liveness timeouts were **each zero** in all six
passes. Observation counts are not crashes or unique frames.

The cumulative fidelity and fallback health gates failed in every
main-VR pass and passed in both baseline passes. The fixed two-frame
stretch cutoff remains diagnostic because settling imposes stretch.
The scaled-presentation gate after proven native output remains a
contract mismatch. Neither excluded gate is counted against health.

<details>
<summary>Each switch: baseline mean vs combined main-VR mean, with SE</summary>

All times are ms; negative change is faster. Each row uses two baseline
observations and four main-VR observations of that exact route.

| Row | Switch                           | Baseline mean | Main-VR mean (4 passes) |          Change | ±SE baseline | ±SE main-VR |
| --- | -------------------------------- | ------------: | ----------------------: | --------------: | -----------: | ----------: |
| 1   | DLSS Hoshipa → NONE              |         735.0 |                   702.5 |   -32.5 (-4.4%) |        ±38.7 |       ±14.6 |
| 2   | NONE → TAA                       |         174.7 |                   174.7 |    +0.0 (+0.0%) |        ±11.0 |        ±2.9 |
| 3   | TAA → DLAA                       |         296.1 |                   271.5 |   -24.7 (-8.3%) |        ±22.4 |        ±9.9 |
| 4   | DLAA → DLSS Hoshipa              |       1,018.5 |                   966.0 |   -52.4 (-5.1%) |        ±34.3 |       ±30.9 |
| 5   | DLSS Hoshipa → DLSS UQ           |       1,089.5 |                 1,044.9 |   -44.6 (-4.1%) |        ±46.0 |       ±78.0 |
| 6   | DLSS UQ → DLSS Quality           |       1,202.6 |                 1,121.2 |   -81.4 (-6.8%) |         ±4.0 |       ±32.1 |
| 7   | DLSS Quality → DLSS Balanced     |       1,255.1 |                 1,310.1 |   +55.0 (+4.4%) |        ±26.7 |       ±82.0 |
| 8   | DLSS Balanced → DLSS Performance |       1,398.4 |                 1,274.4 |  -124.1 (-8.9%) |       ±143.8 |       ±42.6 |
| 9   | DLSS Performance → DLSS UP       |       1,263.9 |                 1,242.7 |   -21.2 (-1.7%) |       ±111.2 |       ±56.3 |
| 10  | DLSS UP → DLAA                   |         903.4 |                   788.8 | -114.7 (-12.7%) |         ±6.6 |       ±42.7 |
| 11  | DLAA → TAA                       |         543.7 |                   462.1 |  -81.6 (-15.0%) |        ±87.0 |       ±12.8 |
| 12  | TAA → NONE                       |         169.7 |                   168.3 |    -1.5 (-0.9%) |         ±3.7 |        ±8.6 |
| 13  | NONE → FSR3 Native AA            |         691.7 |                   737.2 |   +45.4 (+6.6%) |        ±92.2 |       ±86.6 |
| 14  | FSR3 Native AA → FSR3 Hoshipa    |       1,448.6 |                 1,334.8 |  -113.8 (-7.9%) |        ±56.6 |       ±42.6 |
| 15  | FSR3 Hoshipa → FSR3 UQ           |         760.3 |                   805.0 |   +44.7 (+5.9%) |        ±19.0 |       ±49.7 |
| 16  | FSR3 UQ → FSR3 Quality           |         740.6 |                   786.5 |   +46.0 (+6.2%) |        ±36.6 |       ±62.6 |
| 17  | FSR3 Quality → FSR3 Balanced     |         766.2 |                   756.8 |    -9.5 (-1.2%) |        ±55.4 |       ±28.2 |
| 18  | FSR3 Balanced → FSR3 Performance |         722.9 |                   917.1 | +194.2 (+26.9%) |        ±63.4 |      ±164.8 |
| 19  | FSR3 Performance → FSR3 UP       |         721.1 |                   736.1 |   +15.0 (+2.1%) |         ±1.6 |       ±19.3 |
| 20  | FSR3 UP → FSR3 Native AA         |         868.7 |                   771.3 |  -97.4 (-11.2%) |       ±169.5 |       ±96.1 |
| 21  | FSR3 Native AA → TAA             |         454.9 |                   485.9 |   +31.0 (+6.8%) |        ±18.8 |       ±35.4 |
| 22  | TAA → NONE                       |         166.1 |                   179.9 |   +13.8 (+8.3%) |         ±0.8 |       ±13.2 |
| 23  | NONE → DLAA                      |         264.2 |                   254.5 |    -9.7 (-3.7%) |        ±17.8 |        ±8.7 |
| 24  | DLAA → FSR3 Native AA            |         967.1 |                 1,104.0 | +136.9 (+14.2%) |       ±134.4 |       ±47.8 |
| 25  | FSR3 Native AA → DLSS Hoshipa    |       2,119.5 |                 1,149.4 | -970.1 (-45.8%) |       ±117.5 |      ±170.7 |
| 26  | DLSS Hoshipa → FSR3 Hoshipa      |         949.1 |                 1,414.2 | +465.1 (+49.0%) |         ±7.4 |       ±71.2 |
| 27  | FSR3 Hoshipa → NONE              |         772.7 |                   767.3 |    -5.4 (-0.7%) |        ±26.2 |       ±59.8 |
| 28  | NONE → FSR3 UP                   |         930.5 |                   887.2 |   -43.3 (-4.7%) |        ±24.4 |       ±26.1 |
| 29  | FSR3 UP → DLSS UP                |       1,938.2 |                 1,705.0 | -233.1 (-12.0%) |        ±87.3 |      ±207.6 |
| 30  | DLSS UP → TAA                    |         788.6 |                   708.6 |  -80.0 (-10.1%) |         ±1.3 |       ±48.0 |
| 31  | TAA → FSR3 Native AA             |         621.1 |                   624.9 |    +3.7 (+0.6%) |        ±32.3 |       ±28.0 |
| 32  | FSR3 Native AA → NONE            |         490.0 |                   460.8 |   -29.2 (-6.0%) |        ±32.4 |       ±28.3 |
| 33  | NONE → DLAA                      |         298.3 |                   260.6 |  -37.7 (-12.6%) |         ±7.2 |       ±14.9 |

</details>

<details>
<summary>Exact builds, retained evidence and remaining limits</summary>

| Identity                | Older baseline                                                     | Main-VR repeat 1                                                   | Main-VR repeat 2                                                   |
| ----------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Compiled source         | `390fdea2533ae447849c5f07766c796872996a61`                         | `348803c1831c8cd71ceb75a58143ad0bcb00abb2`                         | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         |
| Renderer source/base    | `1595cffd6a0a770020074c3d5481d45502df58a7`                         | `348803c1831c8cd71ceb75a58143ad0bcb00abb2`                         | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         |
| Main-VR base/equivalent | `9074b4676aa708ae32c26295dfd69cc8696d5efa`                         | `348803c1831c8cd71ceb75a58143ad0bcb00abb2`                         | `7c8e3e6568972f0dfbae83d60e3b6d2cbd68bc99`                         |
| Build ID                | `d20962d1f8b75609f7b97074d1ae8132450d61b477be793227ad88088f5bda0d` | `07238b1fe36024d2c33081f023973c34887ce8a0fa4bd4743bf8a1c85b5ec78d` | `9e88a559cc66883614a83f797d32b1c4bf12b412e5f853c0606252e5957c08e4` |
| Run ID                  | `renderscale-tuning-nvidia-20260910T090649592Z`                    | `nvidia-2026-09-10T07-19-50-405Z`                                  | `nvidia-20260910T124329625Z`                                       |

The old baseline’s renderer matches main-VR `9074b467`; compilation at
`390fdea25` adds the tuning-preparation bridge to renderer `1595cffd`.
Both main-VR repeats include PR65 merge `a4cb755c3`; the second adds
DevBench retry telemetry. They do not measure the original PR head
`69467a0c` in isolation.

All runs used the RTX 5070 Ti Laptop GPU, Dragonsreach position/weather,
1512 × 1680 output per eye, DLSS K, explicit FSR3, and foveation
0.3/0.3/0.7. Each pass followed the same 33 routes with five-second
pre-dispatch pacing and a 20-second strict deadline. Producer timings
exclude that pacing. Physical DLL, manifest and AIO evidence are retained.

Game hour was 9.532 / 1.958 / 9.532 for baseline / repeat 1 / repeat 2.
Complete modlist/cache, driver, headset refresh and power equivalence
are not proven. No versioned tolerance policy was supplied. Memory is
inconclusive; fresh resolved GPU timer samples are unavailable, so these
are switch-latency results rather than FPS or steady-state GPU costs.

Detailed retry diagnostics are complete only for repeat 2. The older
runs retain retry counts but lack per-retry timing details; their old
COMPLETE labels predate that reporting contract. These tuning runs do
not claim the separate `csx-render-scale-pr-v1` qualification.

[Original baseline comparison](https://github.com/ParticleTroned/skyrim-community-shaders/blob/a4eabdf04b93229207f637c1239ff4553ce0a4c6/docs/development/nvidia-renderscale-baseline-vs-head-20260910.md),
[independent repeat evidence](https://github.com/ParticleTroned/skyrim-community-shaders/blob/8e2526281bc969a0a70493672183412eea9f99aa/docs/development/nvidia-renderscale-tuning-7c8e3e656-20260910.md), and
[canonical ledger](https://github.com/ParticleTroned/skyrim-community-shaders/blob/8e2526281bc969a0a70493672183412eea9f99aa/docs/development/vr-render-scale-comparison-ledger.csv)
retain every original pass and transition. This presentation adds no
measurement or ledger column and changes no historical timing cell.
All **1,584 distinct numeric timing cells** across the three runs were
audited. Full raw evidence remains local.

</details>

<!-- end pr65-baseline-repeat-means-v1 -->
