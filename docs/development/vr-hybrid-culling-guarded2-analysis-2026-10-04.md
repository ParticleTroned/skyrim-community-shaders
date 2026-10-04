# Guarded 2x2 Hi-Z: remaining cost and rejection gap

The selected implementation is guarded projected-face testing with a 2x2
source-depth reduction. The alternative proof and coarse-depth comparison
controls are removed. A 4x4 reduction remains only as the existing resource
limit fallback for large sources. Advanced remains the default.

The [latest clip-storage comparison](vr-hybrid-culling-clip-storage-results-2026-10-04.md)
supersedes the frame timings below. This record retains the preceding
vertex-storage measurements and implementation checks.

## Vertex-storage comparison and focused follow-up

After vertex-copy removal, source `e9b2a6960` was measured at noon in
save 22, with fixed player position/weather and DLSS Quality K at
1344x1492 per eye. Five 20-second whole-frame windows kept telemetry,
traversal diagnostics and profiling disabled. All valid windows had no
detected compiler activity and no source-shader compilation.

| Mode        | CPU mean, ms | GPU mean, ms | Whole-frame repeats                      |
| ----------- | -----------: | -----------: | ---------------------------------------- |
| Advanced    |       11.094 |        8.457 | 10.969/8.247, 10.952/8.428, 11.361/8.697 |
| Guarded 2x2 |       15.509 |        9.951 | 14.036/9.664, 16.982/10.239              |

Means weight each window equally. Hi-Z remains 17.67% slower on GPU and
39.80% on CPU. Repeat variation and a restarted process/view prevent
attributing the difference from the preceding campaign to vertex storage.
No pictures were taken because performance was not comparable.

Two complete 300-frame normal-shader captures measured Hi-Z culling at
0.793960/0.831383 ms, with bounds testing at 0.724313/0.750759 ms:
90.75% of mean culling cost. The retained Advanced capture measured
0.067783 ms. A second complete Advanced capture is excluded from timing
because build activity was detected at its end. Separate frozen cohorts
rejected 1,346,510/3,579,904 and 1,432,756/3,928,064 Hi-Z records,
versus 2,617,583/4,358,144 Advanced records after recovery. Advanced
promoted 64,704 native-hidden records; its raw native rejection was
2,682,287/4,358,144. No Hi-Z fallback or invalidated/unreadable history
was observed. These cohorts do not identify useful missed draws.

The next build uses indexed private face rectangles/nearest depths and
alternating eight-vertex clipping banks. It removes the compiled face
selection chain and polygon survivor-copy loop while retaining all guarded
proofs, precise intersection arithmetic, plane order and capacity checks.
All four strict Standard/reversed normal/diagnostic shaders compile with
unchanged resource/constant bindings. The maintained verifier reports the
expected bytecode differences. Focused tests pass 12/12 in 18.49 seconds,
including the independent WARP source-pixel/3D-ray tests in 17.94 seconds.
The existing DevBench diagnostics and GPU timers are retained; no new
diagnostic surface or production instrumentation is added.

Strict DXBC removes both copy loops and uses direct indexed face reads.
Normal temporaries decrease from 31 to 23 and static slots from 2,674 to
2,634. Indexed capacity increases from 120 to 132 four-component entries;
these declarations do not measure hardware registers or occupancy.

Current evidence is local under
`build/astra-runtime/20261004T113738Z-hiz-private-vertices` in the primary
repository. Shader checks used `build/astra-validation/adaptive/validate-clip-storage.ps1`
and focused tests used `build/astra-validation/adaptive/validate-proof-ab.ps1`
in the worktree. The new clip-storage build is not yet measured in game.

## Preceding guarded baseline

The preceding valid save-22 comparison leaves two problems: Hi-Z rejects fewer
candidate records and spends much longer testing their bounds. These need
separate measurements and improvements. The new build also addresses the
confirmed per-region projected-vertex array copy; its runtime benefit is
unmeasured. This report's performance numbers describe the preceding A/B
DLL, not that subsequent source change.

## Comparable performance

Eight retained 20-second windows used the same save, noon resets, five-second
settling, weather, player position and rendering settings in one process.
Telemetry, traversal diagnostics and CSX GPU profiling were disabled during
whole-frame timing. No source shader compilations occurred. The guarded
configuration was exercised twice, between Advanced observations.

| Method           | CPU mean, ms | GPU mean, ms | Tested / rejected records | Rejection |
| ---------------- | -----------: | -----------: | ------------------------: | --------: |
| Advanced         |        9.649 |        7.416 |     5,046,272 / 3,086,867 |   61.171% |
| Guarded 2x2 Hi-Z |       11.466 |        9.008 |     4,579,328 / 1,767,540 |   38.598% |

