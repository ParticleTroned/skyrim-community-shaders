# Experimental VR Hybrid Hi-Z culling

Hybrid is an optional Skyrim VR 1.4.15 visibility producer for comparison
with Advanced. Advanced remains the default. The first repeated noon
four-mode assay found Hybrid materially slower than Advanced and Legacy.
Limited static stereo review found no obvious missing solid geometry in
sampled previews; motion and lifecycle correctness remain unqualified.
See the [2026-10-03 runtime report](vr-hybrid-culling-runtime-2026-10-03.md).
The [current analysis](vr-hybrid-culling-guarded2-analysis-2026-10-04.md)
selects guarded proofs with 2x2 source-depth reduction for further work.
The [latest save-22 noon comparison](vr-hybrid-culling-clip-storage-results-2026-10-04.md)
averaged 11.68 ms CPU / 8.94 ms GPU against Advanced's 10.41 / 7.84 ms.
Separate GPU captures measured 0.591 ms Hi-Z culling versus 0.081 ms
Advanced. Bounds testing remains the dominant cost at 0.520 ms. Separate
candidate counters rejected 40.54% versus Advanced's 60.45% after recovery;
these are different cohorts.
Motion and lifecycle qualification remain open.

Hybrid now has one proof implementation: the compound guarded face test
and guarded triangle, retained-vertex, plane and clipping proofs. It
prefers 2x2 reduction, retaining 4x4 only when the validated source would
exceed pyramid resource limits. Proof and coarse-depth A/B selectors and
their extra shaders are removed. Advanced remains
the default while Hi-Z performance and runtime correctness are evaluated.

Projection and region testing use one private per-invocation vertex array.
Projection initializes every vertex for the current eye before any region
test; the face helpers only read it. This avoids passing the complete array
by value on every region test. Guarded arithmetic and coverage are retained;
The tested implementation also indexes prepared face metadata directly and
alternates between two clipping banks, avoiding survivor copies after each
plane. Clipping order, interpolation and guarded comparisons are unchanged.
The latest runtime comparison still trails Advanced.

This implementation uses conventional scene depth: near is zero, far is
one, and each pyramid cell stores the maximum covered depth. It does not
convert Skyrim to Reverse Z or recover precision lost in the scene depth
buffer. A future Reverse Z renderer would require coordinated depth
production, reduction, comparison and compatibility changes.

## Native integration

Skyrim retains object collection, 64-byte affine OBBs, index assignment,
the GPU OBB buffer, result UAV, staging buffer and CPU result pointers.
The [verified native contract](vr-hybrid-culling-native-contract.md)
describes the Skyrim VR executable and inspected layouts. An index is
valid only within its submission; it is not a persistent object identity.

Hybrid suppresses only the depth-copy draw inside the native downscale
routine. The outer routine still publishes its depth-ready state and
camera snapshots. After validated preparation, Hybrid builds its own
hierarchy and replaces the native OBB draw. If replacement cannot run,
the native downscale is replayed before the native OBB test, and Advanced
recovery remains available. The two producers are not intentionally run
together for normal comparison.

The compute test writes every active native result index, then copies
the result resource to the existing native staging resource. The normal
readback hook consumes that submission before the engine resets its
collection. Ordinary operation adds no readback or synchronous GPU query.
The native staging map itself can still block if the GPU is late.
An independently enabled DevBench diagnostic permutation adds a separate
bounded result copy and nonblocking Map after accepted native readback.
It never changes native visibility, and is disabled for timing.

D3D11 context-state isolation restores the engine's bindings after the
compute work. New resources use the shared naming helper and RAII.
Resources and bounds are checked before allocation, binding or dispatch.
The implementation does not install its native hooks on SE or AE.

## Conservative stereo hierarchy

The source is the effective `kPOST_ZPREPASS_COPY` depth SRV used by the
native downscale. Terrain Blending's existing guard disables its blended
depth alias while VR depth culling is enabled. Supported
inputs are single-sample, double-wide stereo textures with the expected
full eye viewports and conventional depth range. Unsupported packed
dynamic-resolution layouts use the native producer with Advanced recovery.

`BuildDepthCS` reduces all pixels in each 2-by-2 source region. Sources
that would exceed the 4096-texel pyramid limit use 4-by-4 reduction,
preserving the existing 16384 source-dimension and 12-mip limits. Each eye
gets a separate texture-array layer; its base dimensions are padded to
powers of two. Zero-depth VR masks, invalid samples, incomplete edge
regions and padding become far depth. `ReduceDepthCS` then takes the
maximum of each covered 2-by-2 region. This hierarchy is independent of
the averaged GI depth pyramid. It stops when both dimensions are at most
two, because the visibility test never needs a coarser level; a 1-by-1
base remains valid.

At 1344x1492 per eye, finer depth adds 8 MiB of logical hierarchy storage.
It can recover occluder detail but adds hierarchy and potential traversal
work. The selected reduction and logical bytes remain observable through
the existing DevBench source snapshot. The latest comparison confirms
the finer layout is active, but performance still trails Advanced.

`TestBoundsCS` projects all eight OBB corners using each eye's actual
matrix and camera adjustment. Camera adjustment is subtracted from the
OBB translation before corner multiplication, preserving small extents
at large world coordinates. Near/eye-plane crossings, far-plane
crossings, nonfinite data and non-affine bounds retain visibility.

Projected rectangles include a pixel guard margin. If that margin leaves
the eye viewport, visibility is retained. The shader selects a mip at
which the complete rectangle overlaps at most four cells and reads every
one of them. If this coarse proof is inconclusive, it retains individually
proven roots and descends only into unresolved cells. The original four
samples are reused. A 40-entry depth-first stack holds packed cell/mip
coordinates; children are restricted to the guarded base-cell bounds.
Before face preparation, the nearest projected vertex is checked against
its finest-level cell. Failure retains visibility immediately; success
still requires the complete traversal. The sample is reused at its leaf
or taken from the coarse reads when the initial mip is zero.
The 64-load limit counts actual reads, including the initial four and this
precheck. Its extra read can reduce budget-limited rejection when that
leaf would otherwise never be visited. DevBench reports these early exits
as nearest_unresolved, separately from unresolved refinement leaves.
Budget or stack exhaustion, invalid depth and unresolved finest cells
retain visibility. Only completing all pending regions proves occlusion.
Within an inconclusive cell, projected box faces are tested against the
cell expanded by the pixel guard and a rounding margin. Cached bounds,
retained original vertices and conservative affine depth proofs can settle
a triangle without clipping. Whole-face and triangle minima, retained vertices, bounded plane proofs
and clipped intersections all use the configured depth bias plus the
64-depth-unit interpolation allowance. The coarse box proof retains its
configured base bias. Uncertain cases keep exact clipping.
Every covered region must prove occlusion
in both eyes.
Visibility in either eye retains the object. Coarse cells, depth within
the guarded footprint and the finite refinement budget can still reduce
rejection efficiency; they cannot justify discarding
a potentially visible object in the captured view. An explicit shader
branch skips testing the second eye when the first already retains it.

## Temporal behavior and limitations

Before using a Hybrid readback, CSX matches the culler, frame age,
rendering epoch, result selector/address, object count, transform address
and exact submitted transform contents. It also checks render dimensions,
source identity and rectangles, world-camera pose, each eye's pose and unjittered
projection. Mode changes invalidate the epoch.

