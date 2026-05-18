# Codex Handover: VR Performance Analysis

Branch context: `cs-1.4.11-PL-VR`

This document captures the last VR-focused performance analysis for this branch. It is intended for a future Codex session so work can continue without rediscovering the same repo context.

This is a static code analysis, not a profiler capture. The expected savings below are estimates and must be validated with RenderDoc, Tracy, or the in-game performance overlay.

## Current Branch State

- The branch already contains VR-specific foveated infrastructure.
- The branch already contains foveated upscaling/periphery TAA paths.
- The branch already contains a single FOV SSGI toggle that reuses the shared upscaling FOV mask.
- The branch already contains targeted stereo blending/sync for SSGI and screen-space shadows.
- The branch does not contain the broad upstream #2002 global stereo reprojection path.
- The previous conclusion was to avoid #2002-style global stereo reprojection as the primary optimization path because it had a fixed cost around 0.6 ms and was fragile.
- The better strategy is feature-local foveation plus feature-local stereo sync where the data being synchronized is known and bounded.

## Most Important Correction From The Last Analysis

The highest priority is not only optional feature compute passes. In VR, the common shader paths are likely more important:

1. `package/Shaders/Lighting.hlsl`
2. `package/Shaders/Utility.hlsl`
3. SSR, screen-space shadows, water, Wetterness, SSGI residual work

Reason: `Lighting.hlsl` is the main material/lighting path and can receive many feature defines. Optimizing one optional pass may help only when that feature is active, but reducing auxiliary work inside the lighting path can improve many normal scene draws.

## Existing Foveated Infrastructure

### C++ shared helper

File: `src/Features/FoveatedCommon.h`

Key constants:

- `kCenterAreaMin = 0.25`
- `kCenterAreaMax = 1.0`
- `kCenterFeather = 0.05`
- `kCenterHorizontalScaleMin = 1.0`
- `kCenterHorizontalScaleMax = 2.0`
- `kMaskShapePower = 4.0`
- `kThreadGroupSize = 8`

Important functions:

- `ClampCenterArea`
- `ClampCenterHorizontalScale`
- `BuildCenteredDispatchBounds`
- `BuildCenteredInscribedMaskRect`

Use this for new C++ dispatch rects. Do not invent a second foveated coordinate system unless there is a very specific reason.

### HLSL shared helper

File: `package/Shaders/Common/FoveatedMask.hlsli`

Important functions:

- `FoveatedComputeCenterUV`
- `FoveatedComputeMaskRadii`
- `FoveatedComputeMaskDistance`
- `FoveatedComputeCenterBlendWeight`

Use this for shader-side center/feather/periphery decisions. Keep masks consistent across features.

### Existing upscaling foveated path

Files:

- `src/Features/Upscaling.h`
- `src/Features/Upscaling.cpp`
- `features/Upscaling/Shaders/Upscaling/FoveatedPeripheryCS.hlsl`
- `features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl`
- `features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl`

Current settings include:

- `foveatedVendorDispatch`
- `foveatedCenterArea`
- `foveatedCenterHorizontalScale`
- per-eye mask offsets
- `periphery_taa_enable`
- `periphery_taa_outer_scale`
- `periphery_taa_center_area`
- `periphery_taa_center_blend_feather`
- `foveatedPeripheryMaskVisualization`

Important implementation details:

- Upscaling can build foveated dispatch rects per eye.
- It has periphery TAA support.
- It can encode only foveated regions for vendor upscaling paths.
- It resets history when foveated center area, horizontal scale, offsets, method, runtime path, or periphery TAA state changes.

Do not reorder this path lightly. For new feature work, prefer consuming its mask settings rather than moving the upscaler earlier in the pipeline.

## Existing SSGI/AO FOV SSGI

Files:

- `src/Features/ScreenSpaceGI.h`
- `src/Features/ScreenSpaceGI.cpp`
- `features/Screen Space GI/Shaders/ScreenSpaceGI/common.hlsli`

Current VR defaults in `ScreenSpaceGI.h`:

