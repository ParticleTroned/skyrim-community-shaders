# Upscaling + Terrain Blending + VR Depth Culling (Current Handoff)

This file is a current, practical handoff so a new chat can continue without re-discovery.

## 1) Scope

Main topic:
- VR depth buffer culling behavior across:
  - `None/TAA` (no vendor upscaling)
  - `DLSS/FSR` (vendor upscaling)
  - `Terrain Blending` (TB) ON/OFF

Current focus:
- Regression check: depth culling appears to work with vendor upscaling, but not with `TAA/DLAA` according to testers.

## 2) Repository Snapshot

- Branch: `cs-1.4.7-PL-dlssA`
- Current `HEAD`: `2086cb5e` (`fix(upscaling): add DLSS model presets and restore RCAS/DX12 compatibility`)
- Working tree is currently dirty (not committed):
  - `src/Features/TerrainBlending.cpp`
  - `src/Features/Upscaling.cpp`
  - `src/Features/VR.cpp`

## 3) Important Commit Timeline (this branch)

Starting from prior TB/depth work already present in base branch:
- `90278b49` depth-culling + upscaling groundwork
- `df76319f` TB depth SRV guard in VR upscaling
- `d6b1d7bf` scoped draw-call overrides for OBB/shadowmask
- `597cb147` depth-bias tuning
- `f952ae63` tighter gating for VR/upscaling/depth culling
- `f04c9b7e` depth-bias CB slot fix
- `820f190b` older `UpscalingTB.md` update

Then on `cs-1.4.7-PL-dlssA`:
- `eaf13137` `fix(VR): apply per eye upscaling (#1819)` (cherry-picked with incoming/theirs)
- `e213f6ea` `fix(vr upscaling): stabilize depth upscaling with per-eye clamps`
- `23835a03` `merge(upscaling): sync PR1819 files and keep VR depth-culling support`
- `2086cb5e` `fix(upscaling): add DLSS model presets and restore RCAS/DX12 compatibility`

## 4) What Was Requested in This Session

User requested:
- Keep depth culling activation independent from TB enable state.
- Re-check TAA/DLAA depth-culling behavior vs `v1.4.6_PL3.5-VR`.
- Prefer DLSS-native scaling behavior (safer for DLAA interpretation) instead of generic FSR ratio for all methods.
- Update this file with detailed current status for a fresh chat.

## 5) Reported Test Results From User/Testers

Reported outcomes:
- Depth culling works with upscaling (DLSS/FSR path).
- With `TAA/DLAA`, depth culling seems ineffective:
  - no measurable perf increase
  - setting occluding box extent to `0` did not produce expected behavior changes
- Testers explicitly disabled TB and still saw TAA issue.

Implication:
- TAA/DLAA issue is likely not TB-toggle gating alone.

## 6) Current Local (Uncommitted) Changes

### A) `src/Features/TerrainBlending.cpp`

Function: `ShouldUseBlendedDepthSRV()`

Change:
- Removed the `IsUpscalingActive()` early-return gate.
- Now in VR, if depth culling is enabled (`vr.gDepthBufferCulling == true`), function returns `false` regardless of upscaling mode.

Intent:
- Simplify TB depth SRV choice under active VR culling.
- This is SRV behavior only, not culling enable/disable logic.

### B) `src/Features/VR.cpp`

Changes:
- `EarlyPrepass()` now also re-syncs `gMinOccludeeBoxExtent` every frame.
- VR menu checkboxes for depth culling now apply immediately by calling `UpdateDepthBufferCulling(...)`.
- Added runtime readback lines in UI:
  - `Runtime Depth Culling: ON/OFF`
  - `Runtime Min Occludee: <value>`
- `UpdateDepthBufferCulling(...)` now:
  - always writes desired value
  - warns if write does not stick (`wanted X, got Y`)

Intent:
- Improve observability and detect runtime overwrite issues.

### C) `src/Features/Upscaling.cpp`

Changes:
- UI label renamed from `Upscale Preset` to `Upscaling`.
- Fixed `enableWaterTAA` logic in `ConfigureTAA()` to:
  - `true` for `NONE/TAA`
  - `false` for vendor upscalers
- In `ConfigureUpscaling()`:
  - Restored method-specific scale behavior for DLSS/DLAA path by querying:
    - `streamline.slDLSSGetOptimalSettings(...)`
  - Fallback to FSR quality-ratio path if DLSS query fails.
  - Keep FSR path on FSR quality-ratio path.
  - Clamp render dimensions and preserve exact `1.0` scale for native/DLAA-sized output.

Intent:
- Reduce risk of DLAA misclassification by avoiding FSR-derived scale for DLSS path.

## 7) Depth Culling vs TB Activation Logic (Current Understanding)

Depth culling activation itself:
- Controlled in VR feature (`src/Features/VR.cpp`) by user setting and interior/exterior state.
- Not disabled by TB in current VR logic.

TB interaction that still exists:
- TB can still influence which depth SRV is used in TB-specific and scoped override paths.
- That is separate from "is depth culling on/off".

## 8) Why TAA/DLAA May Still Differ From Old Tag

Even after removing obvious lockouts, behavior is not guaranteed identical to old tag because:
- Upscaling subsystem had major refactors from PR1819 and later merges.
- Runtime state sync and diagnostics in VR are now stronger than older tag.
- Depth consumers and scoped overrides introduced in TB-related evolution can still affect depth behavior in edge cases.

## 9) Practical Diagnostics Already Added

In VR menu -> General Settings:
- `Runtime Depth Culling: ON/OFF`
- `Runtime Min Occludee: <value>`

Use these while toggling settings to verify actual runtime values are changing.

## 10) Recommended Next Steps in New Chat

1. Build current dirty state and test with TB OFF:
   - `TAA`, `DLAA`, `None`, `DLSS`, `FSR`
2. For each mode, capture:
   - runtime readback values from VR menu
   - perf delta with depth culling ON/OFF
   - visual response to `Min Occludee Box Extent` extreme values
3. If runtime value shows ON but behavior unchanged:
   - instrument direct reads around occlusion dispatch path to confirm engine-side variable is respected at draw time.
4. Decide whether to commit current local trio (`VR.cpp`, `Upscaling.cpp`, `TerrainBlending.cpp`) or split into:
   - diagnostics-only commit
   - DLSS-scale logic commit
   - TB SRV policy commit

## 11) Quick Copy/Paste Context For New Chat

Use this block directly:

```
Branch: cs-1.4.7-PL-dlssA
HEAD: 2086cb5e

Goal: Fix/verify VR depth buffer culling behavior, especially TAA/DLAA with TB OFF.

Already integrated:
- PR1819 per-eye upscaling and follow-up merges.
- Scoped TB depth overrides and previous TB/depth work from earlier commits.

Current uncommitted files:
- src/Features/TerrainBlending.cpp
- src/Features/Upscaling.cpp
- src/Features/VR.cpp

Local changes summary:
- VR: immediate culling apply + runtime readback + min-occludee resync + write-sticky warning.
- Upscaling: water TAA logic fix + UI label rename + DLSS optimal-settings-based resolution scale (fallback to FSR ratio).
- TB: ShouldUseBlendedDepthSRV now keyed on VR depth culling, not IsUpscalingActive.

Observed issue to investigate:
- Testers report depth culling gives expected effect with vendor upscaling, but not with TAA/DLAA, even when TB is disabled.
```

