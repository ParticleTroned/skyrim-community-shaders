# CSX Feature API v1

`csx.features` is an additive feature catalog and guarded boot-configuration
service. Existing feature loading, menu settings, hotkeys, shader-cache
validation, and settings persistence remain the authoritative legacy paths.

The API reports installed and required versions, load/failure state, category,
visibility, VR/core/menu traits, shader-define identity, boot-disable state,
runtime dependency suppression, summaries, current settings JSON, and active
cross-feature constraints. Live effect toggles and performance measurement are
not generalized here; their owning Shader and Profiler services retain those
controls.

The sole v1 mutation is `set_disabled_at_boot`. It is a persistent,
restart-required operation which may cause shader-cache regeneration. It is
therefore rejected during save/load persistence guards and requires a fresh
state revision, `persist=true`, `allowDisruptive=true`, preflight, and an exact
execution token within 30 seconds. Save failure restores the prior in-memory
boot state.

Native live calls are main-thread-affine. DevBench exposes the same contract as
`communityshaders.feature_api`; registry metadata itself is off-thread safe.

DevBench minor 1 / schema revision 2 adds the read-only
`preset_compatibility` action. It reports the last `SettingsUser.json`
compatibility decision, target range, loaded CSX version, and rejection reason.
This is additive; the native feature-service ABI remains at v1.0 because preset
validation belongs to the settings loader rather than the feature catalog.