- SSGI is disabled by default in VR.
- GI/IL is disabled by default in VR.
- VR default resource profile is AO-only.
- AO defaults are `NumSlices = 3`, `NumSteps = 6`.
- Resolution mode defaults to full resolution.
- `AOInteriorsOnly = true` in VR.
- `ILInteriorsOnly = true` in VR.
- `VRCullDistance = 1500`.
- `EnableFoveated = false`.

FOV SSGI behavior:

- `EnableFoveated` is a single VR foveation-tab toggle.
- FOV SSGI runs SSGI only inside the active upscaling FOV mask; outside the mask receives no SSGI.
- FOV SSGI is AO-only at runtime.
- IL/GI and denoisers are bypassed while FOV SSGI is active, without overwriting the normal SSGI preset/resource settings.
- SSGI has no separate FOV area or link toggle; it consumes the shared upscaling/periphery TAA FOV mask.

Known remaining opportunity:

- In FOV SSGI mode, some full-frame support work can still run before the center-only work. Investigate depth prefilter/history work and skip it when the output will be discarded.
- Existing gating already skips some later work, but not necessarily all earlier prefilter paths.

## Existing Stereo Blending / Stereo Sync

This branch uses targeted stereo sync, not broad global stereo reprojection.

Shared helper file:

- `package/Shaders/Common/VR.hlsli`

Important functions:

- `Stereo::ReprojectToOtherEye`
- `Stereo::FinalizeStereoBlend`

Feature-local stereo sync shaders:

