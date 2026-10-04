# Hi-Z refinement comparison

The combined contained-plane, lazy-plane and original-depth refinement
build remains slower than Advanced. Four noon-reset frame windows give
10.132 / 7.852 ms CPU/GPU for Advanced and 11.475 / 9.021 ms for Hi-Z:
an additional 1.343 ms CPU (13.26%) and 1.169 ms GPU (14.88%). CPU and
GPU overlap; these differences must not be added.

Implementation snapshot: `605fcf65e3f9727a2400adc848668061e06c5fc0`.
The archive preceded that commit, as requested. Its compiled manifest
records `cdaa15649ce5449ca6911dd65d6f158103c58714` plus the validated
dirty implementation snapshot, rather than claiming to compile commit
605fcf65. Producer Build ID:
`a59c7e2411cfecf684a94f0c548e0477cb0528dbd8a62cc5342cadec63101017`.

## Controlled comparison

Skyrim VR, WhiterunExterior01, SkyrimClearTU, DLSS Quality K, render-scale
ON, 1344x1492 render pixels and 2016x2240 display pixels per eye. Every
condition resets game hour to 12 and settles for five seconds. Position,
weather, profile and source-compilation counters remained unchanged
within the campaign. Observed frame-window boundary HMD variation from
the first reference was at most 0.504 mm / 0.0628 degrees.

Whole-frame measurements used two 20-second fpsVR windows per method,
with telemetry, traversal, matched diagnostics and the CSX profiler OFF.
The mean weights each complete window equally. The guarded collectors
found no compiler/packager activity, owned and stopped their recordings,
and retained the complete raw samples. Logging settings were unchanged.

| Window | Method   | CPU ms | GPU ms | Samples |
| ------ | -------- | -----: | -----: | ------: |
| r1     | Advanced | 10.037 |  7.771 |    1566 |
| r2     | Hi-Z     | 11.453 |  9.009 |    1424 |
| r3     | Advanced | 10.226 |  7.934 |    1485 |
| r4     | Hi-Z     | 11.497 |  9.033 |    1403 |

Three separate complete 300-frame GPU captures used normal shaders,
telemetry ON, traversal/matched diagnostics OFF. Profiler times are GPU
self time. Only independent culling scopes are summed. Every controller
call passed the registered performance-neutrality guard and retained its
cleanup receipt. The repeated Hi-Z capture measures substantial timer
variation; report both results rather than selecting the faster one.

| Culling scope          | Advanced ms | Hi-Z first ms | Hi-Z repeat ms |
| ---------------------- | ----------: | ------------: | -------------: |
| Native downscale       |    0.028747 |             — |              — |
| Native bounds producer |    0.049383 |             — |              — |
| Hierarchy setup        |           — |      0.000649 |       0.000647 |
| Visibility setup       |           — |      0.004947 |       0.005600 |
| Base depth             |           — |      0.025863 |       0.022663 |
| Mip reduction          |           — |      0.041079 |       0.034348 |
| Bounds testing         |           — |      0.640380 |       0.555206 |
| Result copy            |           — |      0.011068 |       0.004544 |
| Total                  |    0.078130 |      0.723988 |       0.623008 |

Bounds testing accounts for 88.5–89.1% of Hi-Z culling. Mean culling cost
is 0.673498 ms, 8.62 times Advanced. The remaining hierarchy/copy work
already costs 0.067802–0.083607 ms, close to Advanced's entire producer.
Producer parity therefore needs a large change to bounds testing and
possibly hierarchy overhead, rather than another small arithmetic saving.

CPU telemetry means in the first normal capture were 3.566 us prepare,
22.210 us dispatch, 10.630 us Hi-Z validation and 4.528 us native staging
readback. Advanced producer/readback means were 3.036 / 2.897 us. These
are inclusive CPU scopes and must not be summed. They do not explain the
1.343 ms whole-frame CPU difference by themselves; engine drawing and
GPU/CPU synchronization remain unmeasured contributors.

## Culling outcomes

