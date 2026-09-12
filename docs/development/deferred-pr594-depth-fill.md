# Open Shaders #594 - Culled Eye 1 depth fill

Status: not applicable to `main-VR` at
`d6cada629e1f4395d6c0b793f54223f3cc5dc0cf` (reviewed 2026-09-12).

Source:

-   [alandtse/open-shaders#594](https://github.com/alandtse/open-shaders/pull/594),
    merged as `10223a461550a3d9a8e8f37a7c373e0cc7980692`.
-   The one-file patch removes `[earlydepthstencil]` from
    `package/Shaders/VRStereoOptimizations/DepthFillPS.hlsl`. Upstream reports
    that forced early depth writes the fullscreen triangle's zero depth in
    place of `SV_Depth`, leaving culled Eye 1 pixels black after upscaling.

Adversarial review (scope, correctness, robustness, and reuse):

-   The shader, its host pass, `StereoMode`, and the `VRStereoOptimizations`
    feature are absent. This is the deferred [#2002](../../deferred_PRs.md#2002---vr-stereo-reprojection)
    feature line, so adding that feature would exceed the scope of this fix.
-   The existing `VR::DrawStereoBlend` uses a compute shader to blend color;
    it reads depth but does not write the depth buffer. Native
    `VRDepthCullingTemporal` recovers object visibility and does not implement
    the upstream stencil-culled Eye 1 depth-fill pass.
-   The two local depth-output shaders, `ISCopy.hlsl` with `DEPTHBUFFER_COPY`
    and `Upscaling/DepthRefractionUpscalePS.hlsl`, already omit forced early
    depth/stencil. No equivalent occurrence was found in `src`, `package`,
    or `features`; no code correction or additional helper is needed.
-   The upstream description's separate `SetEye1Viewport` dynamic-resolution
    follow-up also concerns the absent feature. It is not evidence of that
    defect in this branch's viewport code.

Validation:

-   Source searches and feature-registration/call-path inspection confirm the
    distinctions above.
-   Windows SDK 10.0.28000.0 FXC compiled 10 `ps_5_0` permutations with
    `/Ges /WX /O3`: flat/VR depth-refraction upscaling and flat/VR `ISCopy`
    depth output across both `DISABLE_DYNAMIC` and `DEPTHBUFFER_4X_DOWNSAMPLE`
    toggles. All passed; every disassembly declares `oDepth` and none enables
    `forceEarlyDepthStencil`.
-   Reproduction script, compiler logs, DXBC, disassembly, and result hashes:
    `build/pr594-depth-fill-evidence-20260912/verify-depth-shaders.ps1` and its
    adjacent outputs (local evidence).
-   In-game tests were omitted at the user's request. No rendering code was
    changed; no runtime visual or performance result is claimed.

If revisited:

-   Include this fix when deliberately integrating the deferred stereo
    reprojection feature and its later depth-fill pass. Reassess stencil
    ownership, dynamic-resolution bounds, and frame cost with that feature.

The viewport follow-up is assessed separately in
[Open Shaders #595](deferred-pr595-stereo-viewport.md).