Pose reuse uses the existing small-motion thresholds: at most 0.05 degrees
and 0.1 world unit. When a submitted batch becomes invalid and its native
result array is available, every hidden result becomes visible. There is
no 64-object promotion quota in this path.

These thresholds and the pixel margin are temporal heuristics. They do
not prove visibility under every sub-threshold translation or rotation,
and they do not solve independently moving occluders. The conservative
GPU proof applies to the captured depth and camera. Large or rapid head
motion may invalidate many batches, increase draws and regress frame
times. Objects with changing bounds and scene transitions need explicit
runtime testing.

## Selection and diagnostics

The VR Depth Culling selector exposes Advanced, Legacy and Hybrid Hi-Z
at normal Info logging as well as in Developer Mode.
Saved selection is independent of logging level. Numeric
`DepthCullingMethod` values are `0`, `2`, and `3`, respectively; retired
value `1` does not select Hybrid. Old `DepthCullingLegacyMode` settings
remain readable, and saving preserves that compatibility boolean.

Through `communityshaders.menu`, use:

```json
{ "action": "set_depth_culling_method", "method": "hybrid" }
```

Use `"balanced"` for Advanced or `"legacy"` for Legacy. Normal settings
save is required for persistence. Existing exterior/interior enable
switches and minimum object sizes remain independent of method selection.

`status.depthCullingTemporal.hybrid` reports `state`, `submittedBatches`,
`acceptedBatches`, `invalidatedBatches`, `fallbackBatches`,
`unreadableBatches`, `promotedObjects`, and `lastObjectCount`, plus
`effectiveBackend`, `fallbackReason`, `historyRejectionReason` and CPU
stage timings. Hybrid inherits the existing telemetry enable/reset
controls: disabling rejects new measurements, and a reset clears both
methods together or reports busy without clearing either. Operational
backend and failure reasons remain visible with measurement disabled.
See the [telemetry contract](vr-depth-culling-recovery-telemetry.md).

`depthCullingTemporal.engine` observes the engine's current
`depthBufferCulling` gate and `minimumOccludeeBoxExtent`, separately from
the desired `cullingEnabled` policy and configured location extents.
Each observation has its own availability flag. Values are null on
non-VR runtimes, before cached engine bindings initialize, for local
fallback storage, or for a nonfinite observed extent. These main-thread
reads do not force engine values or resolve new addresses. When native
batches are empty, inspect these observations before interpreting zero
rejections or profiling samples as successful culling work.

Enable `set_depth_culling_traversal_diagnostics_enabled` only for reason
and work measurements, with telemetry enabled, then reset the counters.
`hybrid.traversalDiagnostics` sums work across both eyes and reports
`planeProofs` (successful plane proofs), `polygonClips` (clipper entries),
and reserved `faceBiasOnlyProofs` / `triangleBiasOnlyProofs` fields, which
remain zero with guarded proofs. Plane and clip totals measure
intermediate work events, not independently recovered object rejections. Disable diagnostics for frame-time and GPU comparisons.

`clipPlanes` and `skippedClipPlanes` separate executed clipping planes
from planes containing every current survivor. `planeBuilds` / `planeReuses`
measure the four-entry invocation-private lazy plane cache. `refinedCells`,
`sourcePixels`, `resolvedCells` and `sourceWitnesses` identify selective
original-depth work. Pyramid and source reads share the 64-read eye budget;
refinement preserves the configured guard and strict depth allowance.

Enable `set_depth_culling_matched_diagnostics_enabled` in Advanced or
Legacy with telemetry enabled, then reset counters. The diagnostic path
runs Hi-Z into private visibility/staging buffers while native culling
continues to own displayed results. `hybrid.matchedDiagnostics` reports
four outcome buckets before and after native recovery, and decisive reasons
for native-only retention. Both paths use the same validated bounds batch,
frame, source identity and camera history. Compare `snapshotBatches` with
accepted `batches`; a busy/superseded snapshot may lag. Dropped/failed
readbacks are explicit and never fabricate matches. These are repeated
candidate records, not unique scene objects or draw counts. Disable this
switch and traversal diagnostics before timing either backend.

Viewport reasons are `viewport_offscreen` for wholly outside bounds,
`viewport_partial` for original bounds crossing the viewport, and
`viewport_guard` when only the guarded extent reaches outside. All three
retain visibility. Each retained object contributes one decisive reason;
an untested second eye is recorded separately. Freeze telemetry before
using totals and report diagnosed-object coverage and missing batches.
The 64-byte diagnostic records, their resources, extra comparisons and
readback are absent from production builds.

`status.depthCullingTemporal.hybridInstalled` identifies
whether the optional replacement hooks are available; failure to install
them retains native testing and Advanced recovery. Check effective culling enablement and producer state,
since selecting Hybrid does not prove every frame used it. The profiler
labels `VRHybridCulling::BuildHierarchy` and
`VRHybridCulling::Visibility` identify its GPU work.

The combined contained-plane/cache/refinement iteration passes 12/12
focused tests in 24.99 seconds (`validate-proof-ab.ps1`), including WARP
pixel/ray checks in 24.74 seconds. Four strict FXC permutations pass.
The normal shader uses 27 temporaries and 144 indexed entries, versus
23/132 in the last measured build; hardware occupancy and performance are
unmeasured. Selective refinement reuses the same region tester and polygon
storage. Production forced-header syntax/preprocessing checks exclude the
new shadow machinery. The universal DevBench Release DLL and full AIO
verification pass. No new runtime or motion qualification is claimed.

## Validation and acceptance

### Selected guarded 2x2 implementation

Select `hybrid` through `set_depth_culling_method`; no proof or reduction
selection is required. `hybrid.configuration` reports `proof: guarded`,
`preferredSourceReduction: 2`, `activeSourceReduction` and
`largeSourceFallback`. Active reduction is zero unless the current
culling epoch has submitted the effective Hybrid backend. A large source
reports four with `largeSourceFallback: true`. Source dimensions and
logical hierarchy bytes remain available in the source snapshot.

The next build uses the same guarded shader logic exercised by the
completed A/B assay. Normal culling and the independently controlled
traversal diagnostic permutation share that proof implementation. Shader
or resource setup failure retains native fallback; diagnostic setup
failure retains normal Hybrid culling. The removed comparison actions
are no longer advertised or accepted. Existing telemetry, traversal
reason counters and GPU profiler controls remain DevBench-only, compiled
out of production.

After installation, warm Hybrid, verify its effective backend and actual
reduction, then reset to noon and settle before each capture. Keep
traversal diagnostics off for timing, collect work and reason counters in
separate windows, and freeze telemetry before reading final totals.
The historical A/B records preserve their measured source identities and
are evidence for selecting this implementation; they do not validate a
subsequent DLL or replace motion/lifecycle qualification.

Initial guarded-only cleanup passed 12/12 tests and preserved all four
Standard/reversed, normal/diagnostic guarded bytecodes. After private
vertex storage was introduced, the maintained verifier found intentional
differences. Strict `/Ges /WX /O3` compilation confirmed both repeated
eight-vertex copies removed, indexed arrays 10 to 7, normal temporaries
37 to 31 and unchanged resource bindings/constant layouts
(`guarded-array-alias-strict-20261004T111651253Z`). Static instruction slots
increase by 30 in the normal shader; no runtime gain is claimed. Final
focused validation passed 12/12 tests in 18.90 seconds, including WARP in
18.64 seconds (`proof-ab-tests-20261004T111806880Z`).
Actual compiler flags and forced headers passed syntax and production
preprocessing checks for VR, Hybrid, Temporal and the menu bridge, with
the developer-only engine observations, diagnostics and comparison
markers absent (`guarded-only-production-20261004T110300952Z`). These
evidence directories are under `build/astra-validation/adaptive/`.
No separately linked production DLL or new in-game result is claimed.

