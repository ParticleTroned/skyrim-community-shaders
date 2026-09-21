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

## Review and validation

The adversarial scope/correctness/DRY/robustness review fixed same-frame
validity leakage and incomplete SRV restoration on skipped terrain paths.
The implementation reuses the shared depth accessor, existing engine
textures and Terrain Blending backups, and one copy helper for both
boundaries. Tests use the repository's production-source extraction
pattern with fake engine/D3D boundaries. The generated header has one
producer and the regression belongs to the aggregate `run_host_tests`
target.

The host-side `SceneDepth` regression extracts the production routing,
copy-publication and hook code. It covers prepass/final source selection,
missing blended formats, disabled and unloaded terrain, frame rollover,
disabled early consumers, repeated same-frame world passes, fallback
ordering, early-copy failure, SRV ownership and missing resources.

Offline tests do not prove D3D binding behavior or visual quality. Terrain
toggles, water edges, AO/GI modes, temporal modes and resource rebuilds
still require live SE and AE coverage. No performance gain is claimed.
