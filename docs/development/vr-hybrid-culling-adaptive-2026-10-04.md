# Hi-Z adaptive traversal: 4 October 2026

The latest code adds cached face tests and reduces polygon-array copying.
The measurements below belong to the preceding adaptive build, whose
exact identity is retained. The new shortcuts pass the focused tests;
their in-game performance has not yet been measured.

## Latest same-build in-game comparison

Two 20-second fpsVR windows per method ran in Advanced / Hybrid / Hybrid /
Advanced order. Noon was reset before every window, with five seconds of
settling. The WhiterunExterior01 scene, SkyrimClearTU weather, player
position and DLSS Quality K fixture were fixed: 1344x1492 render pixels
per eye, 2016x2240 display pixels. Boundary HMD observations differed by
at most 1.29 mm and 0.105 degrees. These observations do not continuously
certify the pose between boundaries.

| Mode     | Run       | Samples | CPU mean ms | GPU mean ms | CPU p95 ms | GPU p95 ms |
| -------- | --------- | ------: | ----------: | ----------: | ---------: | ---------: |
| Advanced | advanced1 |    1788 |    9.162808 |    7.229474 |     13.100 |      8.365 |
| Hybrid   | hybrid1   |    1413 |   10.616844 |    9.299575 |     15.040 |     11.000 |
| Hybrid   | hybrid2   |    1340 |   11.118507 |    9.324701 |     16.505 |     10.905 |
| Advanced | advanced2 |    1590 |    9.369686 |    7.468994 |     13.400 |      9.000 |

Giving each completed window equal weight, Advanced averaged 9.266247 ms
CPU / 7.349234 ms GPU; Hybrid averaged 10.867676 / 9.312138 ms. Hybrid
costs an additional **1.601429 ms CPU (+17.3%) and 1.962904 ms GPU
(+26.7%)**. Those differences consume 19.2% and 23.6% of the 120 Hz
8.333 ms frame budget respectively; CPU and GPU time overlap.

Telemetry, traversal diagnostics and the CSX profiler were disabled during
these timing windows. All four completed with monotonic, covering fpsVR
records, stable runtime identity, no shader failures and no detected
compiler activity. Both runs of each mode support the same conclusion.
This is one static scene, not a general performance or fidelity verdict.
No images were taken because the measured performance was not similar.

Separate 20-second counter windows reported:

| Mode     | Culled records | Tested records | Rejection | Accepted batches |
| -------- | -------------: | -------------: | --------: | ---------------: |
| Advanced |      4,188,974 |      6,937,834 |     60.4% |            1,837 |
| Hybrid   |      2,284,772 |      5,983,146 |     38.2% |            1,576 |

Hybrid had zero fallback, invalidated or unreadable batches. Counts are
repeated candidate-result observations, not unique objects or matched
persistent cohorts. The gap is not explained by fallback in this scene.
Earlier builds were not retested in this process, so their timings cannot
establish the adaptive change's isolated speedup.

Measured clean source: `ea7615fd8975fa341f8eb279425791cc81221850`.
Producer Build ID:
`ce938ab2748dce78dfd756051d0970f8ad53fa09390ad79051311f7150a2334f`.
The enabled physical DLL, manifest, AIO receipt and six depth shaders
matched the producer and source. Full local evidence is in main-workspace
`build/astra-runtime/20261004T010846Z-hiz-adaptive/`, including
`final-comparison.json`, raw CSVs, per-run snapshots, timing distributions
and the diagnostic analysis. The pre-test DLAA rendering profile,
Advanced culling and telemetry state were restored after testing.
Logging stayed at Info and the profiler was restored disabled.

## Measured optimization opportunity

Two separate 300-frame GPU captures used normal Hybrid shaders with
telemetry enabled and traversal diagnostics disabled. An Advanced capture
also completed 300 frames. All bounded captures resolved every submitted
frame, with no query-slot refusals. Completed capture activity flags are
false; the Hybrid timers have GPU data and 300 retained samples each.

| Hybrid GPU self time        | Capture 1 ms | Capture 2 ms |
| --------------------------- | -----------: | -----------: |
| Build base                  |     0.026338 |     0.028442 |
| Reduce mips                 |     0.027180 |     0.026378 |
| Hierarchy parent remainder  |     0.000658 |     0.000683 |
| Test bounds                 |     1.515315 |     1.489838 |
| Copy results                |     0.005664 |     0.005242 |
| Visibility parent remainder |     0.006390 |     0.005136 |
| Sum of disjoint self times  |     1.581546 |     1.555720 |