### Historical original-vertex implementation validation

The following validates the earlier base-bias candidate, which is absent
from the selected guarded implementation.

The proof-bias iteration passed all 12 focused DepthCulling,
VRHybridCulling and D3DContextProtection tests in 15.32 seconds. The WARP
test compiles optimized, warnings-as-errors Standard/reversed variants,
with diagnostics on/off, and verifies matching visibility. New cases
bracket the base bias at 7/8/9/16/72/73 depth units, preserve equality,
exercise flat/sloped/sheared and reflected boxes, and check guarded rays
with an independent double-precision oracle. Separate cases validate
triangle-only shortcuts, viewport categories and the 32-byte record.
Host tests cover malformed, overflowing and contradictory work records.

Evidence: `build/astra-validation/adaptive/proof-bias-tests-20261004T074811570Z/`.
`validate-proof-bias.ps1` builds the 12 named targets and runs their exact
CTest selection. `validate-fine-production.ps1` passed all three changed
translation-unit syntax and preprocessing checks using actual compiler
flags and the generated forced header; source hashes stayed unchanged.
Evidence: `build/astra-validation/adaptive/production-20261004T074635786Z/`.
No separate production DLL link or new in-game performance/visual result
is claimed by these checks.

The portable policy tests cover safe dimensions, power-of-two padding,
constant validation, projection validity and dispatch limits. The WARP
test executes the production compute shaders and checks every pyramid
level against complete source coverage, odd eye sizes, masks, invalid
depth, stereo visibility, viewport margins, clipping, asymmetric
perspective and small bounds at large world coordinates. History and
settings tests cover batch correspondence and method migration.

Runtime acceptance requires controlled off/Legacy/Advanced/Hybrid comparisons on
the same build base, hardware, scene, settings and camera motion. Include
stationary views, rapid head rotation and translation, dense exteriors,
interiors, moving occluders, cell transitions, native resolution and
supported upscaling configurations. Inspect both eyes for disappearing
geometry and compare CPU/GPU frame times, P95/P99, spikes, memory and
batch rejection/fallback rates. Establish baseline variability before
classifying a result as neutral.

Performance must be neutral or better while correctness passes
separately. Incorrectly hidden geometry is not a valid speedup, and an
average gain cannot conceal repeatable scene or motion regressions. The
full source-depth reduction and additional visible draws after history
invalidation are explicit performance risks. Retain Advanced for A/B
and fallback until those requirements are demonstrated.

## Recorded local validation, 2026-09-25

The isolated branch starts at `origin/main-VR` commit
`df9f377a53d237518c1b671b7be1085c9a65b69d`. The universal Release build
passed with SE, AE, VR and DevBench enabled. The pre-commit validation
binary records that base as dirty, with these exact identities:

-   Build ID: `0b66202ce6e2b57d1581b75b1791f35ef84d9df34b0289a51ce0a8180fbbef8c`
-   Dirty digest: `de156ac99597acbdf06b3e2c14c4cba808dbe169f143da9c5a662ff44807edd4`
-   DLL SHA-256: `5e5b83f891b7c47eee45e39acc38862b323a1266453b5a106b0dfefa69b33f93`
-   DLL size: 29,343,744 bytes; the adjacent manifest hash and size matched.

From the task worktree, the successful build command was:

```powershell
pwsh ./tools/cmake.ps1 --build C:/src/skyrim-community-shaders/build/hiz --config Release --target CommunityShaders --parallel 4
```

The same build invocation passed for targets
`vr_depth_culling_settings_ui_test`, `vr_depth_culling_settings_test`,
`menu_depth_culling_settings_policy_test`,
`vr_depth_culling_temporal_policy_test`, `vr_hybrid_culling_policy_test`,
`vr_hybrid_culling_history_test`, and `vr_hybrid_culling_shader_test`.
All seven tests then passed, including the final large-coordinate,
sheared-box and one-dimensional mip-tail GPU regressions:

```powershell
ctest --test-dir C:/src/skyrim-community-shaders/build/hiz -C Release -R '^(VRDepthCullingSettingsUI|VRDepthCullingSettings|MenuDepthCullingSettingsPolicy|VRDepthCullingTemporalPolicy|VRHybridCullingPolicy|VRHybridCullingHistory|VRHybridCullingShader)$' --output-on-failure
```

All three compute shaders also passed `fxc /T cs_5_0 /E main /WX /Ges /O3`.
Scoped repository hooks passed for changed C++, HLSL, tests and Markdown.
The added CMake test block passed the pinned Gersemi check; formatting the
entire legacy CMake file introduced unrelated changes, which were removed.
Its existing custom-command formatting warnings remain visible in the
local record.

Logs and the exact dirty validation DLL/manifest are preserved under
`C:/src/skyrim-community-shaders/build/hiz/`, including
`evidence/dirty-validation`, `dll-build-final.log`,
`focused-test-build-final.log`, and `focused-tests-final.log`.

The full unrelated controller/shader suites were not run. The native hook
lifecycle, fallback replay, live context restoration, in-game stereo
appearance and CPU/GPU performance comparison have not been exercised in
Skyrim. No candidate was deployed into the existing running game.

The Info-level selector amendment on 2026-09-26 passed the existing
production-extracted UI test, updated to exercise all three methods at
both Info and Debug and retain the selection across logging changes:

```powershell
pwsh ./tools/cmake.ps1 --build C:/src/skyrim-community-shaders/build/hiz --config Release --target vr_depth_culling_settings_ui_test --parallel 4
ctest --test-dir C:/src/skyrim-community-shaders/build/hiz -C Release -R '^VRDepthCullingSettingsUI$' --output-on-failure
```

Result: 1/1 passed. Logs are `info-ui-test-build.log` and
`info-ui-test.log` in the same build directory. The DLL evidence above
predates this UI amendment; no DLL rebuild or deployment accompanied it.

## Adversarial review, 2026-09-26

The second review covered native batch lifetime and fallback ordering,
stereo shader coverage, D3D resource contracts, method selection,
DevBench controls, performance costs and shared utilities. Confirmed
issues fixed in the implementation:

-   HLSL boolean evaluation executed both eye tests. The explicit branch
    now skips the second when visibility is already established.
-   The final 1-by-1 reduction dispatch was unreachable by mip selection
    and has been removed, except where the base itself is 1-by-1.
-   Hybrid measurements now share Advanced's telemetry admission/reset
    gate, with RAII writers and no sampling code in non-DevBench builds.
-   Specific fallback reasons survive native replay. Backend reporting
    follows the producer epoch, so an old readback cannot impersonate a
    new submission after a method switch. Inactive methods suppress stale
    last-count diagnostics, without extra per-frame diagnostic writes.
-   GPU profiling begins after dispatch admission. CPU timings separately
    cover preparation, submission and readback, including failed attempts.
    Dispatch reuses validated immutable constants; a latched pipeline
    failure avoids repeatedly capturing and validating the source frame.