The windows retained 3,124 Advanced and 2,731 guarded 2x2 samples.
Frame means give equal weight to the two windows per method. Counters come
from separate, telemetry-enabled profiler windows. They are repeated
submission records, not matched unique objects or draw calls.

Guarded 2x2 costs an additional **1.817 ms CPU (18.83%)** and **1.592 ms GPU
(21.47%)**. Its two GPU frame means were 8.924 and 9.093 ms, versus
Advanced's 7.402 and 7.430 ms. The selected configuration had the lowest
observed whole-frame means among the Hi-Z alternatives. Its 0.032 ms GPU
advantage over original-vertex 2x2 is small; this establishes a focused
baseline, not statistical proof of that variant's advantage.

For context, 1.592 ms consumes 14.33% of an 11.111 ms/90 Hz frame budget,
or 19.11% of an 8.333 ms/120 Hz budget. Refresh rate was not captured for
these valid windows, so neither budget is asserted as their runtime mode.
CPU and GPU overlap and must not be added.

### Advanced and Legacy

Both native methods use the same native depth downscale and rasterized
bounding-box producer. Advanced can additionally promote native-hidden
records to visible during conservative temporal recovery. The valid
Advanced capture recorded zero recovery attempts and promotions. Its
61.171% rejection therefore reflects the native producer without recovery
changing those results. This makes the native producer relevant to both
Advanced and Legacy; it does not establish identical future behavior.

The earlier [native-count campaign](vr-hybrid-culling-native-counts-2026-10-03.md)
observed 61.639% rejection for Advanced and 61.777% for Legacy, with zero
recovery differences. That is supporting historical evidence from different
cohorts. Its frame timings were affected by concurrent compilation and are
not a current performance baseline.

An attempted fresh Legacy/Advanced investigation received 785/861 empty
native batches and zero tested records. Guarded 2x2 then also fell back with
`empty_or_invalid_batch`. These windows are excluded: their frame times
cannot measure culling performance. Reloading save 22 ended the process in
an engine load/preload crash. The probable stack does not identify a Hi-Z
render hook, and crash attribution remains undetermined. No clean fresh
Legacy timing or matched Legacy/Hi-Z rejection comparison is available.

The next DLL exposes observed engine enable/extent values separately from
CSX's desired policy. Observations are unavailable/null without proven VR
engine bindings; they neither force enablement nor alter engine state.
Verify nonempty native batches before another measurement.

## Where the GPU time goes

Separate complete 300-frame GPU captures, one per condition, measured self
time:

| GPU scope              | Advanced, ms | Guarded 2x2, ms |
| ---------------------- | -----------: | --------------: |
| Native downscale       |     0.030123 |               — |
| Native bounds producer |     0.056052 |               — |
| Hi-Z hierarchy setup   |            — |        0.000673 |
| Hi-Z visibility setup  |            — |        0.005243 |
| Hi-Z base depth        |            — |        0.025444 |
| Hi-Z mip reduction     |            — |        0.032884 |
| Hi-Z bounds test       |            — |    **0.851016** |
| Hi-Z result copy       |            — |        0.004825 |
| Total culling          | **0.086175** |    **0.920085** |

Bounds testing is **92.49%** of Hi-Z culling. All its other scopes together
cost 0.069069 ms. Eliminating mip reduction entirely would save only
0.032884 ms. Halving bounds cost would still leave about 0.495 ms of total
culling, versus 0.086 ms for the native capture. Reaching native scope cost
with unchanged Hi-Z overhead would leave just 0.017106 ms for bounds:
approximately a **98% reduction** from the current bounds scope. Small
shortcuts cannot plausibly close that entire producer gap.

The observed producer difference is 0.834 ms, about 52% of the 1.592 ms
whole-frame GPU difference. Those numbers come from different windows;
the remaining 0.759 ms is not an exact measurement of extra drawing. Fewer
useful rejections could increase drawing, but the current counters cannot
attribute that residual. The largest shared profiled effect delta was
Upscaling at +0.040 ms; no measured individual CSX effect explains it.
Uninstrumented engine draws and capture/scene variation remain possible.

CPU prepare/dispatch/readback scopes are measured in microseconds. For
example, guarded dispatch averaged 23.859 microseconds and post-native
history readback 13.011 microseconds. These nested/inclusive scopes must
not be summed. They do not directly explain the 1.817 ms CPU frame gap;
paired visible-draw/work evidence is still needed.

### Confirmed shader work

Strict `/Ges /WX /O3` DXBC for guarded Standard Z has 2,644 static instruction
slots, 37 ordinary temporary declarations and ten indexed temporary
arrays. The original-vertex alternative differs by only ten instruction
slots and has the same declared storage. Its small arithmetic difference
does not explain the structural native/Hi-Z cost gap.

