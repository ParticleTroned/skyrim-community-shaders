# Experimental VR Hybrid Hi-Z culling

Hybrid is an optional Skyrim VR 1.4.15 visibility producer for comparison
with Advanced. Advanced remains the default. Hybrid is not yet accepted
as performance neutral or visually equivalent: those are separate runtime
acceptance requirements, and CPU/shader tests do not establish either.

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
collection. There is no additional readback or synchronous GPU query.
The native staging map itself can still block if the GPU is late.

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

`BuildDepthCS` reduces all pixels in each 4-by-4 source region. Each eye
gets a separate texture-array layer; its base dimensions are padded to
powers of two. Zero-depth VR masks, invalid samples, incomplete edge
regions and padding become far depth. `ReduceDepthCS` then takes the
maximum of each covered 2-by-2 region. This hierarchy is independent of
the averaged GI depth pyramid. It stops when both dimensions are at most
two, because the visibility test never needs a coarser level; a 1-by-1
base remains valid.

`TestBoundsCS` projects all eight OBB corners using each eye's actual
matrix and camera adjustment. Camera adjustment is subtracted from the
OBB translation before corner multiplication, preserving small extents
at large world coordinates. Near/eye-plane crossings, far-plane
crossings, nonfinite data and non-affine bounds retain visibility.

Projected rectangles include a pixel guard margin. If that margin leaves
the eye viewport, visibility is retained. The shader selects a mip at
which the complete rectangle overlaps at most four cells and reads every
one of them. A box is hidden only when its nearest depth is strictly
behind the farthest covered depth, including a depth bias, in both eyes.
Visibility in either eye retains the object. Coarse cells and far-valued
padding can reduce rejection efficiency; they cannot justify discarding
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

`status.depthCullingTemporal.hybridInstalled` identifies
whether the optional replacement hooks are available; failure to install
them retains native testing and Advanced recovery. Check effective culling enablement and producer state,
since selecting Hybrid does not prove every frame used it. The profiler
labels `VRHybridCulling::BuildHierarchy` and
`VRHybridCulling::Visibility` identify its GPU work.

## Validation and acceptance

The portable policy tests cover safe dimensions, power-of-two padding,
constant validation, projection validity and dispatch limits. The WARP
test executes the production compute shaders and checks every pyramid
level against complete source coverage, odd eye sizes, masks, invalid
depth, stereo visibility, viewport margins, clipping, asymmetric
perspective and small bounds at large world coordinates. History and
settings tests cover batch correspondence and method migration.

Runtime acceptance requires controlled Advanced/Hybrid comparisons on
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