Method changes share an Info-level log through the central setter. The
DevBench schema describes the shared controls and new diagnostics, and
setter responses retain producer provenance and explicit non-persistence.

Eight focused tests passed after rebuilding their targets:

```powershell
ctest --test-dir C:/src/skyrim-community-shaders/build/hiz -C Release -R '^(VRDepthCullingSettingsUI|VRDepthCullingSettings|MenuDepthCullingSettingsPolicy|VRDepthCullingTemporalPolicy|VRDepthCullingTelemetryPolicy|VRHybridCullingPolicy|VRHybridCullingHistory|VRHybridCullingShader)$' --output-on-failure
```

Result: 8/8 passed, including the production-shader WARP cases. Added
coverage includes exhaustive mip interval coverage, mixed stereo waves,
minimal pyramid sizes, combined reset/disable behavior and concurrent
writer admission. The production routing helper also verifies native
replay precedes a fallback draw, suppression retires once, method changes
retain fallback ordering, and exceptions do not proceed into a native
draw without its required depth.

FXC production (`/WX /Ges /O3`) and developer (`/WX /Zi /Gfa /Gpp`)
compilations passed. DXBC inspection confirms an early return before any
second-eye projection. Shader evidence is preserved in the worktree's
`build/hiz-review-validation/`; combined build and test logs are
`build/hiz/review-build.log` and `build/hiz/review-tests.log` under the main
repository. Scoped formatting and whitespace checks passed.

The final Hybrid and temporal host sources also passed MSVC syntax checks
with the exact Release includes/defines, `/Y- /Zs`, and
`/UDEVBENCH_BRIDGE_ENABLED`. A forced header verifies the macro remains
undefined. Both returned zero without diagnostics; this is syntax
coverage, not a second DLL link. Exact commands, source hashes and logs
are in `build/hiz/review-no-devbench/receipt.json` under the main repository.

The universal Release DLL linked successfully with SE, AE, VR and
DevBench enabled. Its intermediate review manifest and DLL are preserved
under `build/hiz/evidence/review-initial-validation/`: Build ID
`ab87bc6bacc331b366a7974ad37d3d44608c43be585b9558971bb626df637cad`,
source `7d04ad7268fd59e21f283a7b363d3c2b05f8ba7d` with dirty digest
`d8efa96e3d53557e93bf7b44479bab88647451c4bbfba70dbd6a14fe1f5dad2c`.
This artifact predates the final inactive-status cleanup; the final
amended-commit build is separately identified by its adjacent manifest
and the local `build/hiz/evidence/review-final-validation/receipt.json`.

These checks do not establish native in-game hook execution, fresh
stereo camera data at readback, live graphics-state restoration, moving
occluder fidelity or neutral-or-better headset frame times. Those remain
the runtime acceptance work described above; Advanced remains the default.

## Rebase, 2026-09-29

Rebased the Hi-Z change from `be8891ad0486909effb78cc4c47fbe8fcfa3b35f`
onto `main-VR` head `dab1874a76fd39175dcefdc52110ba69d7284e12`.
The DevBench conflict resolution preserves both Hybrid controls and the
new FOV, parallax and Adaptive Balance controls. The evidence above
describes the pre-rebase source; no compilation or tests were run for
this rebase, as requested.

## Current-main integration, 2026-10-03

The original candidate branch remains unchanged. Its single feature delta
was integrated on `codex/astra-hiz-depth` from current `main-VR`
`c3f028b5207a2d9a32d539127aad614d2eef640a` as commit
`29ce68539c1fa489b241135d1a7629ccddf6a4e3`. The DevBench conflict resolution
retains newer Sky Sync controls and the Hybrid controls together.

Subsequent working changes implement completed-publication generation and
source-SRV admission across preparation, dispatch and delayed readback.
Incomplete, invalidated or replaced publications reject admission.
Observation phases identify the native-downscale and readback boundaries;
they do not prove when depth contents were written or that camera/depth
contents are current. Existing camera, bounds, one-frame age and epoch
checks remain separate. Readable invalid history remains fail-visible.

The pipeline retains its D3D device/context COM owners even after creation
failure. Actual owner replacement or explicit shader-cache clearing allows
recreation and resets the failure latch. A same-owner resource publication
change does not retry a failed shader pipeline. Dispatch rejects changed
owners, and recreation preserves pending history for fail-visible rejection
at readback rather than discarding the native batch's recovery obligation.

`Common/DepthOrder.hlsli` centralizes near/far, nearest/farthest reduction
and biased occlusion comparisons. Standard Z remains the only runtime
convention. Standalone WARP tests additionally exercise reversed arithmetic,
source depths and projection matrices; this does not activate or qualify a
Reverse-Z renderer. The inaccurate Reverse-Z comment in `ClearHMDMaskCS`
was corrected without changing its current mask behavior.

The user requires DevBench diagnostics and performance measurements adequate
for in-game evaluation. Additional snapshot/admission/pipeline diagnostics,
reason counts and attributed measurement coverage are implemented.
All new diagnostic/performance machinery must compile out when
`DEVBENCH_BRIDGE_ENABLED` is absent; runtime disablement alone does not meet
this requirement. The intermediate evidence below predates those final
extensions. The final diagnostic validation below supersedes it for those changes.

The source payload is a coherent bounded copy. Cumulative counters and
stage bins are individually atomic live observations, not one indivisible
aggregate snapshot. Capture the source payload before disabling telemetry;
then wait for admitted writers to finish and sample final totals for a
stable measurement window. Disabled telemetry hides the source payload.
A pending readback can belong to a submission before reset, so reset-window
accounting must not assume exact submitted/readback cohort conservation.

### Intermediate validation and artifact

Fresh universal `ALL` configuration succeeded with SE/AE/VR enabled,
DevBench ON and Tracy OFF. The generated project used CMake 4.4.1, Visual
Studio 18 2026, MSVC 19.51.36256.0 and Windows SDK 10.0.26100.0.

All 10 focused CTests passed in 3.61 seconds: depth-culling cache-refresh,
enable, telemetry, settings UI, settings and temporal policies; menu
depth-culling settings; and Hybrid policy, history and production-shader
WARP tests. WARP ran both Standard and reversed test directions. An initial
focused link lacked the `ShaderInclude` implementation because generation
predated the test target's linkage change; explicit reconfigure and rebuild
resolved it. The passing record is the final focused build/CTest pair.

The maintained shader verifier found all 12 Standard-Z Hybrid comparisons
identical to `29ce68539`: BuildDepth, ReduceDepth and TestBounds under
CSHADER, CSHADER+VR, CSHADER+HDR_OUTPUT and CSHADER+VR+HDR_OUTPUT. All four
corresponding mask-shader comparisons were also identical. This is exact
bytecode evidence for the Standard behavior, not a runtime performance
result or a separate complete strict-FXC sweep.

The initial universal Release DLL linked successfully before the new
diagnostic extensions. Its manifest at
`D:/Coding/GitHub/skyrim-community-shaders/build/ahiz/Release/CSX.BuildManifest.json`
was inspected directly and records:

