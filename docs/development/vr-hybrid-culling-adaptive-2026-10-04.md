# Hi-Z finer-depth comparison: 4 October 2026

The finer 2x2 hierarchy is active, but Hybrid still trails Advanced:
**12.6% more CPU time and 14.9% more GPU time**, with **40.1% candidate
rejection versus 59.1%**. Culling GPU scopes average **0.775 ms for
Hybrid and 0.133 ms for native Advanced**. Matching rejection alone
would not establish performance parity.

## Final same-build comparison

Two selected 20-second fpsVR windows per method ran at WhiterunExterior01
with SkyrimClearTU weather. Noon was reset before each timing, counter
and profiler phase, followed by five seconds of settling. Player position
stayed fixed. DLSS Quality K remained at 1344x1492 render pixels and
2016x2240 display pixels per eye on the RTX 4090.

The initial Advanced baseline included headset settling: about 0.494
degrees within its boundary observations and about 0.8 degrees relative
to later windows. It was preserved and replaced with a settled repeat.
The selected order is Hybrid / Hybrid / Advanced / Advanced.
Selected boundary HMD observations differ by at most 0.358 mm and 0.0425
degrees; these observations do not certify continuous pose stability.

| Mode     | Window                 | Samples | CPU mean ms | GPU mean ms |
| -------- | ---------------------- | ------: | ----------: | ----------: |
| Hybrid   | fine-hybrid1           |    1476 |   10.958401 |    8.794851 |
| Hybrid   | fine-hybrid2           |    1484 |   11.157345 |    9.001954 |
| Advanced | fine-advanced2         |    1632 |    9.757598 |    7.715993 |
| Advanced | fine-advanced3-settled |    1564 |    9.881905 |    7.770524 |

Equal-window means are **9.819752 ms CPU / 7.743258 ms GPU** for Advanced
and **11.057873 / 8.898403 ms** for Hybrid. Differences are **1.238121 ms
CPU (+12.6%) and 1.155144 ms GPU (+14.9%)**. Advanced GPU repeats differ
by 0.055 ms; Hybrid repeats differ by 0.207 ms. Both Hybrid windows
exceed both Advanced windows. CPU and GPU times overlap; do not add them.

Telemetry, traversal diagnostics and profiling were off during timing.
All selected windows retain covering monotonic fpsVR records, unchanged
runtime/renderer identity, no shader failures and no detected compiler
activity. All owned fpsVR loggers were stopped and verified inactive.
No pictures were taken because performance is not comparable.

The gap is larger than in the preceding campaign, but player position
and view differ between campaigns. Neither whole-frame measurements nor
query captures isolate a cross-build regression or improvement. Finer
depth can increase traversal work; isolating that effect requires both
builds at the same saved position, view and settings.

## Rejection and active hierarchy

Separate 20-second counter windows:

| Mode     | Window              | Culled records | Tested records | Rejection | Batches |
| -------- | ------------------- | -------------: | -------------: | --------: | ------: |
| Advanced | fine-advanced-count |      3,993,075 |      6,754,304 |    59.12% |   1,649 |
| Hybrid   | fine-hybrid-count1  |      2,362,276 |      5,890,048 |    40.11% |   1,438 |
| Hybrid   | fine-hybrid-count2  |      2,294,161 |      5,726,208 |    40.06% |   1,398 |

Advanced counts are after recovery, including its 105,536 promotions.
Every batch contains 4,096 candidates. Both Hybrid windows have zero
fallback, invalidated-history, unreadable and promoted records/batches.
The backend is Hybrid, and accepted batches equal submissions. Recovery
or fallback does not explain the remaining rejection gap.

These totals count repeated candidate observations, not unique objects
or matched persistent cohorts. They establish different aggregate
rejection, not which exact objects only Advanced rejects.

Source snapshots confirm **sourceReduction=2**, **1024x1024x2 layers**,
**10 mips** and **11,184,800 logical bytes** for the 2688x1492 depth source.
There are no warmed-window allocations, pipeline rebuilds or recreations.
An observation labeled older_frame is not a rejected batch; independent
invalidation counters remain zero.

## GPU and CPU diagnosis

Two captures per method resolved 300 GPU frames each with telemetry on,
traversal diagnostics off and the normal shader. All relevant GPU
histories contain 300 samples and reproduce their timer means. Boundary
HMD differences across captures are at most 0.470 mm and 0.0462 degrees.

| GPU self time               | Capture 1 ms | Capture 2 ms |
| --------------------------- | -----------: | -----------: |
| Hybrid build base           |     0.022752 |     0.022113 |
| Hybrid reduce mips          |     0.033236 |     0.031515 |
| Hybrid hierarchy remainder  |     0.000653 |     0.000655 |
| Hybrid test bounds          |     0.704983 |     0.713970 |
| Hybrid copy results         |     0.004980 |     0.004208 |
| Hybrid visibility remainder |     0.005309 |     0.006170 |
| Hybrid disjoint scope total |     0.771913 |     0.778631 |
| Native downscale            |     0.048599 |     0.042669 |
| Native producer             |     0.094549 |     0.080701 |
| Native disjoint scope total |     0.143148 |     0.123371 |

