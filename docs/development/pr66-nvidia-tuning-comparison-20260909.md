# PR66 comparison with preceding NVIDIA attempts

No regression has been causally attributed to PR66. The candidate run is
materially slower and incomplete, but its six large timing outliers and its
second-baseline failure have direct evidence of system-commit admission
waits. This supports an environmental pressure explanation; it does not
establish that PR66 is regression-free.

## Build and comparison scope

The candidate is clean source `89e3964582730b92c1885eaef8f865e2d1d02ea9`,
a local merge of `348803c18` and PR66 head `85ee7d4a5`. The immediately
preceding run used dirty source `f2a73cd95`. Both used Dragonsreach,
1512x1680 per eye, DLSS K and explicit FSR3. The runner was updated between
runs, including evidence retention and native proof validation. Dirty
baseline provenance and different system-memory conditions prevent a
controlled attribution to the PR alone.

The exact PR66 merge diff changes full-eye source bounds in the upscaling
encoder, populates those bounds in three C++ paths, replaces padding in the
existing 64-byte constant buffer, and adds shader tests/CI coverage. It does
not modify memory admission, allocation estimates, retirement, or retry
cadence. The extra integration-base commit changes tuning preparation.
These are source-inspection findings, not new build or shader-test results.

## Comparison

| Observation                                   | Sep 3, nvidia-mtlid7m3            | Earlier Sep 9, nvidia-mtu2u3dm                           | PR66, nvidia-mtu6cpo8                 |
| --------------------------------------------- | --------------------------------- | -------------------------------------------------------- | ------------------------------------- |
| Render completion                             | 66/66 PASS                        | 66/66 reported live; 57 full receipts retained, all PASS | 33/33 dispatched PASS; repeat not run |
| Pass 1 mean strict transition                 | Numeric timing unavailable        | 787.217 ms                                               | 2569.260 ms                           |
| Pass 1 worst strict transition                | Numeric timing unavailable        | 1447.129 ms                                              | 16264.205 ms                          |
| Pass 1 private-memory growth                  | +1202.367 MiB                     | +155.617 MiB                                             | +417.285 MiB                          |
| Pass 1 system-commit growth                   | Not compared                      | +150.254 MiB                                             | +3855.988 MiB                         |
| Second pass                                   | Completed                         | Completed live; last 9 row receipts missing              | Baseline timed out at 20001.565 ms    |
| Device loss / OOM / terminal provider failure | 0                                 | 0 in retained evidence; none reported live               | 0                                     |
| Task 2                                        | 60 PASS / 0 FAIL / 6 inconclusive | Corrected offline: 57 retained PASS                      | 33 PASS / 0 FAIL / 0 inconclusive     |
| Memory conclusion                             | Inconclusive                      | Inconclusive                                             | Repeat not completed                  |

The September 3 run used 2468x2740 per eye. Its memory values are historical
context, not comparable performance evidence at the current resolution.
The September 2 attempt had render FAIL and 24 terminal producer failures;
the early September 3 attempt rendered successfully but retained three
Task 2 failures. Those historical failures are not observed in the 33
completed PR66 rows. The updated validator also means Task 2 counts cannot
be interpreted as runtime improvements by themselves.

## Why the latest timing is worse

First-pass mean latency rose 3.264 times. Rows 16, 17, 18, 25, 29 and 31
account for 98.99% of the summed increase. Each has a matching runtime plan
with `systemDeferred=yes`, at epochs 18, 19, 20, 27, 31 and 33 respectively.
Most of their latency occurs before the first physical mutation.
Excluding those six identified pressure-wait rows, the remaining 27 rows
average 751.101 ms previously and 773.121 ms now, a 2.93% increase. This
subset is diagnostic only; it does not replace the full-run result.

System commit began at 52602.707 MiB in the earlier September 9 pass and
54680.234 MiB in the PR66 pass; at pass end it was 52752.961 versus
58536.223 MiB. Skyrim private memory ended at 16936.539 versus
17070.754 MiB, only 134.215 MiB apart. During the failed baseline, system
commit later fell by about 5 GiB while Skyrim remained near 16928 MiB,
after which the operation completed. The external/system allocation owner
was not captured, so this is not attributed to a particular process.

All timing values below are recomputed directly from the 33 original
pass-1 waiter JSON receipts for each run, matched by ordinal and target.
These are transition latencies, not steady-state GPU frame times.