| Field         | Intermediate artifact identity                                     |
| ------------- | ------------------------------------------------------------------ |
| Source commit | `29ce68539c1fa489b241135d1a7629ccddf6a4e3`                         |
| Dirty source  | `true`                                                             |
| Dirty digest  | `57615e2f31259e929117306f9e929afdec14d5f285e4e876ab99a32f23eda9a1` |
| Build ID      | `58e67fc0be147f0983564e2458fd3591a0e212a2cc0b78ba5b19b9f92081968b` |
| DLL SHA-256   | `790039f9d87e75715c505b90d61606395d76770a735df3f24246c8ef8b02466c` |
| DLL bytes     | 29587456                                                           |

Build ID and artifact hash are different fields. This intermediate artifact
does not certify the ongoing diagnostic additions and was not deployed.
The final diagnostic artifact is recorded below; this intermediate copy is preserved under `build/astra-validation/pre-diagnostics-artifact/`.

### Commands and local evidence

These commands ran from
`D:/Coding/GitHub/skyrim-community-shaders/.tmp/worktrees/astra-hiz`.
The HDE64 source override reuses an existing local dependency tree; it is
not a portable prerequisite. Separate `-D` from `KEY=value` arguments to
retain drive-colon paths correctly through the PowerShell wrapper.

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -B D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -D BUILD_CONTROLLER_TESTS=ON -D BUILD_SHADER_TESTS=OFF -D AUTO_PLUGIN_DEPLOYMENT=OFF -D ZIP_TO_DIST=OFF -D AIO_ZIP_TO_DIST=OFF -D SKIP_RUNTIME_DOWNLOADS=ON -D DEVBENCH_BRIDGE=ON -D TRACY_SUPPORT=OFF -D FETCHCONTENT_SOURCE_DIR_HDE64=D:/Coding/GitHub/skyrim-community-shaders/build/ALL/_deps/hde64-src
pwsh ./tools/cmake.ps1 -S . -B D:/Coding/GitHub/skyrim-community-shaders/build/ahiz
pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_depth_culling_telemetry_policy_test vr_depth_culling_settings_ui_test vr_depth_culling_settings_test menu_depth_culling_settings_policy_test vr_depth_culling_temporal_policy_test vr_hybrid_culling_policy_test vr_hybrid_culling_history_test vr_hybrid_culling_shader_test vr_depth_culling_cache_refresh_policy_test vr_depth_culling_enable_policy_test --parallel 4
& 'D:\Coding\GitHub\_tools\CMake\4.4.1\bin\ctest.exe' --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '^(VRDepthCulling(TelemetryPolicy|SettingsUI|Settings|TemporalPolicy|CacheRefreshPolicy|EnablePolicy)|MenuDepthCullingSettingsPolicy|VRHybridCulling(Policy|History|Shader))$' --output-on-failure --no-tests=error --output-junit D:/Coding/GitHub/skyrim-community-shaders/.tmp/worktrees/astra-hiz/build/astra-validation/focused-results.xml
pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target CommunityShaders --parallel 4
pwsh tools/verify-shader-refactor.ps1 'features/Upscaling/Shaders/Upscaling/ClearHMDMaskCS.hlsl' -BaseRef 29ce68539 -Fxc 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe' -Profile cs_5_0
```

Logs are in worktree `build/astra-validation/`: `configure.log`,
`reconfigure.log`, `focused-build.log` (initial linkage failure),
`focused-build-final.log`, `focused-ctest.log`, `focused-results.xml` and
`dll-build.log`. Hybrid shader-equivalence logs and their summary are in
`build/validation/depth-order-20261003T183356516Z/`.

No deployment, native lifecycle/fallback execution, real-HMD fidelity or
matched performance measurements have run. No render-scale owner/controller
writes changed, and these checks do not claim render-scale qualification.
Advanced remains default and Hybrid remains experimental. Native runtime
qualification remains open; PBR grass follows as a separate feature PR.

## Final diagnostic validation, 2026-10-03

All 11 focused tests passed in 3.75 seconds after adding the diagnostics:
menu serialization, concurrent telemetry admission/reset and coherent
snapshots, native-routing and history policies, and Standard/reversed WARP.
The final universal DLL linked with SE/AE/VR, DevBench ON and Tracy OFF.
Its preserved DLL and manifest in
`build/astra-validation/diagnostics-artifact/` passed provenance, hash and
size verification:

| Field         | Final diagnostic artifact                                          |
| ------------- | ------------------------------------------------------------------ |
| Source commit | `29ce68539c1fa489b241135d1a7629ccddf6a4e3`                         |
| Dirty source  | `true`                                                             |
| Dirty digest  | `78ce316f2ce046993c4ce6eb77b5cdb7ae4c0059c448c0ddf357195d18f01fb9` |
| Build ID      | `8246ebf5ca62ab8ac0aefb6d620059f22b03f4dd2a3bb171e0ec2b578aea0169` |
| DLL SHA-256   | `20c7063a0d7ca3c4418af0110d60cba6b9b9619f0991a3444a0be98f0570a1f4` |
| DLL bytes     | 29623296                                                           |

Hybrid, Temporal and Menu bridge each passed MSVC `/Zs` and `/P` with
DevBench disabled. The audit reused actual generated Release flags,
disabled PCH consumption with `/Y-`, retained the actual forced PCH
header, and asserted afterward that `DEVBENCH_BRIDGE_ENABLED`,
`TRACY_SUPPORT` and `TRACY_ENABLE` remained undefined. All 27 diagnostic
namespace/counter/GPU-label markers were absent in each preprocessed file.
Input source hashes remained unchanged. This is compiler/preprocessor
isolation evidence, not a separately linked production DLL or runtime test.

The schema check retained all existing `main-VR` actions and properties,
including Sky Sync; only Hybrid method and targeted snapshot actions were
added. Native and Hybrid GPU scopes share telemetry admission. During
pipeline setup, all three shader devices must match the retained device;
mismatches latch native fallback until owner replacement or cache clear.

The final commands reuse the configure and DLL commands above and add
`menu_depth_culling_diagnostics_test` to the focused build targets:

```powershell
& 'D:\Coding\GitHub\_tools\CMake\4.4.1\bin\ctest.exe' --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '^(VRDepthCulling(TelemetryPolicy|SettingsUI|Settings|TemporalPolicy|CacheRefreshPolicy|EnablePolicy)|MenuDepthCulling(SettingsPolicy|Diagnostics)|VRHybridCulling(Policy|History|Shader))$' --output-on-failure --no-tests=error --output-junit D:/Coding/GitHub/skyrim-community-shaders/.tmp/worktrees/astra-hiz/build/astra-validation/diagnostics-focused-results.xml
python tools/build_provenance.py verify --manifest build/astra-validation/diagnostics-artifact/CSX.BuildManifest.json --artifact build/astra-validation/diagnostics-artifact/CommunityShaders.dll
```

Evidence is in worktree `build/astra-validation/`:
`diagnostics-configure.log`, `diagnostics-focused-build.log`,
`diagnostics-focused-ctest.log`, `diagnostics-focused-results.xml`,
`diagnostics-dll-build.log` and `devbench-schema-check.txt`.
Actual production commands, forced-header assertions, preprocessed files,
source hashes and results are in
`production-boundary/20261003T191209638Z/`; its parent contains `validate.ps1`.

The requested complete developer AIO uses DevBench ON and verified upscaler
runtime payloads. It is not a public production release or a completed
in-game qualification. Its separate archive identity is recorded below.

## DevBench AIO archive, 2026-10-03

The complete developer AIO built successfully with DevBench ON, Tracy OFF
and SE/AE/VR enabled. All 369 extracted files (374,705,554 bytes) match the
staging tree by exact relative path, size and SHA-256. The 7-Zip integrity
test passed. The DLL, PDB, manifest, three Hybrid shaders and DepthOrder
helper match their producer files. Three pinned FidelityFX DLLs and their
source license, plus six Streamline DLLs and five notices, were verified.
Packaging did not deploy or launch the game. Later runtime evidence is
recorded below.

| Field             | AIO identity                                                                                |
| ----------------- | ------------------------------------------------------------------------------------------- |
| Delivered archive | `D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-DevBench-66b6efdee77b.7z` |
| Archive bytes     | 90819500                                                                                    |
| Archive SHA-256   | `27f144c67321f87d24f8d5d28f01044fc55a53593e1844fd11dafee08c9f4ac1`                          |
| Source commit     | `29ce68539c1fa489b241135d1a7629ccddf6a4e3` plus reviewed working changes                    |
| Dirty digest      | `635f3da88d720a30e98b3c1ce8569da3b193afdece582e226ea6d4596a42569f`                          |
| Build ID          | `66b6efdee77bd3e03a565b8698cf395e332c44c4e62d0af479ad7a47ec152088`                          |
| DLL SHA-256       | `fc4df6f883c5e9023ec27f90a1032751bd0e41020e9eaabf0aed573f81b8bfda`                          |
| DLL bytes         | 29623296                                                                                    |
| PDB SHA-256       | `cfeb501ed11b83367930c71f380e4873d8372439ff1b7f950359d7223794075a`                          |

The original archive is worktree `dist/CSX_AIO-2026-10-03T19-17Z.7z`.
Its verified copy and adjacent `.receipt.json` are in the primary checkout's
`dist` above. Full inventories, extraction and logs remain under
`D:/Coding/GitHub/skyrim-community-shaders/build/ahiz-aio-verification/20261003T192534997Z-f8dc0c/`.
The first validator expectation incorrectly required three total
FidelityFX files; correcting it to three DLLs plus the existing license
resolved that validation-only failure without changing the archive.
All 22 implementation/shader inventory hashes remained unchanged from
the final diagnostic DLL validation through AIO packaging. The new Build
ID preserves its own dirty-source identity; it does not replace the
earlier measured compile identity.

Pinned runtimes were copied from the existing `build/ALL` cache without
modifying it. Normal configuration reverified all bytes and extracted the
SDK into this task's build tree. Only owned staging paths were reset.
Automatic deployment and automatic archives stayed disabled:

```powershell
pwsh ./tools/cmake.ps1 -S . -B D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -D SKIP_RUNTIME_DOWNLOADS=OFF -D DEVBENCH_BRIDGE=ON -D ZIP_TO_DIST=OFF -D AIO_ZIP_TO_DIST=OFF -D AUTO_PLUGIN_DEPLOYMENT=OFF
pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target Package-AIO-Manual --parallel 4
```

Build logs are `build/astra-validation/aio-configure.log` and
`aio-build.log`; the local validator is
`build/astra-validation/aio/validate.ps1`. This is a developer package,
with no release/RC allocation or prebuilt shader-cache generation.

## Noon runtime evaluation and diagnostic follow-up, 2026-10-03

Two approximately 20-second fpsVR repeats completed for each of Off,
Legacy, Advanced and Hybrid in the same Whiterun exterior fixture, with
noon reset before each condition and phase. The measured AIO is Build ID
`66b6efdee77bd3e03a565b8698cf395e332c44c4e62d0af479ad7a47ec152088`, compiled
from `29ce68539c1fa489b241135d1a7629ccddf6a4e3` plus the recorded dirty
source, not the later repository head.

Hybrid's mean CPU/GPU times were 18.817793/11.283064 ms and
16.524725/10.324555 ms. Advanced measured 12.088390/8.722472 ms and
11.310150/8.431094 ms; Legacy measured 11.615019/8.608075 ms and
10.922291/8.244168 ms; Off measured 19.704537/11.637931 ms and
19.360093/11.681488 ms. The Index session budget was 8.333333 ms at 120 Hz.
CPU and GPU durations overlap. These results fail Hybrid performance
neutrality in this fixture, despite 6518/6518 and 3205/3205 accepted
Hybrid batches and zero recorded fallback, invalidated/unreadable batches,
warm pipeline builds or pyramid allocations.

All four static stereo sequences completed. Independent visual review
covered 16 previews from 64 validated originals and found no obvious large
geometry or stereo defects in those samples. JPEG/display scaling, animated
contents and physical pose variation limit that conclusion. Motion,
disocclusion, near-plane and lifecycle behavior are not qualified. Exact
means/quantiles, identity, sample definitions, source links and the
distinct evidence limits of upstream Reverse-Z PR 818 are in the
[runtime report](vr-hybrid-culling-runtime-2026-10-03.md).

The new additive `depthCullingTemporal.nativeVisibility` fields count
native CPU-result batches before and after Advanced recovery. They include
native fallback and exclude all Hybrid-owned readbacks, retain explicit
empty/unreadable counts, and use one existing telemetry writer across both
observations. Counts represent batch observations, not unique objects.
Disabled telemetry does not inspect the array. No extra GPU readback,
culling-policy or shader change is introduced. The complete diagnostic
path is guarded by `DEVBENCH_BRIDGE_ENABLED`.

These counters are absent from the original measured DLL. Both
`VRDepthCullingTelemetryPolicy` and `MenuDepthCullingDiagnostics` tests
passed after the change. DevBench ON syntax checks passed for Temporal
and Menu; OFF syntax/preprocessing passed for Hybrid, Temporal and Menu
using actual forced headers, with all 29 diagnostic markers absent.
Scoped formatting and diff checks passed. Evidence is under worktree
`build/astra-validation/native-visibility-tests-final-20261003T210450171Z/`
and `build/astra-validation/production-boundary/` (initial OFF
`20261003T205835011Z`, final Menu OFF `20261003T210440104Z`, ON Temporal/Menu
`native-visibility-on-20261003T210225404Z`, final Menu ON
`native-visibility-on-20261003T210453804Z`). The new universal DevBench ON,
Tracy OFF DLL/AIO has now built and passed archive verification:

| Field                   | Native-count AIO identity                                                       |
| ----------------------- | ------------------------------------------------------------------------------- |
| Compiled source         | `c684ff32c9f75c97f6743fe2829ca7eb040525f2`, dirty                               |
| Dirty digest            | `92ea1d0bb80e669a84b01843dd8cb1ed4403257ddd298e60051c43cb01839c86`              |
| Build ID                | `ee0c10f34abe0a5a7197ce4f77436273355c80b1c72747deb7a9331d9a44d5c3`              |
| Archive bytes / SHA-256 | 90,846,788 / `c4b48a061544cb84a45c58189d51803368a00dce5078c8a18746e954618b9449` |
| DLL bytes / SHA-256     | 29,625,856 / `7ca6b10542a836e33b59033220e3c477bea97aaef02a272f34f4a391d4d7f1a3` |

All 369 extracted files (374,724,498 bytes) matched staging. Archive
integrity, DLL/PDB/manifest, shader and pinned-runtime verification passed.
Source remained stable through the build, and both original archive copies
remain unchanged. The [new archive](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-native-counts-DevBench-ee0c10f34abe.7z)
has its [own verification receipt](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-native-counts-DevBench-ee0c10f34abe.receipt.json).
The build/delivery record is worktree
`build/astra-validation/native-count-rebuild-20261003T213736453Z/`.
The user installed and restarted this AIO. The separate
[native-count report](vr-hybrid-culling-native-counts-2026-10-03.md)
records verified runtime/physical identity, repeated Off/Legacy/Advanced/
Hybrid count windows and telemetry-disabled timings. Native rejection was
about 61-62% versus Hybrid's 19%; no Advanced recovery promotions or Hybrid
history rejection explained that gap. These new timings are descriptive:
concurrent compilation and differing simulation-clock progression prevent
a controlled performance verdict. A quiet repeat is pending. Hybrid remains
experimental following the original measured regression. Counts are repeated
batch observations, not matched persistent object identities.

The original measured DLL also completed four final motion ROI bursts of
160 consecutive frames each, covering 8.7075% of each eye. Native PNGs open
without resizing. The planned 64-step route has nine observed checkpoints;
captures cover 5/9 in Advanced, 6/9 in Legacy and 9/9 in Hybrid/Off. Only
common poses 0 through 32 support a fair four-mode comparison. Hybrid route
deltas are +84 submitted, +20 accepted and +64 `view_changed` invalidations,
with zero native fallback; observed cache `cameraAdjust` changed. All 640
original PNGs passed artifact verification. The completed sampled visual
review found no obvious holes or eye-specific geometry disappearance in
the common-pose samples and adjacent-frame candidates. This does not
qualify temporal behavior, physical head motion or the whole image; clean
samples during fail-visible rejection do not prove sustained culling
efficacy. Exact selection, review-player and coverage links are in the
[runtime report](vr-hybrid-culling-runtime-2026-10-03.md#bounded-motion-review).

## DevBench in-game evaluation procedure

Executed results are recorded separately in the
[noon runtime report](vr-hybrid-culling-runtime-2026-10-03.md). The procedure
below defines subsequent qualification windows; it does not imply that
every listed scenario has run. Follow the installed controls for session
ownership, capture and deployment. Verify the exact enabled AIO DLL against
its manifest/receipt and the running producer first. Replace every
`<verified-build-id>` below with that final 64-character Build ID. The text
placeholder is deliberately not a valid executable request value; never
substitute the intermediate artifact merely because its ID is in this doc.
Include `expectedBuildId` in every menu and profiler request and preserve
each response's producer identity. A mismatch invalidates the window.

All added culling diagnostic structures, clocks, counters, serialization
and diagnostic profiling scopes belong behind `DEVBENCH_BRIDGE_ENABLED`
and must be absent from production compiler output. An OFF build requires
separate verification; disabling telemetry in an ON build is not that proof.
In an ON build, depth-culling telemetry and the global profiler have
separate controls. Keep telemetry enabled during attributed culling GPU
captures, verify profiler availability, and inspect actual active sample
flags. Missing samples are not zero-cost measurements.

### Prepare one comparable window

Record the original method, exterior/interior enable/minimum-extent
settings, telemetry state and profiler state for restoration. Use the same
binary, shader package/cache warmth, save, weather, grass population,
material pack, render scale, upscaler/foveation mode and camera route for
each comparison. GO stays absent/off for D1. Predeclare the sample window,
repeat count and stopping conditions; a 300-frame profiler capture is one
bounded window, not necessarily an entire motion/lifecycle route.
Full qualification includes native depth culling off, Legacy, Advanced and
Hybrid. The user's focused optimization comparisons use Advanced and
Hybrid only. For off, disable the relevant depth-culling
enable controls and record the configured method and effective state.

Unless an explicit test protocol requires another time, reset the game to
noon before every condition and again before each separate visual, profiler
or fpsVR phase. Use the guarded DevBench command path, verify the scene's
observed `gameHour` is in `[12, 12.05]`, then settle for at least five seconds
before capture. Retain the command, scene observation and settling evidence.
An unverified reset invalidates that window. Preserve earlier night or
mixed-time captures as diagnostic evidence and exclude them from the final
matched comparison.

Use `communityshaders.menu` to record settings and select a method:

```json
{ "action": "status", "expectedBuildId": "<verified-build-id>" }
```

```json
{
    "action": "set_depth_culling_method",
    "method": "balanced",
    "expectedBuildId": "<verified-build-id>"
}
```

Use `balanced` for Advanced, `legacy` for Legacy and `hybrid` for Hybrid.
Retain identical enable/extent settings unless the row explicitly tests
native-culling-disabled behavior. Setters stage settings and return
`persisted=false`; the measurement need not save them. Switching invalidates
history. Warm the selected path until shader/pipeline preparation and
switch recovery finish; record that warmup separately from steady state.

Then enable shared Advanced/Hybrid telemetry and request a joint reset:

```json
{
    "action": "set_depth_culling_telemetry_enabled",
    "enabled": true,
    "expectedBuildId": "<verified-build-id>"
}
```

```json
{ "action": "reset_depth_culling_telemetry", "expectedBuildId": "<verified-build-id>" }
```

Require `reset=true`. `errorCode=depth_culling_telemetry_busy` with
`retrySafe=true` means admitted writers prevented the reset; both methods'
counters remain untouched. Retry this same reset at the next bounded
sampling opportunity without assuming partial success. If the reset cannot
complete within the predeclared deadline, preserve the failure and stop the
window. A reset changes measurement counters, not culling policy/history or
profiler history.

Take the narrow snapshot to establish the window:

```json
{ "action": "depth_culling_snapshot", "expectedBuildId": "<verified-build-id>" }
```

This returns `frame` and `depthCullingTemporal` without enabling capture or
building unrelated menu status. Preserve `installed`, `hybridInstalled`,
`cullingEnabled`, `policy`, `cullingEpoch` and `measurementWindow`.
`measurementWindow.current` must stay true, with the same nonzero window
ID and `startEpoch` matching the tested culling epoch. After a method or
enable change, warm again and reset explicitly. Do not pool mixed epochs
into one A/B result. For a Hybrid row, verify `hybrid.effectiveBackend`
and cumulative reasons; requested `hybrid` alone does not prove execution.

### Capture bounded profiler samples

Use the maintained `communityshaders.profiler_api` for paired CPU/GPU
captures. It requires `contractMajor`, `clientId`, `commandId` and `action`.
Use a unique `commandId` for each fresh observation; replaying a completed
command can return its prior response. Retain the same ID only when retrying
the same request after an uncertain transport outcome.

The profiler is one global controller with one active bounded session.
Record its original state, check the registry/snapshot and respect another
owner's capture before enabling or clearing history. These examples own a
new capture window and use unique IDs within that window:

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-registry",
    "action": "registry",
    "expectedBuildId": "<verified-build-id>"
}
```

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-snapshot-before",
    "action": "snapshot",
    "expectedBuildId": "<verified-build-id>"
}
```

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-enable",
    "action": "set_enabled",
    "enabled": true,
    "expectedBuildId": "<verified-build-id>"
}
```

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-start",
    "action": "start_capture",
    "frameCount": 300,
    "clearHistory": true,
    "expectedBuildId": "<verified-build-id>"
}
```

The accepted capture ID is returned under `result.captureId`. Replace the
illustrative numeric `1` below with that actual ID. Poll at a bounded cadence
and deadline, changing the command ID on each fresh poll:

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-progress-1",
    "action": "capture_status",
    "captureId": 1,
    "expectedBuildId": "<verified-build-id>"
}
```

