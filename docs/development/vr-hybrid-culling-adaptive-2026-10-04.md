# Hi-Z traversal and face tests: 4 October 2026

The latest in-game comparison measures the cached-face implementation.
It still trails Advanced. The next implementation adds conservative
triangle-plane proofs and early unresolved-vertex checks before exact
clipping; its runtime performance is not yet measured.

## Latest same-build in-game comparison

Two 20-second fpsVR windows per method ran in Advanced / Hybrid / Hybrid /
Advanced order. Noon was reset and verified before every counter and
timing phase, with five seconds of settling. The scene remained
WhiterunExterior01 with SkyrimClearTU weather. DLSS Quality K used
1344x1492 render pixels per eye and 2016x2240 display pixels.
Boundary HMD observations differed by at most 0.497 mm and 0.0503 degrees;
these observations do not continuously certify pose between boundaries.

| Mode     | Run                   | Samples | CPU mean ms | GPU mean ms |
| -------- | --------------------- | ------: | ----------: | ----------: |
| Advanced | face-advanced-timing1 |    1541 |    9.949384 |    7.827904 |
| Hybrid   | face-hybrid1          |    1471 |   11.243508 |    9.151734 |
| Hybrid   | face-hybrid2          |    1417 |   11.084615 |    9.181087 |
| Advanced | face-advanced2        |    1653 |    9.815487 |    7.718633 |

Giving each window equal weight, Advanced averaged **9.882435 ms CPU /
7.773268 ms GPU**; Hybrid averaged **11.164062 / 9.166410 ms**. Hybrid
costs **1.281626 ms CPU (+13.0%) and 1.393142 ms GPU (+17.9%)**.
At 120 Hz, these differences use 15.4% and 16.7% of the 8.333 ms budget
respectively; CPU and GPU time overlap. The fpsVR process and start
identity remained the same as the preceding 120 Hz campaign.

Telemetry, traversal diagnostics and profiling were disabled during
timing. All four windows retained covering monotonic fpsVR records,
stable runtime identity, unchanged renderer settings, no shader failures
and no detected compiler activity. No pictures were taken because both
repeats showed a material performance gap. This is one static scene;
motion and lifecycle fidelity remain unqualified.

The first preparation stopped before timing because an exact player
position comparison rejected a 0.005859375 game-unit height change.
Its completed counter window was retained. Subsequent timing boundaries
use an explicit 0.1 game-unit position tolerance, retaining raw positions.
No failed timing window was silently substituted.

Separate 20-second counter windows reported:

| Mode     | Culled records | Tested records | Rejection | Batches |
| -------- | -------------: | -------------: | --------: | ------: |
| Advanced |      3,620,684 |      6,619,671 |     54.7% |   1,782 |
| Hybrid   |      1,853,021 |      5,835,727 |     31.8% |   1,552 |

Hybrid recorded zero fallback, invalidated or unreadable batches. These
are repeated candidate-result observations, not unique objects or matched
persistent cohorts. Lower rejection can leave more geometry to draw,
but these counters do not isolate its contribution to frame time.
Earlier binaries were not retested in this process and the player
position differs from the preceding campaign; cross-build speedup is
therefore not established.

Measured clean source: `d6115b0d7146cc1b45989f9e0bc6e4d29c9454f3`.
Producer Build ID:
`edab108772212f745ea94f1caaf043700129bae2a2a587dda9e80b6e727e0465`.
The enabled physical DLL, adjacent manifest, AIO receipt and six depth
shaders matched the producer and source; no competing enabled loose,
Overwrite or unmanaged Data provider was found at those paths.

Raw evidence is retained in main-workspace
`build/astra-runtime/20261004T021030Z-hiz-facecache/`, including
`final-comparison.json`, original/scoped CSVs, counter windows,
boundary snapshots and `optimization-analysis.json`. The original
DLAA profile, Advanced method and telemetry state were restored;
the profiler and owned fpsVR captures were inactive. The engine's
`qqq` command then closed PID 22960 cleanly, with no Skyrim process
remaining and RootBuilder deployment cleared, before implementation began.

## Measured optimization opportunity

Two normal-shader Hybrid captures and one Advanced capture each resolved
300 GPU frames. Telemetry was enabled, traversal diagnostics disabled,
and captures recorded no query-slot refusals.

| Hybrid GPU self time        | Capture 1 ms | Capture 2 ms |
| --------------------------- | -----------: | -----------: |
| Build base                  |     0.024896 |     0.026845 |
| Reduce mips                 |     0.026754 |     0.025337 |
| Hierarchy parent remainder  |     0.000651 |     0.000680 |
| Test bounds                 |     0.926044 |     0.956116 |
| Copy results                |     0.007653 |     0.004501 |
| Visibility parent remainder |     0.005194 |     0.005099 |
| Sum of disjoint self times  |     0.991192 |     1.018577 |

Bounds testing accounts for **93.4-93.9%** of measured Hybrid GPU work.
The parent rows are exclusive remainders. These diagnostic captures are
separate from fpsVR timing and cannot be subtracted mechanically from it.
The Advanced capture lacks an equivalent native depth-culling GPU timer.

