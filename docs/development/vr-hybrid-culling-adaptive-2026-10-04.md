# Hi-Z adaptive traversal: 4 October 2026

## Latest in-game comparison before this change

The projected-face AIO still regressed against native culling. The user
requested fresh baselines after briefly opening a menu, then requested
results without further menu checks. These are the final baseline repeats
and the retained completed Hybrid window, each timed for 20 seconds after
resetting noon. Counts were captured separately; telemetry was disabled
during fpsVR timing. No images were taken, as requested for a slow Hybrid.

| Mode     | Run       | CPU mean ms | GPU mean ms | Culled records / tested records |
| -------- | --------- | ----------: | ----------: | ------------------------------- |
| Off      | off3      |   20.650621 |   13.109317 | Not applicable                  |
| Legacy   | legacy3   |   10.968468 |    8.127703 | 3,886,871 / 6,250,496 (62.2%)   |
| Advanced | balanced3 |   11.543736 |    8.163105 | 3,564,985 / 5,791,744 (61.6%)   |
| Hybrid   | hybrid2   |   15.587654 |    9.835009 | 1,578,839 / 4,624,384 (34.1%)   |

Hybrid's additional 4.04 ms CPU and 1.67 ms GPU versus Advanced consume
48.5% and 20.1% of the 120 Hz frame budget respectively. CPU and GPU
durations overlap. These are candidate-result records, not unique objects
or matched persistent cohorts; counts do not establish rendering cost.
All 1,129 Hybrid batches were accepted, with no fallback or invalidation.

This remains a single valid Hybrid timing window versus the latest
baseline repeats, with baseline variation across the campaign. The
earlier Hybrid timing failed timestamp monotonicity; the first Legacy
and Off timings were excluded after the brief menu opening. No additional
timeline/menu audit was performed after the user's request to stop those
checks. No current-shader motion or image-quality qualification is claimed.

The observed scene was WhiterunExterior01 with SkyrimClearTU, DLSS Quality
K and render scale enabled: 1344x1492 rendered pixels per eye, displayed
at 2016x2240. The preserved fpsVR session identity attributes the 120 Hz
Index setting. Advanced was left selected and Info logging unchanged.

Measured implementation: `5951a47560efc2ad5c7634682ea51f5b9a48a057`.
Compiled source: `6423b3e8f03337324f560afd6b467ee1b1b709d2`, dirty digest
`c14f7ec67fa3f163da489283ce0c5c7b809270bec8912ee4cd82c69f8c5239f4`.
Producer Build ID:
`5285d5b836772a2745efd8a0db078bad59b8698fa69c52df3dcccf6a7522673c`.
Complete local evidence remains under main-workspace
`build/astra-runtime/20261003T233058Z-hiz-faces/`, including exact per-run
summaries, counter snapshots, timing receipts, CSVs and exclusions.
The table does not measure the adaptive implementation below.

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
No performance improvement is claimed before a new runtime comparison.

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

## Validation and next measurement

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
and in-game adaptive measurements have not been run.

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
failure evidence. Hardware performance, live setting transitions and
moving-view/lifecycle correctness remain runtime gates.

The next runtime comparison uses this candidate and the preserved
projected-face AIO plus Off, Legacy and Advanced, with noon resets.
Capture reasons separately from GPU pass and whole-frame timing.
Require an improvement beyond baseline variation before investing in
further refinement. A performance gain still requires stereo motion and
lifecycle qualification; Advanced remains the default.
