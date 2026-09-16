# RC166: all three repeats against the culling campaign

[Full eleven-run values and deltas](tables.md). That table retains Balanced 1 as its numerical reference; it is not the older historical baseline.

All six holds completed in each repeat. All three compiled sources are `2eef86720e5c6a48e6fa9ad93506a1f2093d0f2f`, DLL SHA-256 `C0436272458A865FA43E501DB5D231EF2E457417356C9C8A4C7594FA2976AD18`. The producer describes RC166-68-g2eef86720; RC166 is the user campaign label, not a claim that this source is the exact RC166 tag.

The original Streamline 2.13.0 / DLSS 310.8.0 binaries were retained. The main-VR culling runs use source 503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc and Streamline 2.14.1 / DLSS 310.9.1. These are whole-build comparisons.

## CPU means, final ten seconds (ms)

| Mode/save              | Main-VR Legacy 1 | Main-VR Legacy 2 | RC166 R1 | RC166 R2 | RC166 R3 | RC166 median | Repeat range | Median delta vs Legacy 2 |
| ---------------------- | ---------------: | ---------------: | -------: | -------: | -------: | -----------: | -----------: | -----------------------: |
| DLAA 08                |            5.477 |            5.768 |    7.526 |    7.770 |    7.380 |        7.526 |        0.390 |                   +1.758 |
| DLAA 09                |            5.804 |            5.919 |    6.727 |    6.417 |    6.542 |        6.542 |        0.310 |                   +0.623 |
| DLAA 10                |            7.184 |            5.935 |    7.654 |    7.235 |    7.184 |        7.235 |        0.470 |                   +1.300 |
| DLSS + render-scale 11 |            8.687 |            8.238 |    9.005 |    8.224 |    8.046 |        8.224 |        0.959 |                   -0.014 |
| DLSS + render-scale 12 |            8.467 |            9.048 |    8.503 |    6.705 |    8.007 |        8.007 |        1.798 |                   -1.041 |
| DLSS + render-scale 13 |           13.199 |           18.174 |   17.027 |   11.466 |   11.025 |       11.466 |        6.002 |                   -6.708 |

## GPU means, final ten seconds (ms)

| Mode/save              | Main-VR Legacy 1 | Main-VR Legacy 2 | RC166 R1 | RC166 R2 | RC166 R3 | RC166 median | Repeat range | Median delta vs Legacy 2 |
| ---------------------- | ---------------: | ---------------: | -------: | -------: | -------: | -----------: | -----------: | -----------------------: |
| DLAA 08                |            9.652 |            9.828 |    9.341 |    9.385 |    9.356 |        9.356 |        0.044 |                   -0.472 |
| DLAA 09                |            8.576 |            7.868 |    7.792 |    7.778 |    7.746 |        7.778 |        0.046 |                   -0.090 |
| DLAA 10                |            9.205 |            8.055 |    8.837 |    8.626 |    8.639 |        8.639 |        0.211 |                   +0.584 |
| DLSS + render-scale 11 |            8.758 |            8.082 |    8.523 |    8.122 |    8.014 |        8.122 |        0.509 |                   +0.040 |
| DLSS + render-scale 12 |            9.250 |            8.866 |    8.849 |    9.267 |    9.005 |        9.005 |        0.418 |                   +0.139 |
| DLSS + render-scale 13 |           12.686 |           12.429 |   12.595 |   11.726 |   11.818 |       11.818 |        0.869 |                   -0.611 |

## Assessment

-   RC166 is not uniformly faster or quieter. Relative to main-VR Legacy 2, all three DLAA saves have higher CPU means in all three RC166 repeats; CPU P95/P99 are also higher. DLAA GPU differences are scene-dependent: save 08 is faster, save 09 is approximately neutral, save 10 is slower.

-   DLSS save 11 crosses from slower to faster across repeats. Save 12 has lower CPU means than Legacy 2 in all repeats but a large repeat range; its GPU is not consistently better. Save 13 has lower CPU means than Legacy 2 in all repeats, yet spans 6.002 ms and overlaps the earlier main-VR Legacy 1 result. Its lower later-repeat GPU times do not establish a source-level gain.

