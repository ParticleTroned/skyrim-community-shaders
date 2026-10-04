# Hi-Z traversal and face tests: 4 October 2026

The latest in-game comparison measures the triangle-plane shortcuts.
Hybrid still trails Advanced. The next implementation checks the nearest
projected vertex against a finest-level depth cell before face refinement.
That early exit's in-game speed and rejection impact remain unmeasured.

## Latest same-build in-game comparison

Two 20-second fpsVR windows per method ran in Advanced / Hybrid / Hybrid /
Advanced order. Noon was reset and verified before every counter and
timing phase, with five seconds of settling. The scene remained
WhiterunExterior01 with SkyrimClearTU weather, using loaded Save 22.
DLSS Quality K used 1344x1492 render pixels per eye and 2016x2240 display
pixels throughout. Boundary HMD observations differed by at most 1.303 mm
and 0.0858 degrees; these are not continuous pose certification.

| Mode     | Run                    | Samples | CPU mean ms | GPU mean ms |
| -------- | ---------------------- | ------: | ----------: | ----------: |
| Advanced | plane-advanced-timing1 |    1613 |    9.865406 |    7.732238 |
| Hybrid   | plane-hybrid1          |    1352 |   11.499038 |    9.199482 |
| Hybrid   | plane-hybrid-timing2   |    1330 |   12.525865 |    9.310602 |
| Advanced | plane-advanced2        |    1618 |    9.994314 |    7.761187 |

Giving each window equal weight, Advanced averaged **9.929860 ms CPU /
7.746712 ms GPU**; Hybrid averaged **12.012452 / 9.255042 ms**. Hybrid
costs **2.082592 ms CPU (+21.0%) and 1.508330 ms GPU (+19.5%)**.
Hybrid CPU means varied by 1.027 ms between repeats, so its CPU estimate
is less stable than the GPU comparison. Both repeats still trail Advanced.
Relative to the campaign's nominal 120 Hz / 8.333 ms budget, the deltas
are 25.0% CPU and 18.1% GPU; these overlapping times must not be added.
The unchanged fpsVR process identity is retained, but the quiet-window
CSV did not independently report the headset refresh rate.

Telemetry, traversal diagnostics and profiling were disabled during
timing. All four windows retained covering monotonic fpsVR records,
stable runtime identity, unchanged renderer settings, no shader failures
and no detected compiler activity. No pictures were taken because
performance was not comparable. Motion/lifecycle fidelity remains open.

Separate completed 20-second counter windows reported:

| Mode     | Window          | Culled records | Tested records | Rejection | Batches |
| -------- | --------------- | -------------: | -------------: | --------: | ------: |
| Advanced | plane-advanced1 |      4,135,931 |      7,140,240 |     57.9% |   1,787 |
| Hybrid   | plane-hybrid1   |      2,171,752 |      5,928,946 |     36.6% |   1,463 |
| Hybrid   | plane-hybrid2   |      2,006,342 |      5,611,520 |     35.8% |   1,370 |

Hybrid recorded zero fallback, invalidated or unreadable batches in both
counter windows. These are repeated candidate-result observations, not
unique objects or matched persistent cohorts. The second Hybrid window
averaged 4,096 candidates per batch, the native capacity. These counters
do not isolate how lower rejection or capacity pressure affects frame
time. Different position, view and process from the previous campaign
prevent an isolated cross-build speedup claim.

Measured clean source: `c1f3fd310891d4637112e9cb7361fa8191ef1903`.
Producer Build ID:
`108a42ba1904c7d7694e317f2ada8421683cbe931f05895f647d0e7d1aaa44d8`.
The enabled physical DLL, adjacent manifest, AIO receipt and six depth
shaders matched the producer and source; no competing enabled loose,
Overwrite or unmanaged Data provider was found at those paths.

Raw evidence is retained in main-workspace
`build/astra-runtime/20261004T044645Z-hiz-plane/`, including
`final-comparison.json`, original/scoped CSVs, counters, boundary
snapshots and `optimization-analysis.json`. Two approval-blocked noon
setups never began timing; their completed counters were preserved and
used separately. Inactive-capture proofs preceded the replacement timing
windows. No failed timing samples were substituted into the averages.

Advanced, the original DLSS Quality profile and telemetry preference were
restored, with profiler and owned fpsVR captures inactive. The engine's
`qqq` command closed PID 29344 cleanly at 05:03 UTC; no Skyrim process
remained and RootBuilder deployment was cleared before implementation.

## Measured optimization opportunity

Two normal-shader Hybrid captures and one Advanced capture each resolved
300 GPU frames. Telemetry was enabled, traversal diagnostics disabled,
and captures recorded no query-slot refusals.

| Hybrid GPU self time        | Capture 1 ms | Capture 2 ms |
| --------------------------- | -----------: | -----------: |
| Build base                  |     0.025038 |     0.026396 |
| Reduce mips                 |     0.025429 |     0.026941 |
| Hierarchy parent remainder  |     0.000655 |     0.000654 |
| Test bounds                 |     0.659497 |     0.673608 |
| Copy results                |     0.005498 |     0.005339 |
| Visibility parent remainder |     0.005301 |     0.004442 |
| Sum of disjoint self times  |     0.721418 |     0.737379 |