Bounds testing accounts for **95.8%** of this measured Hybrid GPU work.
The parent rows are exclusive remainders; no inclusive parent is added
to its children. These GPU timestamp captures are separate diagnostic
windows and cannot be subtracted mechanically from the fpsVR timings.
Native engine depth-culling GPU work is not represented by equivalent
timers, so the Advanced capture does not establish its absolute cost.

The counter-only window measured Hybrid dispatch at 18.45 microseconds,
preparation at 2.75 microseconds, post-native validation at 10.34
microseconds and intercepted native readback at 3.37 microseconds mean.
Advanced native readback averaged 5.37 microseconds. These inclusive CPU
stages must not be summed. They provide no evidence that readback stalls
explain the 1.60 ms whole-frame CPU gap.

Offline FXC compilation of the current Standard-Z shader with strict
O3 flags reports approximately 2,242 instruction slots, 21 ordinary
temporaries and 12 dynamically indexed temporary arrays containing 160
float4 entries. Both eye paths are present in the bytecode. This shows
substantial scratch storage and control flow; it does **not** prove
hardware register spilling, occupancy or a particular cache bottleneck.

The highest-value next experiment is to reduce repeated face work inside
`TestBoundsCS.hlsl` / `ProjectedBounds.hlsli`:

1. Prepare reusable face bounds and depth information once per box/eye.
   Use cheap conservative per-region proofs before exact polygon clipping.
2. Retain the current exact clipper for ambiguous regions initially.
   Reduce dynamically indexed polygon copies and repeated triangle setup;
   preserve the read budget, masks, guards, bias and both-eye proof.
3. Evaluate tighter finest-level evidence only after measuring rejection
   reasons. The 4x4 maximum-depth base loses sub-cell coverage permanently;
   deeper traversal alone cannot recover it.

The face-cache and polygon-copy changes below implement the first bounded
experiment. Analytic face-plane proofs and finer source-depth evidence
remain proposals; neither is required for the current shortcuts.
Halving the roughly 1.50 ms bounds pass suggests about 0.75 ms of local
GPU work to target. Even removing that pass entirely would not by itself
account for the 1.96 ms whole-frame gap under a simple additive estimate.
Parity also needs better rejection or savings elsewhere; the current
data do not promise it. Hierarchy fusion and readback micro-optimization
have much smaller measured ceilings.

The direct connector action inventory omitted
`set_depth_culling_traversal_diagnostics_enabled`, although the loaded
DLL reports diagnostic availability. No unsupported dispatcher bypass
was used. Budget exhaustion, finest-unresolved, clipping and viewport
reason proportions therefore remain unmeasured. Local feedback receipt:
`AUTO-20261004-013128725-B1AC7F0D`.

Raw render-scale status and preparation snapshots were retained around
captures. This build's status lacks the complete resource-publication
fields, and read-only qualification status has no active observation;
no full resource-publication qualification is claimed. One preparation
attempt stopped before profiler enable because the local runner treated a
wait step's absent `ok` field as failure. Its receipt is preserved; the
parser was corrected and the two completed Hybrid captures are retained.
No moving-view, lifecycle, VRAM or SE/AE runtime qualification was added.

## Cached face tests

After the unchanged four-load coarse proof fails, the shader caches each
face's screen rectangle and nearest corner depth once per box/eye. A
region outside that rectangle, or behind that conservative depth bound,
does not need either triangle clipped. Remaining triangles get their own
cheap nearest-depth test before the original exact clipping arithmetic.
The same guarded bias applies to the cheap and exact face proofs.

Face corner lists retain the same twelve triangles and winding. The
implementation does not replace them with interpolated quads or assume
front-face orientation. Both eyes, masks, guards, 64 actual reads per eye,
stack capacity, finest-level fail-visible behavior and history remain
unchanged. The cached depths are packed into two float4 values.

Clipping keeps its arrays local to the region test, avoiding the helper's
inout array copies. Only initialized vertices below the current count are
read; empty polygons stop before another clipping plane, and capacity or
invalid intersections still fail visible. DevBench triangle counts now
count iterations remaining after whole-face shortcuts. The diagnostic
record, reason semantics and production isolation are unchanged.

