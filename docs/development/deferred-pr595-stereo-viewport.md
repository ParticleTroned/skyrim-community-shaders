# Open Shaders #595 - Eye 1 repair viewports

Status: not applicable to `main-VR` at
`7fcffd9a5b68c5d48bb6460054d5fc45319db5dd` (reviewed 2026-09-12).

[Open Shaders #595](https://github.com/alandtse/open-shaders/pull/595),
merged as `9301e29b67cbf332d83a04bf2b006ab0b8b9f187`, sizes stereo
stencil-write and depth-fill viewports from the classified dynamic frame
dimensions. It also exposes classified dimensions, mode-texture dimensions,
and frame flags through the upstream VR feature's diagnostics interface.
This is the viewport follow-up identified in the
[#594 assessment](deferred-pr594-depth-fill.md).

## Adversarial review

-   **Scope:** `VRStereoOptimizations`, `SetEye1Viewport`, its classification
    and repair passes, and the `stereoOpt` member are absent. They belong to
    the deliberately deferred [stereo-reprojection feature](../../deferred_PRs.md#2002---vr-stereo-reprojection).
    Adding the feature would exceed the scope of this viewport fix.
-   **Correctness:** the existing `VR::DrawStereoBlend` performs a compute
    dispatch and does not set an Eye 1 rasterizer viewport. It derives
    `FrameDim` through `Util::ConvertToDynamic` and uses
    `Util::GetScreenDispatchCount` with the same `submitStageSceneDomain`
    argument. The upstream physical-width/classified-width mismatch has
    no equivalent operation here.
-   **Compatibility and robustness:** the two changed upstream `VR` files
    only forward diagnostics to `stereoOpt`. This fork also lacks the
    `Feature::GetDiagnostics` virtual interface, so those hunks cannot be
    copied independently. The local DevBench bridges already expose this
    fork's depth-culling and render-scale state through their own contracts.
-   **Reuse:** existing sizing helpers serve the local stereo pass. No
    additional cached dimensions, helper, or diagnostics surface is needed
    for an absent feature. No applicable implementation defect was found.

## Validation

-   Reviewed the complete four-file upstream diff, description, and discussion.
-   `rg` searches across `src`, `package`, and `features` found no
    `VRStereoOptimizations`, `SetEye1Viewport`, `DispatchStencil`,
    `DispatchGBufferFill`, `stereoOpt`, or `GetDiagnostics` implementation.
-   Inspected `VR::DrawStereoBlend`, its shader, the sizing helpers in
    `src/Utils/Game.cpp`, `Feature.h`, and the existing DevBench bridges.
-   No C++ or HLSL changed, so build and shader execution tests were not run.
    In-game tests were omitted at the user's request. Upstream's headset
    comment only confirms ratio 1.0; it does not validate the dynamic-ratio
    path on a headset or establish any runtime result for this fork.

Revisit this fix with the deferred stereo-reprojection integration. Check
integer eye extents, frame-dimension lifetime across resizes, and coherent
diagnostics snapshots in that implementation; retain the existing fork's
dynamic-resolution and DevBench contracts.