- `features/Screen Space GI/Shaders/ScreenSpaceGI/stereoSync.cs.hlsl`
- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/StereoSyncCS.hlsl`

Current design intent:

- SSGI/AO uses stereo sync after blur/reprojection-sensitive stages.
- Screen-space shadows use stereo sync to reduce per-eye mismatch.
- This is safer than a global full-frame color/depth reprojection pass because each feature owns the data being blended and can apply feature-specific validity checks.

Recent negative debugging result (2026-05-04):

- The reported issue, one eye effectively HMD-locked while the other remains world-stable, also occurs with FOV/foveated mode off.
- Follow-up observation: the artifact is visible in each eye as a mixed result, with one rendered component HMD-locked and another component world-locked.
- The HMD-locked component is not a complete world image. It contains only a partial scene contribution, observed on water, trees, mountains/clouds, fire, and buildings, while the full world view remains separately world-locked.
- Reverting the feature-local stereo reprojection helper from the alternate unjittered projection/view-inverse path back to the full `CameraViewProjInverse` -> `CameraViewProj` path had no visible effect.
- Removing the compute-side `VRValues`/`StereoEnabled` dependency from the SSGI/SSS stereo sync path also had no visible effect.
- Testing the SSS stereo-sync validation patch, including extra cross-eye depth checks and eye-local blur clamping, also had no visible effect. The test changes were reverted; treat this as another negative result.
- Testing runtime SSS stereo-sync diagnostic toggles also had no useful effect. The toggles covered source-copy-only output, disabling cross-eye blend, disabling same-eye blur, and scaled-depth sampling. None changed the artifact.
- During that diagnostic-toggle build, the per-eye misaligned Community Shaders UI in VR reappeared. Treat the toggle build as invalid for further work and keep the branch reverted to the pre-toggle state.
- Testing the depth-layout compatibility gate patch for SSGI/SSS stereo sync (combined-stereo-only guard and frame-dimension dispatch change) did not fix the world-locked/HMD-locked split artifact.
- The same patch introduced a new VR UI regression: Community Shaders per-eye UI overlay no longer lines up correctly (stereo UI sync/overlay broken).
- Do not keep chasing the unjittered/full-matrix helper difference as the primary cause. Next suspects are producer-side per-eye data, eye selection, dispatch/write bounds, or depth/shadow/AO texture layout feeding stereo sync.

Targeted mitigation patch (2026-05-04, pending runtime validation):

- Re-enabled stereo sync back-check in both feature-local shaders by restoring a non-zero threshold (`8.0`) instead of `0.0`.
- SSGI change: `features/Screen Space GI/Shaders/ScreenSpaceGI/stereoSync.cs.hlsl` (`kBackCheckThreshold`).
- SSS change: `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/StereoSyncCS.hlsl` (added `kBackCheckThreshold`, passed into `FinalizeStereoBlend`).
- Rationale: `backCheckThreshold == 0.0` disables round-trip reprojection validation in `Stereo::FinalizeStereoBlend`, which can allow invalid cross-eye matches and produce overlay-like HMD-locked contributions.
- Validation result: this did not solve the world-locked/HMD-locked split artifact. Treat back-check re-enable as another negative result, not a primary fix.

Important guidance:

- Keep stereo blending feature-local unless profiling proves a global pass gives a net win.
- Do not reintroduce #2002 as a broad full-frame dependency without revalidating cost and correctness.
- If a future stereo blend is added, gate it tightly:
  - skip when the feature output is not visible,
  - skip when the relevant effect is disabled by interior/exterior gating,
  - skip if foveated hard-off means the target pixels will be discarded,
  - keep a user toggle for quality/perf debugging.

## Why Global #2002-Style Stereo Reprojection Was Deferred

The prior branch decision was to defer #2002 and follow up with more targeted work.

Reasons:

- It had an estimated fixed cost around 0.6 ms.
- It depended on fragile assumptions around depth layout, dynamic resolution, and reprojection validity.
- It had several later upstream fixes/followups, which suggests the original design was not stable enough for this branch.
- It can be invalid for effects that need feature-specific bilateral/depth/material rules.
- This branch already has safer SSGI and screen-space shadow stereo sync paths.

Best current direction:

- Use targeted stereo sync where it improves stereo stability.
- Use foveated reduction to avoid doing expensive work in the periphery.
- Avoid a global pass unless targeted optimizations are exhausted and profiling shows a clear net benefit.

## Updated VR Optimization Ranking

### 1. Lighting shader auxiliary-detail foveation

File:

- `package/Shaders/Lighting.hlsl`

Why it matters:

- This is the main lighting/material path.
- Many features inject defines into it.
- VR doubles view cost and uses high render targets.
- Optimizing optional compute passes does not help if the main lighting shader remains overloaded.

Hot areas identified:

- Shadowmask sampling in the lighting path.
- `ScreenSpaceShadows::GetScreenSpaceShadow`.
- Extended Materials parallax shadow calls.
- Directional lighting evaluation.
- Point light and LightLimitFix clustered light loops.
- Wetterness direct lighting per light.
- Wetterness indirect reflectance/lobe logic.
- Hair self-shadow/specular paths.
- PBR/glint paths.

Recommended approach:

- Do not foveate base diffuse lighting.
- Do not reduce main albedo/normal correctness.
- Add a VR "detail budget" or "foveated auxiliary quality" path.
- Center: full current quality.
- Feather: reduced auxiliary work.
- Periphery: reduce or skip only expensive detail terms.

Candidate reductions:

- Screen-space shadow contribution outside center.
- Parallax soft shadow quality outside center.
- Wetterness dynamic direct specular outside center.
- Hair self-shadow outside center.
- Glint/aniso detail outside center.
- Very small clustered lights outside center.

Expected win:

- Around 0.5-2.0 ms in feature-heavy scenes.

Risk:

- Medium-high because this touches the main lighting shader.
- Must be guarded by debug masks and toggles.

### 2. Utility shader shadowmask foveated filtering - removed

File:

- `package/Shaders/Utility.hlsl`

Why it was considered:

- Utility shadowmask permutations are heavy.
- The PCF helper uses 8 taps.
- Shadowmask generation is screen-space and VR-sensitive.

Hot areas identified:

- `SampleShadowPCF`
- `SampleDualParaboloidShadowPCF`
- cascade shadowmask sampling
- cascade blending
- focus-shadow extra sampling
- spot/paraboloid shadowmask variants

Current status:

- Do not reintroduce this path without a dedicated VR validation pass.
- The Utility shadowmask foveation implementation caused shadow flicker and has been removed.
- `Utility.hlsl` should keep direct `SampleShadowPCF` and `SampleDualParaboloidShadowPCF` calls for shadowmask PCF.

Rejected approach:

- Keep shadowmap generation unchanged.
- Foveate shadowmask filtering only.
- Center: 8 taps.
- Feather: 4 taps.
- Periphery: 1 or 2 taps.
- Focus shadows: full quality in center, reduced or skipped in periphery depending on visibility.

Expected win:

- Around 0.1-0.8 ms depending on shadow filter, focus shadows, and scene.

Risk:

- Medium.
- Peripheral shadow shimmer is possible if the transition is too sharp.

### 3. SSR foveated fallback

File:

- `package/Shaders/ISReflectionsRayTracing.hlsl`

Why it matters:

- The SSR shader has a fixed heavy raymarch path.
- Current raymarch can run up to 64 iterations plus binary refinement.
- In VR, peripheral SSR is less important and dynamic cubemap fallback is visually acceptable.

Recommended approach:

- Center: unchanged SSR.
- Feather: reduced SSR iterations.
- Periphery: skip SSR raymarch and use dynamic cubemap/reflection fallback.
- Fade between SSR and fallback in the feather band.

Expected win:

- Around 0.2-1.0 ms in reflective scenes.

Risk:

- Medium.
- Must avoid popping on head turns and reflective water edges.

### 4. Screen-space shadows foveated sample scaling

Files:

- `src/Features/ScreenSpaceShadows.h`
- `src/Features/ScreenSpaceShadows.cpp`
- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli`
- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/StereoSyncCS.hlsl`

Current branch state:

- VR sample scaling already exists.
- Stereo sync exists and is default enabled.
- UI warns that high sample counts are not recommended in VR.

Recommended approach:

- Center: current sample count.
- Feather: lower dynamic sample count.
- Periphery: low sample count or optional hard-off perf mode.
- Make stereo sync region-aware if possible.

Expected win:

- Around 0.2-0.8 ms.

Risk:

- Medium.
- Shadows are high contrast, so avoid hard cutoffs by default.

### 5. Water parallax foveation

Files:

- `package/Shaders/Water.hlsl`
- `features/Water Effects/Shaders/WaterEffects/WaterParallax.hlsli`

Why it matters:

- Water parallax has 16-32 step loops.
- Peripheral water micro-parallax is not as important as center detail.

Recommended approach:

- Keep base water color, reflection, refraction, and normal response.
- Center: current parallax.
- Feather: reduced parallax steps.
- Periphery: skip expensive parallax detail.

Expected win:

- 0-0.7 ms in water-heavy scenes.

Risk:

- Low-medium.

### 6. Wetterness dynamic-detail foveation

Files:

- `package/Shaders/Lighting.hlsl`
- `features/Wetterness/Shaders/Wetterness/Wetterness.hlsli`
- `package/Shaders/Common/LightingEval.hlsli`

Why it matters:

- Wetterness affects the main lighting shader.
- Dynamic rain droplets/ripples and wet direct specular can add cost.

Recommended approach:

- Keep base wetness, puddles, darkening, and broad reflection everywhere.
- Reduce only dynamic droplet/ripple/direct-specular detail outside center.
- Keep Wetterness separate from legacy WetnessEffects where possible.

Expected win:

- 0-0.5 ms in rain/wet scenes.

Risk:

- Low-medium if only dynamic microdetail is reduced.

### 7. LightLimitFix and particle/local light culling

Files:

- `src/Features/LightLimitFix.h`
- `src/Features/LightLimitFix.cpp`
- `package/Shaders/Lighting.hlsl`

Why it matters:

- Clustered lights and particle lights can be expensive in interiors.
- Lighting shader loops scale with strict and clustered light counts.

Recommended approach:

- Improve CPU/GPU culling before adding complex occlusion.
- Use combined-eye VR frustum/influence culling.
- Add distance and minimum-influence thresholds for tiny particle lights.
- Consider a foveated small-light budget, but do not remove large visible lights.

Expected win:

- Around 0.2-1.0 ms in light-heavy interiors.

Risk:

- Medium.
- Light popping is possible if thresholds are too aggressive.

### 8. Dynamic cubemap cadence and visibility throttling

Files:

- `src/Features/DynamicCubemaps.h`
- `src/Features/DynamicCubemaps.cpp`
- `features/Dynamic Cubemaps/Shaders/DynamicCubemaps/UpdateCubemapCS.hlsl`
- `features/Dynamic Cubemaps/Shaders/DynamicCubemaps/InferCubemapCS.hlsl`

Why it matters:

- Cubemap capture, inference, irradiance, and BC6H compression are spread over frames but still create recurring GPU work and spikes.

Recommended approach:

- Adapt update cadence to visible reflective contribution.
- Skip secondary/low-priority captures when water/wet/reflective surfaces are not visible.
- Lower cadence in interiors unless reflective materials are prominent.

Expected win:

- Around 0.1-0.8 ms average or spike reduction.

Risk:

- Low-medium.

### 9. SSS mask/tile/foveated reduction

Files:

- `src/Features/SubsurfaceScattering.cpp`
- `features/Subsurface Scattering/Shaders/SubsurfaceScattering/Burley.hlsli`

Recommended approach:

- Prefer tile/mask-based dispatch first.
- Reduce Burley samples outside foveated center if quality remains stable.

Expected win:

- 0-0.5 ms.

Risk:

- Medium.

### 10. Extended Materials/POM/parallax

Files:

- `package/Shaders/Lighting.hlsl`
- `features/Extended Materials/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli`

Why it matters:

- Expensive parallax and soft-shadow logic can be called from the main lighting shader.

Recommended approach:

- Do not start here unless profiling shows it dominates.
- Use distance and foveated step reduction, not broad disabling.
- Treat this as higher risk because it touches many material permutations.

Expected win:

- Around 0.2-1.5 ms in heavy terrain/PBR scenes.

Risk:

- High.

## Proposed Shared VR Detail Budget

A future implementation should avoid adding separate, inconsistent foveated controls to every feature. Prefer a shared concept:

- `center`: full quality
- `feather`: reduced quality with smooth transition
- `periphery`: cheap fallback or reduced auxiliary detail

Suggested HLSL utility:

- Use `FoveatedComputeMaskDistance`.
- Compute a stable `detailWeight` or `centerBlend`.
- Use the result to scale expensive auxiliary work.

Example policy:

- `centerBlend >= 0.99`: full quality.
- `0.0 < centerBlend < 0.99`: reduced quality or lerped contribution.
- `centerBlend <= 0.0`: periphery fallback.

Important:

- Avoid abrupt branches on high-contrast effects unless the feature already has a safe fallback.
- Use feathering for SSR, shadows, and wet reflections.
- Use hard-off only for low-contrast microdetail or explicit performance modes.

## Profiling Plan Before Implementation

Before changing code, collect baseline captures in at least these scenes:

- Interior with many local/particle lights.
- Exterior forest/terrain scene.
- Water-heavy scene.
- Wet/rain scene with Wetterness active.
- Reflective scene with SSR/dynamic cubemaps active.
- SSGI/AO enabled with FOV SSGI on and off.

Measure:

- Full frame GPU time.
- Lighting draw cost if visible in capture.
- Utility shadowmask cost.
- SSR raytracing cost.
- Screen-space shadows pass and stereo sync cost.
- SSGI/AO prefilter, GI/AO, blur, stereo sync, center pass costs.
- Water and Wetterness cost if separable in capture.
- Dynamic cubemap update spikes.

Use existing perf markers where available. Add temporary markers if a hot path is not visible enough in captures.

## Recommended First Implementation Sequence

1. Keep Utility shadowmask PCF unfoveated unless a new fix is proven stable in VR.
2. Implement SSR foveated fallback because it has a clear hot loop and fallback path.
3. Implement screen-space shadow foveated sample scaling.
4. Implement Wetterness dynamic-detail foveation.
5. Implement water parallax foveation.
6. Implement LightLimitFix culling improvements.
7. Investigate remaining SSGI prefilter/gating skips.
8. Only then consider SSS and Extended Materials.

## Expected Aggregate Savings

These are rough GPU frame-time estimates:

- Conservative average gameplay: 5-10%.
- Heavy feature scenes: 10-20%.
- Best-case scenes with SSR, shadows, wetness, water, and many lights: 20-30%.
- Scenes where targeted features are off/not visible: near 0%.

Avoid overpromising. Actual savings depend heavily on headset resolution, upscaler mode, shadow settings, weather, water visibility, and modded asset/material density.

## Main Risks

- Stereo mismatch between eyes if foveated decisions differ per eye.
- Head-turn popping at foveated boundaries.
- Temporal instability if history buffers do not know foveated state changed.
- Peripheral shadow shimmer.
- Wetness/reflection discontinuities in rain.
- SSR fallback popping on reflective surfaces.
- Interaction with dynamic resolution and per-eye texture layout.
- OpenComposite/OpenVR differences.

Mitigations:

- Use existing per-eye mask offsets.
- Keep mask math shared through `FoveatedCommon.h` and `FoveatedMask.hlsli`.
- Reset temporal history when foveated settings change.
- Add debug mask visualization before behavior changes.
- Keep user toggles for each major optimization.
- Prefer fail-open/full-quality if data is invalid.

## What Not To Do First

- Do not reintroduce global #2002 stereo reprojection as the first optimization.
- Do not hard-disable base lighting outside the foveated center.
- Do not globally cull main game geometry behind the HMD from this plugin.
- Do not start with complex depth-occlusion culling.
- Do not foveate Extended Materials first unless profiling proves it is dominant.
- Do not add independent foveated masks per feature if they can share the existing upscaling FOV mask.

## Quick File Reference

Core foveation:

- `src/Features/FoveatedCommon.h`
- `package/Shaders/Common/FoveatedMask.hlsli`

Upscaling foveation:

- `src/Features/Upscaling.h`
- `src/Features/Upscaling.cpp`
- `features/Upscaling/Shaders/Upscaling/FoveatedPeripheryCS.hlsl`
- `features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl`
- `features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl`

FOV SSGI and stereo sync:

- `src/Features/ScreenSpaceGI.h`
- `src/Features/ScreenSpaceGI.cpp`
- `features/Screen Space GI/Shaders/ScreenSpaceGI/common.hlsli`
- `features/Screen Space GI/Shaders/ScreenSpaceGI/stereoSync.cs.hlsl`

Stereo shared helpers:

- `package/Shaders/Common/VR.hlsli`

Screen-space shadows:

- `src/Features/ScreenSpaceShadows.h`
- `src/Features/ScreenSpaceShadows.cpp`
- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli`
- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/StereoSyncCS.hlsl`

Highest-priority common shaders:

- `package/Shaders/Lighting.hlsl`
- `package/Shaders/Utility.hlsl`
- `package/Shaders/Common/LightingEval.hlsli`
- `package/Shaders/Common/ShadowSampling.hlsli`

Other targets:

- `package/Shaders/ISReflectionsRayTracing.hlsl`
- `package/Shaders/Water.hlsl`
- `features/Water Effects/Shaders/WaterEffects/WaterParallax.hlsli`
- `features/Wetterness/Shaders/Wetterness/Wetterness.hlsli`
- `src/Features/LightLimitFix.cpp`
- `src/Features/DynamicCubemaps.cpp`
- `src/Features/SubsurfaceScattering.cpp`
- `features/Extended Materials/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli`

## Final Recommendation

The best next work is not one giant optimization. It is a staged VR detail-budget system:

1. Reuse the existing foveated mask.
2. Do not apply it to Utility shadowmask filtering until the previous flicker regression has a proven root-cause fix.
3. Apply it to SSR and auxiliary lighting work in `Lighting.hlsl`.
4. Keep stereo blending feature-local.
5. Avoid global reprojection until profiling proves it is worth its fixed cost and correctness risk.