The previous analysis omitted existing VRDepthCulling native timers.
Advanced exposes NativeDownscale and NativeProducer. The latter surrounds
the native producer call, including its result-copy work. Hybrid parent
rows are exclusive remainders; totals do not double-count children.

Bounds testing is **91.3-91.7%** of measured Hybrid GPU work. The mean
culling-scope difference is **0.642 ms**, with Hybrid about **5.8 times**
the native scope total. Captures are separate from fpsVR timing:
subtracting this difference from the whole-frame gap does not quantify
drawing cost or predict an equal-rejection result.

Hybrid counter-window CPU means are 20.22-20.75 microseconds for dispatch,
3.12-3.28 for preparation, 10.95-11.35 for post-native history validation,
and 3.73-3.91 for intercepted native readback. These inclusive stages must
not be summed. Native recovery averages 105.69 microseconds per batch.
These stages do not support a readback-stall explanation for the
whole-frame regression. Extra retained drawing is a plausible contributor;
draw-cost attribution was not measured.

## Traversal diagnosis on the same build

Two further 20-second Hybrid counter windows and one Advanced counter
window ran after a game restart, using the same compiled source and DLL.
Noon was reset and allowed to settle for five seconds before each window.
The current position is [17060.19140625, -12208.3115234375,
-4771.19970703125], about 25 game units from the earlier campaign.
Weather and renderer settings match. Boundary HMD observations differ by
at most 0.454 mm and approximately 0.051 degrees, without a continuous
pose guarantee. These diagnostic runs do not replace the final timing
comparison above or establish a cross-build change.

| Counter window   | Rejected / tested records | Rejection |
| ---------------- | ------------------------: | --------: |
| hybrid-reasons-2 |     1,974,474 / 5,099,520 |    38.72% |
| hybrid-reasons-3 |     2,041,828 / 5,165,056 |    39.53% |
| advanced-count   |     3,937,386 / 6,381,568 |    61.70% |

Advanced used policy `balanced`; this settled window recorded no recovery
attempts or promotions. Recovery returns early for a coherent view. Its
61.70% must not replace the earlier 59.12% counter result without also
preserving this changed scene/view and recovery context.

The following percentages use diagnosed candidate objects as denominator,
with work summed across both eyes. A retained object contributes exactly
one decisive retention reason; an untested second eye is not another
retention. These are reasons Hi-Z retained candidates, **not identified
false negatives against native culling**.

| Decisive retention reason      | Window 2 | Window 3 |
| ------------------------------ | -------: | -------: |
| Finest depth unresolved        |  30.028% |  29.447% |
| Nearest vertex unresolved      |  12.171% |  12.114% |
| Viewport guard                 |  16.735% |  16.562% |
| Eye/near/far clip crossing     |   2.024% |   2.020% |
| Depth-read budget exhausted    |   0.323% |   0.325% |
| Invalid input / stack capacity |       0% |       0% |

Diagnostic coverage is 5,095,424 / 5,099,520 and
5,160,960 / 5,165,056 accepted candidates: **99.92%** in each window.
Each has one boundary batch without a resolved diagnostic record.
Not-ready, failed, discarded and unavailable diagnostic batches are zero,
as are Hybrid fallback, history invalidation and unreadable batches.
Frozen snapshots satisfy the eye-reason and visibility identities; no
independently sampled atomic counters are treated as one live snapshot.

| Mean work per diagnosed object | Window 2 | Window 3 |
| ------------------------------ | -------: | -------: |
| Depth reads                    |   12.438 |   12.432 |
| Refined regions                |    4.216 |    4.182 |
| Triangle attempts              |    7.659 |    7.585 |
| Triangle attempts per region   |    1.817 |    1.814 |

Triangle attempts are counted before vertex/plane shortcuts; they are
not exact polygon-clip counts. These means do not locate GPU time,
establish hardware occupancy/spills or describe the expensive tail.

## Why rejection can remain lower

Projected-face refinement already runs. The test is not limited to a
loose rectangle with one box depth. Source and diagnostic evidence narrow
the remaining conservative differences:

-   A corner crossing the eye, near or far plane retains the whole box.
    A guarded rectangle extending beyond an eye viewport also retains it.
    Native rasterization clips geometry. The viewport category also includes
    wholly offscreen boxes deliberately left to frustum culling. Native
    rasterization can reject boxes producing no viewport fragments, so part
    of the count gap may save no drawing. Split offscreen, partial-viewport
    and guard-only exits before estimating useful clipping gains.
-   A 2x2 leaf still stores its farthest source depth. Far, masked or invalid
    samples can prevent proof even when most of the region is hidden.
    Guarded face regions extend beyond the cell by two pixels plus a rounding
    margin. Finer depth does not eliminate these differences.