Bounds testing accounts for **91.4%** of measured Hybrid GPU work.
The parent rows are exclusive remainders. Diagnostic captures are separate
from fpsVR timing and cannot be subtracted mechanically from it. The
Advanced capture lacks an equivalent native depth-culling GPU timer.

Hybrid counter-window CPU means were 20.14-20.96 microseconds for dispatch,
3.08-3.15 for preparation, 10.50-10.66 for post-native history validation,
and 4.02-5.13 for intercepted native readback. Inclusive stages must not
be summed. They do not support a readback-stall explanation for the
whole-frame CPU gap.

The next bounded target is work spent refining candidates that cannot be
proven hidden. A nearest-vertex depth witness can retain such a candidate
before face preparation, plane arithmetic and polygon clipping.
This only targets query cost; it does not improve the rejection gap.
Parity is not established or predicted from these pass timings.

Hierarchy fusion has a smaller measured ceiling: base and mip work
total only 0.05 ms. A 2x2 base could retain more occluder detail than the
current 4x4 base, but would increase hierarchy size and dispatch work.
Its cost and rejection benefit remain unmeasured, so it remains a
separate experiment. Higher traversal limits are likewise not justified
without the missing termination/work distributions.

The direct connector still omits
`set_depth_culling_traversal_diagnostics_enabled`, although the loaded
DLL reports diagnostics ready. No unsupported dispatcher bypass was
used. Depth-budget, finest-unresolved, clipping and viewport reason
proportions remain unmeasured. Local feedback
`AUTO-20261004-013128725-B1AC7F0D` includes this build's evidence.
Raw render-scale status/preparation responses are preserved; this build
does not expose complete resource-publication qualification fields.

## Nearest-vertex precheck

After an inconclusive four-load coarse proof, sample the finest hierarchy
cell containing the nearest projected box vertex. If this point cannot
pass the existing depth-bias comparison, retain the object immediately.
That cell's ancestors are at least as conservative, and every face region
covering the vertex would also retain it. A hidden vertex alone never
proves the whole box hidden; the ordinary face traversal still runs.

The sample uses the existing 64-actual-read budget and is reused if
traversal reaches its leaf. An initial mip-zero sample is reused directly
from the four coarse reads. A successful coarse proof incurs no new
texture read. Bounds, masks, stereo, temporal validation, depth bias,
the 4x4 base and exact clipping remain unchanged.

A surviving candidate can spend one read on a cell that traversal never
needs, so budget-limited rejection can decrease. This is a measured-next
tradeoff, not a claim of bitwise visibility equivalence or parity.
The DevBench-only `nearest_unresolved` termination reason distinguishes
this precheck from ordinary `finest_unresolved` refinement. Diagnostic
decoding rejects impossible early-check work and publishes the new reason
through the existing narrow snapshot. Production has no new diagnostics.

Validation includes both depth-order test permutations and both eyes,
all four supported source reductions, clear/masked/nonfinite source
depth, bias retention, coarse-leaf reuse, successful coarse proofs and
a visible region away from a hidden witness. Existing source-pixel/ray
oracles, signed-axis permutations, near-parallel faces, 64-read exhaustion
and maximum mip depth remain covered. The local-bias fixture's patch is
away from the witness so it still reaches actual face refinement.

Validation for the nearest-vertex change:

-   `pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_hybrid_culling_shader_test menu_depth_culling_diagnostics_test --parallel 4`: passed.
-   `ctest --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '(DepthCulling|VRHybridCulling|D3DContextProtection)' --output-on-failure`: 12/12 passed in 12.92 seconds; WARP 12.33 seconds.
-   Strict optimized production/diagnostic shaders compile in Standard and
    reversed test ordering. The maintained comparison against `c1f3fd310`
    reports different DXBC, as expected; its shell exit was 1.
-   `pwsh ./build/astra-validation/adaptive/validate-production.ps1`:
    actual compiler flags/forced-header audit, production syntax and
    diagnostic-absence checks passed for Hybrid, Temporal and Menu bridge.
    No separately linked production DLL was run.
-   Shader reflection excludes the diagnostic UAV from production.
    Diagnostic tests preserve the new reason through decoding/serialization
    and reject impossible precheck work.

Exact evidence is retained under worktree
`build/astra-validation/adaptive/witness-*` and
`build/astra-validation/adaptive/production-20261004T051224488Z/`.
The new universal DevBench DLL/AIO is produced by `build-witness-aio.ps1`;
its delivery receipt records the compiled source and package result.
Its in-game performance and motion/lifecycle behavior remain unmeasured.

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
`invalid_input`, `depth_budget`, `finest_unresolved`, `stack_capacity`
and `nearest_unresolved`. Only the first decisive reason per attempted eye is
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