Counter-window CPU means were 18.07 microseconds for Hybrid dispatch,
2.89 for preparation, 10.76 for post-native history validation, and
4.18 for intercepted native readback. These inclusive stages must not be
summed. They do not support a readback-stall explanation for the 1.28 ms
whole-frame CPU gap.

The bounded next target is triangle clipping inside the bounds test.
A visible original vertex inside the guarded region already disproves
that region's occlusion. Conversely, an affine triangle-depth proof over
an enclosing rectangle can prove it hidden without constructing a clipped
polygon. Both shortcuts retain exact clipping for ambiguity, preserve the
same depth bias and avoid additional persistent plane arrays.

Hierarchy fusion has a much smaller measured ceiling: the entire
hierarchy costs only about 0.053 ms. Finer source-depth evidence could
improve rejection, because the 4x4 maximum-depth base permanently loses
sub-cell detail, but this run does not establish its benefit or cost.
It remains a later experiment rather than an unmeasured budget increase.

The direct connector still omits
`set_depth_culling_traversal_diagnostics_enabled`, although the loaded
DLL reports diagnostics ready. No unsupported dispatcher bypass was
used. Depth-budget, finest-unresolved, clipping and viewport reason
proportions remain unmeasured. The existing local feedback record
`AUTO-20261004-013128725-B1AC7F0D` now includes this build's evidence.

Raw render-scale status/preparation responses are preserved. This build
does not expose all resource-publication qualification fields, so no
complete publication, VRAM or lifecycle qualification is claimed.

## Region depth shortcuts

Before clipping an overlapping triangle, an original vertex inside the
expanded region that fails the existing guarded depth test makes that
region unresolved immediately. Inclusive rectangle boundaries preserve
the clipper's coverage convention. This only avoids work before refinement
or retained visibility; it cannot create a new occlusion result.

A second shortcut intersects the region with the triangle's bounding
rectangle and treats that rectangle as a superset of covered points.
For triangle edges e and f, their cross product n defines the affine
depth plane. After orienting n.z positive, the shader evaluates the
minimum signed plane-depth residual over that rectangle without dividing
by n.z. It uses the same rounded guarded scene-depth threshold as
`DepthOrder::IsBehindWithBias`, including reversed test ordering.

The shortcut accepts only a residual greater than its scaled arithmetic
error. Absolute cross-product terms bound cancellation; maximum rectangle
displacements and the depth difference scale their contribution to the
residual. A separate area-sign check rejects uncertain orientation.
Both filters use 64 float unit roundoffs, with a 1e-20 absolute floor
covering amplified subnormal/flush-to-zero error in the admitted
0..16384 pixel and 0..1 depth domain. Nonfinite or uncertain results fall
through to the unchanged exact clipper. Each triangle keeps its own plane;
no planar quad or front-face assumption is introduced.

The four-load coarse test, six cached faces, depth bias, region expansion,
64 actual reads per eye, masks, history validation and both-eye agreement
remain unchanged. Existing DevBench diagnostics and their production
build isolation are unchanged. No grass, Reverse Z or render-scale
behavior is added.

Validation for these shortcuts:

-   `pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_hybrid_culling_shader_test --parallel 4`: passed.
-   `ctest --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '(DepthCulling|VRHybridCulling|D3DContextProtection)' --output-on-failure`: 12/12 passed in 10.39 seconds; WARP 10.12 seconds.
-   Thirty direct production-helper WARP cases per depth ordering cover
    successful analytic proofs, winding, large coordinates, threshold
    equality/adjacent floats, near-degenerate and tiny triangles, and
    inclusive vertex boundaries. Independent local-bias cases retain
    visibility in either eye. Ten near-parallel boxes extend the existing
    double-precision ray oracle and must have actual ray evidence.
-   The existing 48 signed-axis representations, source-pixel oracle,
    stereo holes and production/diagnostic visibility parity pass.
    Shader reflection still excludes the diagnostic UAV from production.
-   Strict FXC `/Ges /WX /O3 /T cs_5_0` compilation passed. The maintained
    comparison against `d6115b0d7` compiled all four production/diagnostic
    and Standard/reversed variants and reported different DXBC.
    Bytecode or bit-for-bit visibility equivalence is not claimed.
-   Independent numerical review found no blocker. A supplementary seeded
    float32/flush-to-zero check covered 30,000 cases: 8,442 positive proofs
    and no unsafe positives against its float64 reference. This is bounded
    numerical evidence, not an exhaustive proof or hardware benchmark.

Evidence is retained under worktree
`build/astra-validation/adaptive/plane-*`. Standard production FXC output
uses 36 ordinary temporaries, the same 10 indexed arrays / 144 float4
entries, and approximately 2,520 instruction slots. The longer static
program trades arithmetic for fewer clipping paths; these counts do not
establish runtime speed or register spilling.

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
pressure. The in-game comparison above measures this cached-face change.
Its earlier compile/test evidence remains under worktree
`build/astra-validation/adaptive/face-fastpath-*`.

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
