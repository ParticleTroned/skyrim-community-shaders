# Upscaling Dynamic-Resolution Path Notes

This note records the state after commit `a4467d79d` (`refactor(vr): remove legacy dynamic upsample path`) plus the current uncommitted `src/Features/Upscaling.cpp` follow-up analysis.

## Last Commit Summary

Commit `a4467d79d` removes the legacy VR dynamic-upsample replacement path that had been added for submit-path handling.

The removed path included:

1. The `TryReplaceVanillaDynamicResolutionUpsample` API and `DynamicResolutionUpsampleStage`.
2. Replacement render/dispatch hooks for `ISUpsampleDynamicResolution`, `ISCopyDynamicFetchDisabled`, and `ISFullScreenVR`.
3. FrameAnnotations forwarding into the upscaling replacement path.
4. The `submitPathDisabledForVendor` fallback branch in `Main_PostProcessing`.
5. The post-load fade/UI sanitize state that was only consumed by the deleted `ISCopyDynamicFetchDisabled` hook.
6. The diagnostic dependency on the `preISCopy` boundary, renamed to `nativeMainPost`.

The commit also restores PL3.14-style hook-time disabling of the original vanilla/manual dynamic-resolution system:

```cpp
REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D, 0x25), REL::NOP5, sizeof(REL::NOP5));
```

That write is currently unconditional again, matching `v1.5.2_PL3.14-AIO-VR`.

## Current Uncommitted Follow-Up

The current working tree contains an `Upscaling.cpp` follow-up that was reviewed for scope, correctness, robustness, and DRY.

The follow-up currently does this:

1. Adds `ForceFullResolutionDynamicResolutionState(RE::BSGraphics::State*)` to centralize the repeated full-resolution dynamic-resolution reset.
2. Keeps the `UpdateCameraData()` forward declaration at file scope so helper calls use the correct linkage.
3. Replaces duplicated reset blocks in `ConfigureUpscaling`, `ApplyDynamicResolutionState`, and `PrepareFullResolutionPostProcessing`.
4. Fixes `UpscaleDepth()` so the VR full-resolution vendor path is detected functionally, not only by `qualityMode == 0`.
5. Leaves the vanilla/manual dynamic-resolution `safe_write` unconditional in `PostPostLoad()`.

The follow-up intentionally does not touch the unrelated dirty `extern/CommonLibSSE-NG` submodule state.

## Comparison With `v1.5.2_PL3.14-AIO-VR`

`v1.5.2_PL3.14-AIO-VR` did not have the removed custom dynamic-upsample replacement path. It also disabled vanilla/manual dynamic resolution unconditionally in `Upscaling::PostPostLoad()`.

Current branch matches PL3.14 in these aspects:

1. `PostPostLoad()` disables the original dynamic-resolution system unconditionally.
2. There is no `TryReplaceVanillaDynamicResolutionUpsample` API.
3. There are no custom replacement hooks for `ISUpsampleDynamicResolution`, `ISCopyDynamicFetchDisabled`, or `ISFullScreenVR`.
4. FrameAnnotations only keeps its normal image-space annotation hooks; it no longer forwards those shader passes into upscaling replacement logic.

Current branch differs from PL3.14 in these important aspects:

1. PL3.14 configured VR vendor methods from the selected quality mode even when no submit path existed. Current code forces VR vendor methods to full resolution whenever VR render-scale mode is inactive.
2. PL3.14 `IsUpscalingActive()` returned true for VR vendor methods when `resolutionScale < .99`. Current code returns false for VR vendor methods when render-scale mode is inactive.
3. PL3.14 `Main_PostProcessing()` used a simple flow: run `PerformUpscaling()` for vendor methods before the original post-processing call. Current code still has newer split handling for `vendorDynamicResolutionActive` and presentation upscaling.
4. PL3.14 full-resolution underwater/depth mask repair treated `NONE`, `TAA`, and native vendor quality as full-resolution paths. Current follow-up treats any VR vendor path outside render-scale mode as full-resolution, because current render-scale-off VR vendor handling is full-resolution.

## Behavioral Conclusion

The branch now matches PL3.14 for the specific hook-time dynamic-resolution disable and for the absence of the legacy custom dynamic-upsample replacement path.

It does not match PL3.14 for render-scale-off VR vendor behavior. PL3.14 still allowed reduced-resolution vendor upscaling through the regular quality-mode path. Current branch instead makes reduced-resolution VR upscaling owned by VR render-scale mode only; when render-scale mode is inactive, VR vendor methods behave as full-resolution native AA/DLAA.

If the intended target is strict PL3.14 no-submit-path behavior, the remaining work is to restore the quality-mode-driven VR vendor path outside render-scale mode and adjust `IsUpscalingActive()`, `Main_PostProcessing()`, and `UpscaleDepth()` accordingly.