-   CPU 0.35 ms / GPU 0.15 ms remain descriptive variation allowances requested by the user. Several observed repeat ranges exceed them; they are not confidence intervals or a guarantee that larger differences are causal. Do not pool different saves into mean plus standard error.

-   Fewer isolated spikes or spike groups alone do not establish smoother execution. Compare their duration/duty and P95/P99 alongside averages: a slower sustained interval may contain fewer separated threshold crossings. The saved thresholds and group definitions were not changed.

-   The depth-off result and the main-VR culling comparison still support keeping culling enabled. Balanced remains the practical default; these RC166 whole-build results do not prove Legacy or Performance is the fastest safe policy. Culling is one contributor, not a complete explanation of CPU/DLSS costs.

-   Source qualification: the verified 2eef86720 source enables D3D11 multithread protection through Hooks.cpp:940 and Utils/D3DContextProtection.cpp:75. UnifiedWater/Flowmap.cpp:199 can later disable it. It therefore is not an always-unprotected reference, even though the preserved PR93 merge is not its ancestor. Source inspection does not establish the protection state or its runtime cost during these holds.

## What WPR adds to the timing comparison

The [side-by-side WPR tables](wpr-comparison.md) and
[frame cadence](tail-cadence.csv) explain why the fpsVR means cannot be
read as exclusive CPU-work costs. Against main-VR Legacy 2:

-   Save 08 has lower GPU time and roughly 66–67 recorded FPS versus 60,
    despite its higher fpsVR CPU mean. Render-thread execution per frame is
    0.538–0.967 ms lower across the RC166 repeats.
-   Save 10 goes the other way: later RC166 repeats record about 95 FPS
    versus 104, with higher CPU/GPU means and about 1 ms more render-thread
    execution per frame. Native Skyrim leaf execution accounts for much of
    that difference; this does not identify a particular source change.
-   Save 12 repeat 2's low 6.705 ms CPU mean is not a throughput win:
    recorded FPS falls from 91 to 67, render-thread execution rises from
    10.475 to 12.930 ms per frame, and waiting rises from 0.419 to 1.815 ms.
-   Save 13 repeats 2/3 reduce the fpsVR CPU mean by 6.708/7.149 ms, but
    render-thread execution falls by 1.108/1.332 ms per frame. Recorded FPS
    rises only from 57 to 59/59.5. The earlier main-VR Legacy 1 result is
    another reminder that this workload varies substantially across runs.
-   Ready-delay differences are small compared with the multi-millisecond
    CPU-mean swings. No D3D11 lock-execution samples were identified in any
    of the eighteen RC166 tails under the preserved classifier. This does
    not establish the protection state throughout a run.

These findings keep execution cost, frame pacing and off-thread activity
as separate investigation targets. They do not isolate culling or vendor
DLLs as the cause. The material-guard candidate committed as `6588831aa`
was not loaded in these measurements; no saving from it is claimed.

## Health and comparability limits

The existing 49-second, 59-second and stop receipts confirm the requested DLAA/native modes for 08/09/10 and DLSS q1 with 0.85 render scale for 11/12/13, with valid applied profiles: 54 profile checks match across the three repeats. The older build lacks modern strict stretch, stereo-cycle and cleanup lifecycle evidence: full health remains unavailable, not passed. Legacy controller wrappers report an unsupported semantic-outcome contract while retaining read-only producer data; these wrapper failures are preserved, not converted to passes.

RC166 uses the explicitly authorized fresh-load/player-loaded/menu-clear boundary, not the modern completed-world-frame marker. The 60-second holds and final [50,60) calculations are unchanged; boundary equivalence is not proven. Legacy profiler status requests one capture before and after all holds. Effective feature snapshots and direct culling counters are unavailable. Save/co-save identities match; exact HMD pose and uncontrolled process load remain confounds.

WPR recordings completed and are archived with matching byte lengths and SHA-256. Derived WPR stack, ready and wait records are preserved in ledger 0005; inspect each window coverage status before interpreting scheduler totals. Inclusive stack categories overlap and do not measure exclusive hook cost.

[Complete ledger 0005](../vr-render-scale-ledger-0005-investigation.csv) preserves every prior 0004 cell plus these three repeats. [Coverage](coverage.json) records exact source hashes and reconstruction.