| Row | Destination           | Previous strict ms | PR66 strict ms | System-commit wait confirmed |
| --: | --------------------- | -----------------: | -------------: | ---------------------------- |
|   1 | none quality 0 native |            693.477 |        724.601 | Not asserted                 |
|   2 | taa quality 0 native  |            160.716 |        170.679 | Not asserted                 |
|   3 | dlss quality 0 native |            252.047 |        266.449 | Not asserted                 |
|   4 | dlss quality 1 scaled |            929.545 |        984.174 | Not asserted                 |
|   5 | dlss quality 2 scaled |           1165.858 |       1172.372 | Not asserted                 |
|   6 | dlss quality 3 scaled |           1129.215 |       1169.762 | Not asserted                 |
|   7 | dlss quality 4 scaled |           1311.420 |       1481.087 | Not asserted                 |
|   8 | dlss quality 5 scaled |           1341.494 |       1431.501 | Not asserted                 |
|   9 | dlss quality 6 scaled |           1262.846 |       1391.591 | Not asserted                 |
|  10 | dlss quality 0 native |            777.340 |        785.256 | Not asserted                 |
|  11 | taa quality 0 native  |            427.240 |        441.566 | Not asserted                 |
|  12 | none quality 0 native |            163.985 |        176.340 | Not asserted                 |
|  13 | fsr quality 0 native  |           1386.810 |        764.332 | Not asserted                 |
|  14 | fsr quality 1 scaled  |            865.969 |        913.319 | Not asserted                 |
|  15 | fsr quality 2 scaled  |            819.370 |        712.979 | Not asserted                 |
|  16 | fsr quality 3 scaled  |            806.393 |       7728.934 | Yes                          |
|  17 | fsr quality 4 scaled  |            713.672 |       8404.336 | Yes                          |
|  18 | fsr quality 5 scaled  |            712.746 |       8012.362 | Yes                          |
|  19 | fsr quality 6 scaled  |            724.159 |       1198.697 | Not asserted                 |
|  20 | fsr quality 0 native  |           1005.280 |        728.326 | Not asserted                 |
|  21 | taa quality 0 native  |            422.430 |        482.464 | Not asserted                 |
|  22 | none quality 0 native |            181.514 |        178.667 | Not asserted                 |
|  23 | dlss quality 0 native |            240.211 |        272.785 | Not asserted                 |
|  24 | fsr quality 0 native  |            733.775 |        885.426 | Not asserted                 |
|  25 | dlss quality 1 scaled |           1447.129 |       8989.652 | Yes                          |
|  26 | fsr quality 1 scaled  |           1368.140 |        990.474 | Not asserted                 |
|  27 | none quality 0 native |            702.388 |        854.050 | Not asserted                 |
|  28 | fsr quality 6 scaled  |            864.806 |       1037.740 | Not asserted                 |
|  29 | dlss quality 6 scaled |           1429.611 |      16264.205 | Yes                          |
|  30 | taa quality 0 native  |            684.849 |        882.064 | Not asserted                 |
|  31 | fsr quality 0 native  |            588.876 |      14511.810 | Yes                          |
|  32 | none quality 0 native |            407.674 |        488.787 | Not asserted                 |
|  33 | dlss quality 0 native |            257.162 |        288.779 | Not asserted                 |

## Verdict and limits

PR66 has no confirmed rendering correctness regression in the completed
matrix rows. The observed slowdown and stop are explained at the controller
level by memory-admission waits, using unchanged safety policy. Increased
Skyrim-private growth remains an observation, not a demonstrated leak or
PR66 regression; the required second-pass memory boundary is absent.

A same-resolution, matched-memory pre/post run with complete repeats and
steady-state GPU/visual evidence is still required to assess PR66's shader
cost and visual behavior conclusively. The present assay has no independent
image review capable of proving the eye-seam fix or excluding visual
artifacts. No PR merge, runtime mutation, page-file change, or new assay was
performed during this comparison.

Sources: `nvidia-renderscale-tuning-20260909.md`,
`nvidia-renderscale-tuning-nvidia-mtu6cpo8.md`,
`vr-render-scale-comparison-ledger.csv`, the exact raw receipts in both
local run bundles, and the inspected merge diff. Extracted comparison rows
and the pressure plans are preserved in
`artifacts/renderscale-tuning/nvidia-mtu6cpo8-diagnosis/` as
`pr66-comparison-rows.json` and `pr66-delayed-transition-plans.log`.