The guarded shader declares eight arrays of eight `float4` entries and two
of forty entries. That is 144 declared vector entries, not a measurement
of physical NVIDIA registers, simultaneous liveness, spilling or occupancy.
RenderDoc 1.44 is available; no installed Nsight or PIX was found in the
bounded tool search. Hardware counters were not collected.

The measured helper copies all eight projected vertices to another indexed
array on each region test, before repeating face/triangle/clip work. The
new implementation initializes one private shader-invocation vertex array
directly during projection. Face preparation and region tests read it;
every vertex is initialized for the current eye before either helper runs.
Separate compute invocations do not share this storage.

Strict compilation confirms both repeated eight-vertex copy blocks are
gone in every normal/diagnostic Standard/reversed permutation. Indexed
arrays fall from ten to seven, declared vector entries from 144 to 120,
and ordinary temporary declarations from 37 to 31 for normal shaders
(42 to 36 for diagnostics). Resource bindings and constant layouts remain
identical; no new GPU resources are added.

There is a tradeoff: the compiler emits eight initial zero writes plus
eight vertex writes per eye, outside all region/face/triangle loops. Static
instruction slots increase from 2,644 to 2,674 in normal Standard Z, rather
than decreasing. The failed `inout`-only experiment retained the copies
and was discarded. The accepted change removes repeated work and indexed
storage, but does not establish occupancy or a runtime speedup. Measure it
against this report's guarded baseline before the next optimization.

The next substantial arithmetic/storage target is face testing. A convex
projected quad can share one depth plane and region-intersection work
instead of evaluating both triangles independently. Precomputed terms may
increase live storage, so compiler counts alone cannot choose the winner.
Retain conservative guards, equality behavior and a visible fallback for
degenerate or uncertain geometry. Reflected/sheared boxes, near-plane
crossings, holes, stereo asymmetry and depth equality require coverage.

## Why rejection remains lower

Hi-Z already tests the projected six box faces, two triangles per face,
with regional depth extrema, plane proofs and polygon clipping. The
remaining gap is not simply the old whole-box rectangle/nearest-depth
approximation. Native rasterization still has tighter discrete pixel
coverage than a conservative reduced-depth/continuous-region proof.

The latest rejection gap is **22.573 percentage points**. Different
submission populations prevent interpreting it as 22.573% extra useful
objects drawn. In particular, native zero-fragment offscreen proxy queries
can reject records that Hi-Z intentionally leaves visible for normal
frustum handling.

The concrete sources of conservatism are:

-   A 2x2 base cell stores its farthest source depth. One uncovered, masked,
    invalid or far/sky pixel can prevent proof for the other pixels.
-   The active two-pixel guard plus rounding allowance expands each tested
    region. A nominal 2x2 leaf spans about 6.0625x6.0625 source pixels for
    guarded face testing. More accurate source depth alone cannot eliminate
    this coverage conservatism.
-   Any corner crossing the eye/clip bounds remains visible. Partially
    viewport-clipped bounds and wholly offscreen bounds also remain visible.
    Native hardware rasterization can clip these proxies more precisely.
-   Depth/interpolation allowances preserve safety around equality and
    numerical uncertainty. Removing them globally lacks quality evidence.

A separate earlier guarded diagnostic cohort in a different view had
99.928% diagnostic coverage, with one boundary batch absent. It measured
about 12.03 depth loads, 3.97 regions, 7.19 triangle attempts, 2.50 polygon
clips and 1.21 successful plane proofs per candidate across both eyes.
Its first decisive retention reasons, divided by candidate count, were:

| Reason                                         | Events per candidate |
| ---------------------------------------------- | -------------------: |
| Finest reduced depth could not prove rejection |              28.830% |
| Nearest-vertex visibility witness              |              11.538% |
| Wholly offscreen                               |              10.574% |
| Partial viewport                               |               6.869% |
| Clip crossing                                  |               3.855% |
| Read budget exhausted                          |               0.278% |
| Guard-only retention                           |               0.186% |
| Invalid input / stack capacity                 |              0% / 0% |

These are retention events, not native false negatives, and not a partition
of the latest save-22 population. Either eye can prevent stereo rejection;
the second eye may not run after the first retains an object. Raising the
64-load budget could recover at most 0.278 percentage points in this
earlier cohort even if every budget exit became hidden. It is a poor
target for the much larger gap and would increase work.

## Ordered follow-up

1. The helper-array copy is now removed. Measure bounds time, whole frames
   and rejection; do not claim a runtime gain from reduced static shader
   storage alone.
2. Add a DevBench-only same-submission native/Hi-Z shadow comparison.
   Preserve the native displayed result and native producer side effects;
   use separate Hybrid result resources and matched depth/camera/bounds.
   Record both-hidden, native-only-hidden, Hi-Z-only-hidden and both-visible
   outcomes, with reason/coverage/size/work buckets. Submission indices are
   not persistent object identities. Keep this diagnostic out of timing
   windows and compile it out of production.