Only `state=completed` with the expected submitted/resolved frame counts
establishes capture completion. A timeout, cancellation, busy response or
missing resolved GPU samples is an evidence limitation. Never turn it into
a zero or a pass. Preserve the final progress receipt, then read the saved
timer catalog and histories for that ID:

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-timers",
    "action": "timers",
    "captureId": 1,
    "expectedBuildId": "<verified-build-id>"
}
```

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-history-gpu",
    "action": "history",
    "captureId": 1,
    "timerIndex": 0,
    "domain": "gpu",
    "offset": 0,
    "limit": 300,
    "expectedBuildId": "<verified-build-id>"
}
```

Replace timer index `0` with each catalog index being evaluated; repeat
with domain `cpu` and distinct command IDs. Retain names, sample counts,
`hasGpu`/`hasCpu`, `activeGpu`/`activeCpu`, slot refusals and raw histories.
Use bounded `depth_culling_snapshot` observations at the beginning, during
the route and before teardown, rather than per-frame status polling.
Examples of relevant GPU labels are `VRHybridCulling::Visibility`,
`BuildHierarchy`, `BuildBase`, `ReduceMips`, `TestBounds`, `CopyResults`, and
`VRDepthCulling::NativeDownscale`, `ReplayDownscale`, `NativeProducer`.
Retain actual catalog names/availability from the tested build.

