# Hi-Z refinement comparison

Guarded 2x2 with original-depth refinement ON remains slower than Advanced:
11.357 / 8.774 ms CPU/GPU versus 9.977 / 7.763 ms. The GPU difference is
1.011 ms (13.02%); CPU is 1.380 ms (13.83%). CPU and GPU overlap and must
not be added. Refinement OFF makes the culling shader slightly cheaper,
but the whole-frame GPU result is 0.268 ms worse than ON. Keep ON.

Measured implementation: `b949dad8185a660e192c355e6b6fa02a2d512194`.
The AIO was built before that commit, as requested: its producer records
source `605fcf65e` plus the dirty implementation snapshot, not a clean
b949dad81 compile. Producer Build ID:
`495a513ef59af08f4d2ab05fd5c526aa6848447cc792cd785d417f7f5b993110`.
The installed DLL and adjacent manifest match the preserved AIO receipt.

## Final controlled comparison

Skyrim VR, WhiterunExterior01, SkyrimClearTU, DLSS Quality K, render-scale
ON, 1344x1492 render and 2016x2240 display pixels per eye. Every phase
resets noon and settles for five seconds. Frame-window boundary hours
were 12.028--12.205. Position, weather, profile and source-compilation
counters remained unchanged. Observed boundary pose variation was at most
0.498 mm / 0.0414 degrees. No compiler or packager activity was recorded.

The selected six 20-second fpsVR windows followed the user's request to
repeat after a possibly open window. The first six windows remain in the
raw evidence but are excluded from this comparison. Selected order:
Advanced, ON, OFF, Advanced, OFF, ON. Diagnostics, telemetry and the CSX
profiler were OFF. Every complete window receives equal weight.

| Mode                | CPU ms | GPU ms | GPU window range | Samples |
| ------------------- | -----: | -----: | ---------------: | ------: |
| Advanced            |  9.977 |  7.763 |     7.609--7.916 |   2,959 |
| Hi-Z refinement ON  | 11.357 |  8.774 |     8.618--8.929 |   2,738 |
| Hi-Z refinement OFF | 11.868 |  9.042 |     8.994--9.089 |   2,581 |

CPU window ranges were 9.750--10.203, 11.072--11.641 and 11.627--12.109
ms respectively. Raw p50/p95/p99 and all samples are retained in
`frame-analysis.json`; baseline drift remains part of the result. fpsVR
did not expose refresh rate, so no observed-headset-budget percentage is
claimed. A different process/view prevents a controlled cross-build
speedup claim against earlier reports.

## Where GPU time goes

Six separate complete 300-frame captures used normal shaders, telemetry
ON, traversal/matched diagnostics OFF. The profiler reports GPU self
time; only independent culling scopes are summed. Table entries are the
equal-capture means. All native timer histories are retained.

| Culling scope          | Advanced ms | Hi-Z ON ms | Hi-Z OFF ms |
| ---------------------- | ----------: | ---------: | ----------: |
| Native downscale       |    0.035759 |         -- |          -- |
| Native bounds producer |    0.075364 |         -- |          -- |
| Hierarchy setup        |          -- |   0.000678 |    0.000647 |
| Visibility setup       |          -- |   0.005142 |    0.004999 |
| Base depth             |          -- |   0.023504 |    0.024044 |
| Mip reduction          |          -- |   0.033062 |    0.032246 |
| Bounds testing         |          -- |   0.553424 |    0.507211 |
| Result copy            |          -- |   0.004844 |    0.004804 |
| Total                  |    0.111123 |   0.620654 |    0.573951 |

Advanced's total varied substantially: 0.080698--0.141548 ms. Hi-Z ON
was 0.611798--0.629510 ms; OFF was 0.564021--0.583882 ms. Retain both
Advanced captures instead of silently selecting the faster baseline.
Bounds testing accounts for 89.17% of ON culling. Non-bounds work is
already 0.067230 ms, near the faster Advanced total.

Refinement adds approximately 0.0467 ms to the culling pass while the
whole frame is 0.2680 ms faster in the selected ON windows. These are
separate captures: the observations support keeping refinement, but do
not establish an exact rendering-time decomposition. Inclusive CPU
prepare/dispatch/readback timers are retained in `analysis.json`; they
must not be added into a CPU total. No hardware occupancy or spill
measurement was obtained.

Frozen normal-shader cohorts rejected 62.17--62.60% for Advanced,
39.52--39.63% for ON and 38.11--39.27% for OFF. Each batch contains 4,096
candidate records. These cohorts span timer collection too, rather than
only the 300 captured frames. There were no normal Hi-Z fallbacks,
invalidated histories, unreadable batches, pipeline rebuilds or pyramid
allocations. Engine culling was enabled with minimum extent 10.

The source remained standard-Z R24 depth, 2688x1492, one sample, two
1344x1492 eye rectangles. Active reduction was 2; each padded pyramid
layer was 1024x1024 with ten mips. Both layers total 11,184,800 logical
bytes, not measured driver VRAM. Zero-is-untrusted handling, two-pixel
guard, depth bias 8/16777216, region roundoff margin 1/32 pixel,
interpolation allowance 64/16777216 and 64 depth reads per eye remain.

## Matched outcomes explain the culling gap