3. Reduce duplicated triangle/plane/clip work with a shared quad proof and
   smaller or analytical region intersection. The existing face rectangles
   are already cached. Judge storage/ALU changes by compiled evidence and
   runtime cost, not source brevity.
4. If matched native-only-hidden objects concentrate at unresolved leaves,
   test a bounded source-depth refinement for those cells. Examine only
   relevant conservatively covered pixels, preserve mask/invalid-depth
   failure behavior, and require independent oracle coverage. This can
   improve rejection but adds work; selective use needs the matched data.
5. If matched data instead implicate viewport/near-plane clipping, implement
   conservative clipped proxy coverage. Separate useful hidden geometry
   from zero-fragment/offscreen count differences before prioritizing it.

A cheap proof pass followed by a compacted refinement queue is a later
candidate if work histograms establish an expensive minority of objects.
Queue storage, additional dispatches and repeated setup can outweigh saved
divergence; no occupancy or long-tail bottleneck is currently measured.

Preserve both-eye rejection, masks, guards, depth allowance, native resource
ownership, epoch/history validation and fail-visible fallback. No motion
or lifecycle qualification, performance parity, or SE/AE runtime pass is
claimed. PBR grass, grass optimization and Reverse Z remain later PRs.

## Evidence and validation

The focused and production checks were run through the preserved local
drivers, which invoke the repository CMake/MSVC wrappers:

```powershell
pwsh ./build/astra-validation/adaptive/validate-proof-ab.ps1
pwsh ./build/astra-validation/adaptive/validate-guarded-only-production.ps1
```

The maintained shader check used `tools/verify-shader-refactor.ps1`, shader
`package/Shaders/VRHybridCulling/TestBoundsCS.hlsl`, base `045fe4e8c`, profile
`cs_5_0`, and four `CSHADER` permutations: normal, reversed,
`CSX_HIZ_DIAGNOSTICS`, and diagnostics plus reversed. The base also defined
`CSX_HIZ_GUARDED_VERTEX_BASELINE`; that removed define is inert in the
candidate. FXC was Windows SDK 10.0.26100.0 x64. Follow-up compilation used
`/Ges /WX /O3 /T cs_5_0 /E main`, retaining assembly and bytecode for both
sides. The maintained verifier's final exit 2 denotes the expected four
bytecode differences; it is not an equivalence pass.

Valid measurements used source
`045fe4e8c5f849ace2ffb2e985d13bc518ba26dd`, producer Build ID
`408f024938f05e005e2321c34bee15131de311389356d4c6614f0513fb47c891`.
The scene was WhiterunExterior01/Tamriel, SkyrimClearTU, save 22, DLSS
Quality K at 1344x1492 per eye with 2016x2240 display dimensions.

Preserved local evidence under the primary repository:

-   `build/astra-runtime/20261004T1020-hiz-depth-ab-save22`: eight retained
    whole-frame windows, separate complete GPU captures and frozen counters.
-   `build/astra-runtime/20261004T1046-hiz-guarded2-investigation`: excluded
    empty native batches, Hybrid fallback and failed reload journal. One
    cached profiler response caused by reused command IDs is excluded;
    subsequent commands used a new assay-scoped client ID.
-   Worktree `build/astra-validation/proof-ab-dxbc-identity-e60c48a3`:
    strict DXBC and recorded identities for the measured proof variants.
-   Worktree `build/astra-validation/adaptive/guarded-only-dxbc-20261004T110046089Z`:
    four maintained and four strict normal/diagnostic Standard/reversed
    comparisons identical after removing the alternate paths.
-   Worktree `build/astra-validation/adaptive/proof-ab-tests-20261004T110158573Z`:
    12/12 focused tests passed, including WARP.
-   Worktree `build/astra-validation/adaptive/guarded-only-production-20261004T110300952Z`:
    actual-command/forced-header syntax and preprocessing for VR, Hybrid,
    Temporal and menu bridge; developer-only markers absent.

The last three checks describe the initial guarded-only cleanup before
the subsequent helper-copy optimization. Its maintained verifier ran first
and found intentional bytecode differences in all four permutations;
strict compilation, copy/loop-placement and reflection evidence is under
`build/astra-validation/adaptive/guarded-array-alias-strict-20261004T111651253Z`
in the worktree. Final focused tests passed **12/12 in 18.90 seconds**, WARP
18.64 seconds, under
`build/astra-validation/adaptive/proof-ab-tests-20261004T111806880Z`.
They exercise guarded thresholds, both reduction sizes, Standard/reversed
depth, stereo holes, clipping and independent source-pixel/3D-ray oracles.
A production DLL link, new guarded-only runtime comparison
and fresh Legacy timing remain unrun. The crashed session's original log
was preserved and verified under the user-specified `D:/Coding/GitHub/CS logs`
archive before analysis.