The legacy `communityshaders.profiler` supports `enable`, `disable` and
`status`, also with `expectedBuildId`. Its `status` requests capture and
returns rolling results; it is not a purely passive snapshot and has no
`start_capture` action. Prefer the versioned bounded API for this protocol.
Independent CPU views have their own publication/count/catalog identity
and do not accept `captureId`; do not mix those indices with paired results.

### Close the window without losing source evidence

While telemetry is still enabled, take and save a final
`depth_culling_snapshot`. This is the last source/camera/resource observation
for the window. Then disable telemetry:

```json
{
    "action": "set_depth_culling_telemetry_enabled",
    "enabled": false,
    "expectedBuildId": "<verified-build-id>"
}
```

Poll the narrow snapshot with a bounded deadline until
`telemetryEnabled=false` and `telemetryFrozen=true`. Already-admitted writers
may finish after disable. Only the frozen response supplies stable final
counter/histogram totals; preserve its measurement-window identity and frame.
Source payloads are hidden when telemetry is disabled, which is why the
source observation must be saved first. Counters/stage bins are individually
atomic observations while live, not a single atomic aggregate snapshot.

Do not reset again before saving the evidence. A submission just before
reset can read back after reset, and a final submission can read back after
disable. Exact submitted/readback cohort conservation across these
boundaries is not guaranteed. Report observed numerator/denominator and
boundary uncertainty rather than forcing counts to balance.

