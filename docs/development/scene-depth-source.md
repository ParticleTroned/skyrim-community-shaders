# Completed scene depth

SSGI and other deferred depth consumers need the depth of the opaque
geometry that produced the current G-buffer. Terrain Blending's separate
blended prepass textures cannot reflect later geometry or decals. Reading
them during deferred shading can pair a foreground normal with background
depth. This correction adapts
[Open Shaders #611](https://github.com/alandtse/open-shaders/pull/611).

## Render contract

`Deferred::DeferredPasses()` copies physical main depth into the existing
`kPOST_ZPREPASS_COPY` texture before SSGI, subsurface scattering and
composition. `EndDeferred()` has already unbound its output targets.
Queued terrain and native blended decals have completed at this boundary.

`Util::GetCurrentSceneDepthSRV()` retains the existing terrain-blended
32-bit/16-bit prepass selection until the current frame publishes its
completed opaque copy. Both accessor formats then select that physical
copy. The copy remains a full `CopyResource`: water can sample beyond the
active scaled region. There are no new graphics resources, HLSL changes,
settings, runtime-specific branches or render-scale coordinate changes.

Point 3 of the depth-source investigation requires invalidation before
early consumers, including skipped-deferred paths. A frame stamp prevents
previous-frame publication from reaching consumers before EarlyPrepasses.
EarlyPrepasses resets validity before its enable guard; opaque-pass entry
also resets it before the deferred enable/world guards. This covers a
second disabled world pass in the same frame. Resource release/setup and
direct StartDeferred entry invalidate it as well.

The pre-water hook retains one fallback copy when deferred rendering did
not publish a completed copy. An active deferred pass still completes if
the master switch changes after it began. The fallback restores both main
and copy SRVs when they still point to Terrain Blending's blended texture,
including when the master switch skipped the terrain redraw. It preserves
views that do not match that redirection. Missing renderer, context, frame
state, textures or destination SRV reject publication; the ordinary
prepass selection remains available when its resources exist.

## Diagnostics

DevBench builds log `[Deferred::SceneDepth]` cumulative copy attempts,
distinct attempted frame numbers, successful early copies, successful
fallback copies by reason, and failed attempts. Reasons distinguish a
disabled master switch, no deferred pass, execution outside the world,
and an unavailable early copy. The first fallback reports immediately;
subsequent reports are limited to one per ten seconds. The named GPU pass
`Deferred::CopySceneDepth` uses the existing profiler instrumentation.
These counters establish copy frequency, not a performance improvement.

## Review and validation

The adversarial scope/correctness/DRY/robustness review fixed same-frame
validity leakage and incomplete SRV restoration on skipped terrain paths.
The implementation reuses the shared depth accessor, existing engine
textures and Terrain Blending backups, and one copy helper for both
boundaries. Tests use the repository's production-source extraction
pattern with fake engine/D3D boundaries, in plain and DevBench variants.
Their shared generated header has one producer and both variants belong
to the aggregate `controller_tests` target.

Passed on 2026-09-12:

-   `pwsh ./tools/cmake.ps1 -S . -B build/commonlib-671-validation-20260912 -DBUILD_CONTROLLER_TESTS=ON`
-   `pwsh ./tools/cmake.ps1 --build build/commonlib-671-validation-20260912 --config Release --target scene_depth_plain_test scene_depth_devbench_test CommunityShaders --parallel 4`
    compiled the universal SE/AE/VR DLL with DevBench enabled and both
    regression variants.
-   `ctest --test-dir build/commonlib-671-validation-20260912 -C Release -R '^SceneDepth_' --output-on-failure`
    passed 2/2 tests. Coverage includes prepass/final source selection,
    missing blended formats, disabled/unloaded terrain, frame rollover,
    disabled early consumers, repeated same-frame world passes, fallback
    ordering/counts, early-copy failure, SRV ownership and missing resources.

The build used the working tree, including unrelated uncommitted changes;
it is compile evidence, not an isolated release or deployment receipt.
Local logs are preserved under
`build/analysis/ssgi-depth-source-20260912/`.

The full-file `gersemi` hook also reformatted inherited CMake content
outside this change. Those edits were reverted against the byte-verified
pre-hook content. `gersemi --check --line-ranges 1644-1681,2689-2690 CMakeLists.txt`
passed for the added ranges, and the new extraction script was formatted
separately. The remaining staged hooks run with `SKIP=gersemi` to preserve
unrelated formatting; this is a scoped formatter check, not a clean
full-file formatting result.

In-game SE/AE/VR validation, visual comparisons and GPU measurements have
not run. Skyrim was not running and the configured DevBench endpoint
refused the bounded discovery connection. The discovery failure and
`sessionCleanup: not_opened` receipt are preserved. Offline tests do not
prove D3D binding behavior or visual quality; terrain toggles, water edges,
AO/GI modes, temporal/stereo modes and resource rebuilds still need live
coverage. No DLL was deployed and no performance gain is claimed.