Frozen normal-shader counter cohorts contain repeated candidate records,
not unique scene objects, triangles or draw calls. Counters span capture
and collection, rather than only the 300 profiled frames.

Advanced hid 2,621,840 of 4,337,664 records over 1,059 batches: 60.444%.
Before/after recovery counts were identical in this stationary cohort.
Hi-Z first hid 1,483,235 of 3,616,768 records over 883 batches: 41.010%.
The Hi-Z repeat hid 1,920,131 of 4,608,000 records over 1,125 batches:
41.670%. Both methods tested 4,096 records per batch.
The aggregate gap is 18.77–19.43 percentage points. No normal Hi-Z
fallback, rejected/unreadable history, pipeline rebuild or allocation was
recorded. Engine culling was enabled with minimum extent 10.

The source was standard-Z R24 depth, 2688x1492, one sample, two
1344x1492 eye rectangles. Preferred/active reduction was 2. Each eye's
padded pyramid was 1024x1024 with ten mips; both layers occupy 11,184,800
logical bytes. This is texture accounting, not a driver VRAM measurement.
The zero-is-untrusted mask policy, two-pixel spatial guard and depth bias
8/16777216 remain active. Region tests add a 1/32-pixel roundoff margin
and a 64/16777216 interpolation-depth allowance. Pyramid/source reads
share 64 reads per eye.

## What the work counters establish

A separate six-second traversal window collected 492 complete batches,
2,015,232 records. Work totals sum both tested eyes. One additional
accepted visibility batch is outside this traversal cohort; retention
shares below use only the complete traversal records.

| Work event                   |      Total | Per candidate |
| ---------------------------- | ---------: | ------------: |
| Depth loads                  | 29,451,018 |        14.614 |
| Face-region tests            | 10,637,074 |         5.278 |
| Triangle visits              | 20,035,590 |         9.942 |
| Plane builds                 |  4,964,663 |         2.464 |
| Plane reuses                 |  5,922,679 |         2.939 |
| Successful plane proofs      |  3,703,243 |         1.838 |
| Polygon clip entries         |  7,184,099 |         3.565 |
| Executed clipping planes     | 18,725,430 |         9.292 |
| Skipped clipping planes      |  8,921,101 |         4.427 |
| Expanded reduced-depth cells |    809,992 |         0.402 |
| Refined source pixels        |  2,467,165 |         1.224 |
| Resolved expanded cells      |    182,852 |         0.091 |
| Source-witness reads         |    222,061 |         0.110 |

Lazy reuse avoids 54.40% of reached plane builds; contained-plane skips
avoid 32.27% of reached clipping-plane executions. These are work-event
savings, not independently measured GPU speedups. The remaining 3.565
polygon entries and 9.292 executed planes per candidate are still a
substantial target. More cache entries could increase indexed storage
and register pressure; no hardware occupancy/spill measurement exists.

Only 22.57% of expanded cells resolve completely. Source pixel/witness
reads are 9.13% of all depth loads, but their associated region/triangle
work is not timed separately. A resolved cell does not establish an
additional hidden object: another cell or eye can still retain it.

Each retained record contributes one decisive failure across the tested
eyes. The traversal cohort retains 1,195,943 records:

| Retention reason                | Records | % all candidates | % retained |
| ------------------------------- | ------: | ---------------: | ---------: |
| Finest depth unresolved         | 624,335 |           30.981 |     52.204 |
| Nearest witness unresolved      | 184,674 |            9.164 |     15.442 |
| Wholly offscreen                | 191,564 |            9.506 |     16.018 |
| Original bounds cross viewport  | 142,253 |            7.059 |     11.895 |
| Clip/near-plane crossing        |  41,219 |            2.045 |      3.447 |
| Read budget exhausted           |  11,406 |            0.566 |      0.954 |
| Expanded guard crosses viewport |     492 |            0.024 |      0.041 |
| Invalid input / stack capacity  |       0 |                0 |          0 |

Offscreen/viewport retention totals 16.589% of all candidates. That is
large compared with the aggregate rejection gap, but it is not a matched
native-only result. Native frustum culling owns offscreen rejection;
these proxy records need not become additional visible draws. Increasing
the read budget globally targets only 0.566% of records and would add
work to the already expensive shader.