Restore the profiler's original enabled state after owned captures finish;
for an originally disabled profiler, use:

```json
{
    "contractMajor": 1,
    "clientId": "astra-hiz-eval",
    "commandId": "w1-disable",
    "action": "set_enabled",
    "enabled": false,
    "expectedBuildId": "<verified-build-id>"
}
```

Restore the recorded method, settings and telemetry preference through the
same guarded setters. Stop on unexpected producer/session changes rather
than continue a mixed-identity campaign.

### Interpret the evidence

| Evidence                                             | Meaning and limitation                                                                                                                                                                                                                                                                    |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CPU `cpuTimings` stages                              | Inclusive wall time in nanoseconds, including attempted/failed work; prepare/dispatch measure CPU work and submission, not completed GPU execution                                                                                                                                        |
| `nativeReadback` versus Hybrid `cpuTimings.readback` | Native intercepted call includes its staging Map/copy/reset and any Map stall; Hybrid timing measures subsequent history validation only                                                                                                                                                  |
| Outer/replay/native/Hybrid stage totals              | Nested CPU work can overlap these scopes; do not sum all inclusive totals to invent culling cost or double-count preparation inside outer downscale                                                                                                                                       |
| New stage histograms                                 | 17 bins with inclusive upper bounds 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 microseconds, then 1, 2, 4, 8, 16, 64 milliseconds and an unbounded overflow bin (`null` upper bound); use returned bounds and counts                                                                           |
| Existing Advanced recovery histogram                 | Separate eight-bin distribution ending at 64 microseconds plus overflow; it is not the new 17-bin stage histogram                                                                                                                                                                         |
| Histogram percentiles                                | Bound the percentile to its occupied interval; overflow has no finite upper bound. Do not report exact p95/p99 values from coarse bins. A zero-sample mean is `null`                                                                                                                      |
| Profiler timers/history                              | Millisecond self time with profiled descendants removed. Rolling statistics cover at most 300 retained samples; bounded capture IDs give attributed histories. Preserve sample counts and compute campaign statistics from actual samples                                                 |
| Profiler `topLevelMs` / resolved GPU total           | Inclusive depth-zero contribution; do not add it again to descendant self-time totals. Profiled totals are coverage of instrumented work, not automatically whole-frame time                                                                                                              |
| Accepted visibility                                  | `acceptedOccludedObjects` and `acceptedVisibleObjects` describe accepted Hybrid readback results. `submittedObjects` counts queued GPU candidates; `testedObjects` counts examined CPU records, including invalidated ones, not extra GPU tests. These are not unique scene-object counts |
| Fallback/history/promotion counts                    | Preserve zero and nonzero reason counts, unreadable batches and promoted objects. Invalidated readable batches keep objects visible; native fallback does not populate Hybrid accepted-visibility totals                                                                                  |
| Source snapshot                                      | Coherent bounded payload with `available/current/valid/busy/validity/stage`, epoch, source formats/dimensions/generation, eye rectangles, camera observations and mask policy. `contentFreshnessProven=false` remains explicit even for a current observation                             |
| Missing or rejected source                           | Preserve `null` data, `snapshot_busy`, superseded/older-frame/inactive/rejected reasons and `droppedSourceSnapshots`; do not interpret a retained SRV or matrix as a depth-content-freshness proof                                                                                        |
| Resource counts and bytes                            | Pipeline attempts/builds/recreations, pyramid allocations/builds/dispatches and bounds dispatches expose actual work. Logical R32-float texel bytes/high-water are uncompressed accounting, not driver VRAM/residency                                                                     |

Run repeated matched off/Legacy/Advanced/Hybrid windows for both performance
and stereo visuals, preferably alternating order to expose drift. Reset
and verify noon before every condition and each separate capture phase.
Retain cold-start/switch/recovery costs separately from warmed steady state.
Compare stationary dense exterior/interior views, head rotation/translation,
thin occluders/door edges, moving occluders, near/far geometry and cell/load
transitions. Record both eyes over time on the real HMD; null-driver captures
cannot qualify stereo motion or asymmetric projections.

Measure whole-frame CPU/GPU behavior and memory with the established
external/runtime tools alongside the pass evidence. Use the actual refresh
budget `1000 / refreshHz` milliseconds and repeated-baseline variance;
11.11 ms applies only at 90 Hz. Report correctness, effective backend,
completion/health and performance independently. A correct experimental
path can still regress, and missing visible geometry never counts as a
gain. Preserve the full source/build/settings/capture identities and missing
evidence without claiming that this documented procedure has run.