Validation for this change:

-   `pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_hybrid_culling_shader_test --parallel 4`: passed.
-   `ctest --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '(DepthCulling|VRHybridCulling|D3DContextProtection)' --output-on-failure`: 12/12 passed in 9.40 seconds; WARP 9.13 seconds.
-   New WARP coverage permutes all six axis orders and eight sign choices
    of the same sloped box. All 48 representations require the same local
    proof and retain clear, masked and invalid-depth holes in either eye.
    Existing source-pixel/ray oracles and production/diagnostic parity pass
    in Standard and reversed test orderings.
-   Strict FXC O3 compilation with warnings as errors passes. The maintained
    shader-refactor comparison against `ea7615fd8` compiles all four
    production/diagnostic and Standard/reversed variants, but returns 2:
    all four DXBC outputs differ. Bytecode equivalence is not claimed.

For the Standard production shader, indexed temporary arrays fall from
12 / 160 float4 entries to 10 / 144 entries. Ordinary temporaries rise
from 21 to 35 and approximate instruction slots from 2,242 to 2,340.
The shortcut trades setup and branches for fewer repeated clipping loops;
these static counts do not establish a hardware speedup or lower register
pressure. A fresh game comparison is still required. Evidence is under
worktree `build/astra-validation/adaptive/face-fastpath-*`.

## Adaptive implementation

The four-load coarse proof remains first. Successful roots are retained,
and failed roots reuse their sampled depths. A depth-first traversal
subdivides only cells whose global or clipped-face proof is inconclusive.
Only children intersecting the guarded base rectangle are visited.
Successful cells are never revisited at a finer level. All pending cells
must be proven hidden before either eye can report occlusion.

The work budget remains 64 actual depth loads per eye, including all four
initial reads. It no longer charges unvisited portions of whole grids.
A fixed 40-entry stack holds 12-bit x/y coordinates and the mip index.
At most four roots and three pending siblings per level are required;
the admitted maximum of 12 mip levels fits within that capacity.
Budget or stack exhaustion fails visible, as does an unresolved finest
cell. The 4x4 base reduction, face clipper, guards, depth bias, masks,
history, native fallback and both-eye agreement remain unchanged.

This can use the fixed budget more effectively, but it cannot recover
depth detail discarded by the base reduction. Traversal bookkeeping,
shader divergence and face clipping can still cost more than they save.
The measured implementation still trails Advanced, as recorded above.

## DevBench diagnostics

`communityshaders.menu` adds
`set_depth_culling_traversal_diagnostics_enabled`, with boolean `enabled`
and the usual exact `expectedBuildId`. It defaults off and also requires
depth-culling telemetry enabled. Reset telemetry after changing it.
This selects an instrumented shader permutation with one uint4 per
candidate: two packed eye reasons, summed depth loads, face regions and
face-triangle iterations. There are no global shader atomics.

`hybrid.traversalDiagnostics` exposes availability, enable state, accepted
sampled batches/objects, eye reasons and work totals. Reasons are
`not_tested`, `occluded`, `clip_crossing`, `viewport_guard`,
`invalid_input`, `depth_budget`, `finest_unresolved` and
`stack_capacity`. Only the first decisive reason per attempted eye is
reported; the second eye is skipped when the first retains visibility.
The reason histogram therefore includes two entries per sampled object,
including skipped eyes. Finest-unresolved includes insufficient depth
evidence and does not distinguish real visibility from masked/clear depth.

A separate staging copy precedes the native result copy. After matching
accepted history, the shared try-only renderer-ownership/readback helper
validates every record against native visibility before publishing totals.
Not-ready includes lock contention and GPU readiness. Submitted, unavailable,
failed and discarded batches are separate counters; missing samples are
never zero work. Diagnostic setup failure reports `setup_failed`, releases
partial diagnostic resources and keeps normal Hybrid culling active.
Reset/toggle generations exclude older pending records. Resources and
context owners are retained safely and bindings are restored.