## Matched diagnostic limitation and next iteration

The Advanced shadow run submitted 476 batches and dropped all 476,
with zero accepted, zero failed and no matched outcome snapshot. This
is a failed runtime qualification of the new matched diagnostic. It
does not invalidate the separate normal performance/traversal captures,
but prevents attribution of native-only culling and useful extra draws.

The current dropped counter combines history validation, unavailable
diagnostic data and nonblocking readback readiness. Source inspection
also shows that shadow history is retained before the original native
producer runs; changes to its selector, result ownership or bounds are
therefore an audit target. The observed receipts do not distinguish these
causes. Do not weaken validity checks or treat dropped records as matches.

Prioritize these changes for the next test build:

1. Repair/qualify matched readback and expose explicit drop reasons. Use
   the resulting same-record outcomes to separate viewport proxies from
   recoverable in-view occlusion misses.
2. Add a DevBench-only A/B switch for original-depth refinement within
   guarded 2x2. Compare GPU cost and matched useful rejections in one
   process; current cell-resolution counts cannot establish net benefit.
3. Reduce polygon work before full clipping, starting with a conservative
   triangle/rectangle disjoint test. Add empty-clip and survivor-work
   counters to establish how often that test could avoid the clipper.
   Preserve precise interpolation, uncertainty fallback and all guards.

Compared with the preceding process, whole-frame GPU is 9.021 rather
than 8.943 ms and normal bounds testing is 0.555–0.640 rather than
0.520 ms. Those differences are consistent with extra refinement cost,
but are not a controlled implementation A/B: the process/view changed
and timer variation is material. This build has not demonstrated an
overall performance improvement. The individual changes remain untimed.

Advanced was restored with profiling, traversal and shadow diagnostics
OFF. Skyrim remains running. No images were taken because performance
was not close. Motion/lifecycle and SE/AE runtime qualification remain
open; static timing and arithmetic tests do not prove visual correctness.

Complete raw evidence and all normalized parameters are retained locally
under `build/astra-runtime/20261004T142532Z-hiz-refinement-restart`, with
the combined record in `analysis.json`. The initial first-use action
timeout and sandbox fpsVR connection failure are preserved separately;
neither contributed samples to the accepted comparison.

## Follow-up implementation

The next build adds guarded separating-edge rejection before polygon
clipping, and records region attempts, disjoint triangles, empty clips
and vertex-loop visits. Uncertain orientation or roundoff still clips.
This has no additional depth loads or polygon clipping passes.

The matched diagnostic uses a private submitted-bounds snapshot, checks
native output identity after production, and retains three pending GPU
readbacks for at most eight frames. Busy maps retain their slot instead
of being counted as failed matches. Every discarded batch has a reason;
snapshot publication misses are reported separately. Source, view,
bounds, epoch and consumer-frame checks remain mandatory.

DevBench action `set_depth_culling_source_refinement_enabled` toggles
original-depth refinement ON/OFF without changing guarded 2x2. The state
is not saved. Production has no toggle or diagnostic machinery and keeps
refinement ON. The next noon campaign must time Advanced and both Hi-Z
conditions with diagnostics OFF, then collect matched outcomes and work
counters separately. The 605fcf65 measurements above do not measure this
new implementation.

Validation: all 12 focused targets passed across the initial run and the
shader retry. The initial shader test reached its 60-second deadline;
the retry passed in 56.17 seconds under a 180-second bound. Both depth
orders exercise refinement ON/OFF, stereo holes, guarded pixel/3D ray
oracles and 4,003 separating-edge cases against double-precision clipping.
Eight strict FXC permutations passed (`/Ges /WX /O3`); maintained bytecode
comparison returned 2 because this change intentionally differs from HEAD.
DevBench syntax checks and production forced-header syntax/preprocessing
checks passed for Hybrid, Temporal and Menu bridge. The registered menu
schema/action contract passed. No new in-game result or production DLL
link is claimed.
