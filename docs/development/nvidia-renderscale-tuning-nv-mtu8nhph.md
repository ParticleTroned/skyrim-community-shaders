# NVIDIA render-scale comparison: nv-mtu8nhph

The complete first pass averaged 794.846 ms per strict transition,
69.06% faster than the preceding same-build run
and +0.97% versus the earlier September 9 baseline.
All 42 retained transitions passed rendering and per-row Task 2 checks.
Execution is INTERRUPTED and reporting INCOMPLETE: pass 2 ended after row 9,
and its last DLSS trace window was not fully assembled before interruption.
The remaining 24 destinations are NOT RUN.

These are dispatch-to-strict transition latencies, not steady-state frame
times. The current and preceding runs share clean source 89e396458 and
Build ID 880b0ef46dd92fc519bc53f577a21d2986b438133952822f350f18d11360169e.
The earlier baseline used dirty source f2a73cd950e98cd27eadd34502dbeea486dce481.
All three used Dragonsreach, 1512x1680 output per eye, DLSS K, FSR3, and
the 0.3/0.3/0.7 fixture. Different memory conditions and baseline provenance
prevent causal attribution to the code change alone.

| Pass 1, identical 33 destinations | Earlier baseline | Previous same build |  Current |
| --------------------------------- | ---------------: | ------------------: | -------: |
| Strict mean ms                    |          787.217 |            2569.260 |  794.846 |
| Strict worst ms                   |         1447.129 |           16264.205 | 1631.113 |
| Retained pass 1 rows              |               33 |                  33 |       33 |
| Retained pass 2 rows              |               24 |                   0 |        9 |

The matched first nine destinations average 904.209 ms
in current pass 1 and 898.537 ms in pass 2 (-0.63%).
The earlier baseline's same nine pass-2 destinations average
852.130 ms (+5.45% current versus baseline).
Do not compare the nine-row repeat mean directly with a 33-row pass mean.
Both current DLSS Hoshipa setup baselines satisfied strict qualification;
the previous same-build attempt stopped at its pass-2 baseline timeout.

## Evidence

Complete per-row timing, cleanup, anomaly, and memory comparison:
`artifacts/renderscale-tuning/nv-mtu8nhph/comparison.md`.
Generated classification report: `artifacts/renderscale-tuning/nv-mtu8nhph/report.md`.
All captures were verified inactive after guarded cleanup. Memory result:
`repeat_not_completed`; no leak or retention conclusion is available.