All additional host resources, copies, maps, counters and shader-selection
code are inside `DEVBENCH_BRIDGE_ENABLED`. Production compiles the shader
without `CSX_HIZ_DIAGNOSTICS`; it has no diagnostic UAV or counters.
For GPU pass captures, enable existing telemetry/profiling but leave
traversal diagnostics off. For fpsVR timing, disable both diagnostics and
telemetry. Existing BuildHierarchy, TestBounds and CopyResults GPU scopes
remain available.

## Implementation validation

Focused tests compare the diagnostic and production shader's visibility
for every fixture in Standard and reversed test ordering. Runtime remains
Standard Z. Reflection requires the extra UAV only in the diagnostic
permutation. The existing source-pixel and independent 3D ray oracles,
stereo holes/masks, local-depth, viewport and clipping checks remain.

A fixture previously blocked by whole-grid budgeting now proves hidden
within the actual budget. A sloped-depth stress fixture must retain
visibility at exactly 64 first-eye reads, with the second eye untested.
A 12-mip thin-eye fixture exercises packed high cell coordinates, complete
deep traversal and a visible second-eye leaf. CPU tests reject malformed
records, impossible short-circuit work and mismatched visibility before
publishing a batch, and preserve reasons, skipped eyes and missing samples
through the DevBench serializer. Test shader bytecode is reused between
fixtures to avoid repeatedly compiling identical permutations.

Validation commands and results:

-   `pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_hybrid_culling_shader_test menu_depth_culling_diagnostics_test d3d_context_protection_test --parallel 4`: passed.
-   `ctest --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '(DepthCulling|VRHybridCulling|D3DContextProtection)' --output-on-failure`: 12/12 passed in 8.57 seconds; WARP 8.02 seconds. Renderer-ownership tests include contention and exception-safe release.
-   `pwsh ./build/astra-validation/adaptive/validate-production.ps1`: actual compiler flags and forced header retained; OFF syntax and diagnostic-absence checks passed for Hybrid, Temporal and Menu bridge.
-   `pwsh ./tools/pre-commit.ps1 run --files <14 changed files>`: applicable whitespace, line-ending, clang-format and prettier checks passed; unrelated hooks skipped.
-   `pwsh ./build/astra-validation/adaptive/build-aio.ps1` runs the universal Release DLL build, producer verification and full AIO validation. Exact results and compiled identity are recorded in its delivery receipt.

The universal DLL, focused tests, scoped hooks, production compiler audit
and AIO receipts are retained under worktree
`build/astra-validation/adaptive/`. A separately linked production DLL
has not been run; current in-game evidence is recorded above.

## Adversarial review

Scope remains PR104's optional Hi-Z backend and its DevBench diagnostics.
No PBR grass, grass optimization, Reverse Z, render-scale behavior or
default setting changes are included.

The review found and corrected these issues before packaging:

-   The initial diagnostic reader used a blocking renderer lock despite a
    nonblocking Map. It now reuses `TryReadbackWithRendererOwnership`, whose
    tests cover lock contention, mapping failure and exception-safe release.
-   Diagnostic shader/resource setup originally shared the main pipeline's
    failure path. Its failure is now isolated, explicitly reported and
    leaves normal Hybrid culling operational.
-   Diagnostic accounting omitted submissions, unavailable setup and a
    pending record overwritten by a later dispatch. Those outcomes are now
    counted; repeated setter calls no longer invalidate pending records.
-   The new decoder duplicated the native capacity limit and accepted
    impossible first-eye-only work. It now uses the existing policy limit
    and rejects malformed visibility, per-eye budget contradictions and
    reserved reason bits before publishing totals.
-   The traversal fixtures did not reach the admitted mip limit. The new
    deep thin-eye fixture covers all 12 levels and a high-coordinate
    second-eye visibility hole.
-   The new DevBench setter now explicitly rejects non-VR runtimes.

The source review checked child coverage, coarse-depth reuse, stack
capacity, actual-load accounting, diagnostic/production parity, native
result ownership, resource lifetime, reset generations and failure paths.
Focused tests and the production compiler audit support those findings.
Diagnostic setup-failure recovery has source review, not injected in-game
failure evidence. Hardware performance and method transitions were
measured above.
Moving-view/lifecycle correctness remains unqualified.

Further optimization should target the measured bounds-testing cost.
Keep PR104 experimental and Advanced as the default. Any speedup still
requires stereo motion and lifecycle qualification before promotion.