The repaired shadow diagnostic accepted 523 ON batches / 2,142,208
records and 537 OFF batches / 2,199,552 records. Both had zero drops,
failed batches, not-ready polls or snapshot publication misses. One
in-flight submission at each freeze is outside the accepted cohort; it
was cleared when diagnostics were disabled. Before/after Advanced
recovery outcomes were identical. These are same-record comparisons
within each condition, not cross-toggle matches or unique draw counts.

| Matched result (% candidates)        |     ON |    OFF |
| ------------------------------------ | -----: | -----: |
| Native hidden                        | 62.394 | 61.918 |
| Hi-Z hidden                          | 40.192 | 38.299 |
| Native-only hidden                   | 22.589 | 24.001 |
| Hi-Z-only hidden                     |  0.387 |  0.382 |
| Net rejection gap, percentage points | 22.203 | 23.619 |

ON's native-only hidden records have these decisive Hi-Z retention
reasons. Percentages use all 2,142,208 matched candidates:

| Reason                          | Records | % candidates |
| ------------------------------- | ------: | -----------: |
| Wholly offscreen                | 189,466 |        8.844 |
| Original bounds cross viewport  |  72,961 |        3.406 |
| Clip/near/far-plane crossing    |  64,304 |        3.002 |
| Expanded guard crosses viewport |      55 |        0.003 |
| Finest depth unresolved         | 103,205 |        4.818 |
| Nearest witness unresolved      |  53,232 |        2.485 |
| Read budget exhausted           |     690 |        0.032 |

Viewport/clip cases are 67.53% of native-only misses, or 15.255% of all
records. Wholly offscreen proxies belong to native frustum rejection;
their visibility bits do not imply extra useful draws. Partial viewport
and clip-plane handling are real capability targets but require a
conservative per-eye clipped bound, not treating missing depth as hidden.

The remaining in-view depth/witness/budget cases are 7.335% of all
records. Increasing the global budget targets only 0.032% of matched
candidates and would increase shader work. The 0.387% Hi-Z-only cohort
does not by itself prove either correct extra culling or a visual defect;
motion and visual qualification remain necessary. No pictures were taken
because performance was not similar.

## Remaining triangle and clipping work

Separate six-second traversal windows collected 460 ON batches /
1,884,160 records and 426 OFF batches / 1,744,896 records. Work sums both
tested eyes. Diagnostic shader timing is not used for performance.

| Work per candidate           |     ON |    OFF |
| ---------------------------- | -----: | -----: |
| Depth loads                  | 13.599 | 11.704 |
| Face-region tests            |  4.887 |  3.941 |
| Triangle visits              |  9.276 |  7.407 |
| Plane builds                 |  2.240 |  1.810 |
| Plane reuses                 |  2.869 |  2.124 |
| Successful plane proofs      |  1.775 |  1.341 |
| Triangle/rectangle attempts  |  3.334 |  2.593 |
| Disjoint triangles skipped   |  1.715 |  1.332 |
| Polygon clip entries         |  1.619 |  1.262 |
| Executed clipping planes     |  4.356 |  3.358 |
| Skipped clipping planes      |  2.122 |  1.688 |
| Clip vertex-loop visits      | 28.354 | 22.107 |
| Expanded reduced-depth cells |  0.361 |      0 |
| Source pixels                |  1.108 |      0 |
| Source-witness reads         |  0.108 |      0 |
| Resolved expanded cells      |  0.077 |      0 |

The separating-edge test skips 51.43% of reached triangle/rectangle
pairs. Only 299 of 3,051,254 remaining ON clips are empty: 0.0098%.
Additional disjointness tests have little remaining opportunity.
Surviving clips average 17.51 vertex-loop visits, including containment
checks and final depth reduction. Lazy plane reuse is 56.16%; contained
plane skips are 32.75%. These work-event ratios are not isolated speedups.
Only 21.19% of expanded cells resolve; refinement-associated region work
is included in the totals rather than separately timed.

## Next focused optimization

1. Keep guarded 2x2 and refinement ON. Target the 0.553 ms bound tester:
   investigate direct conservative depth extrema over a triangle/rectangle
   intersection, using contained vertices, rectangle corners and edge
   crossings to avoid alternating polygon storage and repeated clipping.
   Preserve strict error bounds and the existing clipper for uncertain
   cases. Qualify against the current pixel/ray and double-precision
   oracles, then perform a DevBench A/B; a gain is not yet established.
2. Investigate reuse of triangle edge equations alongside the existing
   lazy plane cache. Cache only reached triangles, and compare compiled
   storage/register behavior: larger indexed arrays can lose the saving.
3. Address missed culling with conservative viewport/clip handling and
   the 4.818% finest-depth / 2.485% witness matched cohorts. Validate
   source/guard coverage and temporal/stereo coherence before relaxing
   any retention rule. Avoid a global depth-read-budget increase.

This campaign qualifies the repaired diagnostic and measures refinement;
it does not isolate the separating-edge optimization against its previous
implementation. Advanced, refinement ON, telemetry ON and noon were
restored; profiler/traversal/matched diagnostics are OFF with no pending
matched slots. Skyrim remains running. Logging was unchanged.

Complete raw evidence, normalizers, owned-capture stop receipts, guard
results and parameters remain under
`build/astra-runtime/20261004T154226Z-hiz-refinement-ab`; the complete
derived record is `analysis.json`. Motion/lifecycle and SE/AE runtime
qualification remain open. This report changes no executable code.