-   The nearest-vertex precheck mostly avoids work the complete current
    proof would also fail: inclusive face tests preserve that vertex.
    Removing it generally adds work, rather than recovering rejection.
    Its extra read can affect marginal budget cases. Resolving every recorded
    budget exit would recover at most 0.325 percentage points here, so a
    larger budget cannot close the observed gap.
-   Face refinement adds 64 depth units of interpolation allowance to the
    base 8/16777216 bias, including original-vertex and whole-face shortcuts
    that perform no clipping interpolation. Numerical review should
    distinguish those proofs from interpolated clipping. Globally reducing
    bias would risk missing geometry.

Nearest-vertex and finest-depth exits together account for about 69% of
retained candidates. They mix genuine visibility with coarse far/masked
samples, guard overlap and numerical allowances; aggregate reasons cannot
separate those causes. The native contract investigation has not fully
established native downsample/pixel-shader bias and mask semantics.
Matching native counts alone is not an independent correctness oracle.

Diagnostic access is restored through the sanctioned bundled-controller
transport. This resumed run had no direct DevBench tools in its callable
catalog before transport selection. Fresh server discovery exposed the
exact setter; enable, measurement and restoration succeeded on the same
DLL. No controller or game restart was needed to collect these counters.
This does not establish that direct client schema refresh is fixed.

## Next focused work

The strongest bounded source-level candidate is to use the existing base
bias for whole-face and original-triangle minimum proofs that perform no
interpolation, retaining the additional allowance for clipped vertices.
The current non-interpolating shortcuts use 72 rather than 8 depth units.
Require numerical review and equality/ULP, sloped-face, reflected/sheared,
stereo and independent source-pixel/ray tests. Do not globally reduce bias;
the separate plane proof needs its own error analysis. Recoverable rejection
and performance gains remain unmeasured.

Add DevBench-only counts for actual polygon clips, plane-proof successes,
and proofs passing base bias but failing the interpolation allowance.
Split viewport reasons into wholly offscreen, partial and guard-only.
These bounded diagnostics should choose subsequent face pruning, cached
planes or clipper changes. Only 1.81 triangle attempts per region are
observed, so pruning half the six nominal faces does not imply halving
work. Plane caching can also increase per-thread storage.

Exact attribution could use a DevBench-only mode comparing native and
Hybrid on the same submitted bounds/depth while preserving displayed
native results. Record native-only rejections alongside Hybrid reasons.
Keep duplicate work out of performance windows and production builds.
This comparison mode is proposed, not implemented.

Bounds testing is the main measured target; reducing repeated per-region
face work is a source-informed candidate. Separating inexpensive candidates
from costly refinement may
reduce divergence or register pressure, but these captures do not prove
an occupancy or spill bottleneck or justify that larger rewrite yet.
Hierarchy construction is only
0.054-0.057 ms, with a much smaller optimization ceiling. Equal rejection
alone is insufficient evidence of parity.

With current non-bounds cost unchanged, matching the native 0.13326 ms
culling scope would require reducing bounds from 0.70948 to approximately
0.06746 ms, a **90.5% reduction**. Halving bounds would still leave about
0.421 ms total. This arithmetic concerns culling scopes only, not whole
frame time. Meaningful improvement is plausible; parity is not promised.

## Validation and evidence

Measured clean source: 425b8d37334884e9d3995e6b2720384f11558196.
Producer Build ID:
cfbd7c0107fe81e29484766a9bd9a019276d4c5eaa51b90c99e61d5e0d29aa3f.

The implementation's 12/12 focused DepthCulling, VRHybridCulling and
D3DContextProtection tests passed in 12.86 seconds, including WARP,
source-pixel/ray, stereo-hole, padding, bias and resource-boundary cases.
Production compiler flags, forced headers and diagnostic-absence checks
passed. A separately linked production DLL, SE/AE runtime and
motion/lifecycle qualification remain untested here.

The enabled physical DLL, adjacent manifest, AIO receipt and six depth
shaders match the measured source. No competing enabled loose, Overwrite
or unmanaged Data provider was found at those paths.

Raw timings, counters, profiler histories, identity checks and restoration
receipts remain under main-workspace
build/astra-runtime/20261004T063415Z-hiz-fine-depth/.
final-settled-comparison.json selects the final timing windows;
final-analysis.json includes counters and GPU comparisons.
The preliminary summary and excluded baseline remain preserved.

New diagnostic evidence is under main-workspace
build/astra-runtime/20261004T071656Z-hiz-reasons/.
diagnostic-analysis.json preserves counts, denominators, work, boundary
poses, scene context and restoration. Its generating analyze.py passed
all counter-identity, frozen-window, health and restoration assertions.
The first diagnostic response hit the controller's default 15-second
HTTP timeout; its later frozen state is preserved but excluded from the
selected comparison. Replacements used an explicit 40-second request
allowance and retained complete transcripts. Local automation feedback:
AUTO-20261004-073222781-7D2F94FB.

Advanced and the original telemetry preference are restored, profiling
and traversal diagnostics are off, and Skyrim PID 30492 was left running.
No runtime code changed or new DLL was built for this diagnosis.
