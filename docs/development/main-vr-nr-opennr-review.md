# Reduced-resolution NR: OpenNR comparison

This review compares CSX `709eb9a1d` with OpenNR commit
`29d9218f17daf323cf67996d40b0210a7ea6e6b8`, fetched from the public
repository on 2026-09-17. Only selected source files were downloaded to
the ignored `build/reference` directory. No OpenNR code was imported.

## What the source establishes

-   OpenNR has two distinct controls: a smaller model image with a spatial
    reconstruction pass, and an experimental NR-before-upscaling route.
    The former supports 33, 50, 75, 85, 90 and 100 percent, keeps guide
    dimensions separate, and scales motion-vector displacement by the
    model/source dimension ratio. Its successful evaluations retain NR
    history. [Renderer.cpp](https://github.com/olekspa/OpenNR/blob/29d9218f17daf323cf67996d40b0210a7ea6e6b8/src/Features/Upscaling/NeuralRendering/Renderer.cpp#L85-L123),
    [evaluation and history](https://github.com/olekspa/OpenNR/blob/29d9218f17daf323cf67996d40b0210a7ea6e6b8/src/Features/Upscaling/NeuralRendering/Renderer.cpp#L435-L494).
-   Its spatial input filter can integrate the exact source-pixel footprint.
    Its reconstruction can transfer a bounded model-minus-proxy residual
    onto the original image, preserving original alpha. Those are explicit
    color/detail choices, not interchangeable with temporal DLSS upscale.
    [ModelResolutionCS.hlsl](https://github.com/olekspa/OpenNR/blob/29d9218f17daf323cf67996d40b0210a7ea6e6b8/features/Upscaling/Shaders/Upscaling/NeuralRendering/ModelResolutionCS.hlsl#L74-L140).
-   Its pre-upscale route runs on the native scene before ordinary upscale.
    VR admission requires full-eye geometry, prepared per-eye guides, DLSS,
    and no menu, HDR Display or frame-generation conflict. Failure permits
    the later post-upscale NR route; successful pre-upscale processing skips
    that second route. [Integration.cpp](https://github.com/olekspa/OpenNR/blob/29d9218f17daf323cf67996d40b0210a7ea6e6b8/src/Features/Upscaling/NeuralRendering/Integration.cpp#L313-L497),
    [hook ordering](https://github.com/olekspa/OpenNR/blob/29d9218f17daf323cf67996d40b0210a7ea6e6b8/src/Features/Upscaling.cpp#L3350-L3377).
-   The inspected NGX parameter setup does not submit a jitter offset. It
    sets the feature-upscaling bit unconditionally. Neither detail establishes
    a general Feature 18 contract for another renderer or image geometry.
    [Runtime.cpp](https://github.com/olekspa/OpenNR/blob/29d9218f17daf323cf67996d40b0210a7ea6e6b8/src/Features/Upscaling/NeuralRendering/Runtime.cpp#L303-L375).

## Consequences for CSX mode C

CSX C evaluates Feature 18 at the engine render resolution with matching
color, output, depth and motion extents, then passes the selected image to
DLSS. It does not create OpenNR's additional smaller proxy. Adding a second
downsample would change the requested route and introduce another color
and guide contract.

The existing crop descriptor supplies full-eye pixel motion scale. C maps
its output crop to its input crop and resets Feature 18 on every
evaluation; DLSS remains the temporal owner and receives the established
render jitter. This is a deliberate difference from OpenNR's retained NR
history. No inference about temporal quality is claimed without live
moving-scene measurements.

For C+D, character masks and ROI plans use the render-resolution extent.
Both candidate images must complete before either is consumed. Each
selected texture starts with the original baseline, then replaces only
produced regions using the authored mask. An empty eye retains its normal
DLSS input. Failure preserves the ordinary upscaling route without silently
switching to a different NR insertion point.

The existing color-managed path remains authoritative. OpenNR's bounded
residual and area-filter approach provides a concrete reference for the
later dedicated Vincent color work; it is not evidence to substitute that
work into this port.

## Confirmed defect and correction

The prepared submit composite selected `submitNeuralFloatColorOut` whenever
NR had succeeded. C intentionally does not allocate that post-upscale
bridge. A successful C/C+D frame could therefore fail on a missing texture
or read a retained A/B texture after a mode switch.

Producer and consumer now share `UsesSubmitNeuralFloatBridge`. C always
composites its completed DLSS output. Tests cover successful C submit,
ordinary fallback, main/submit separation, and A/B-to-C transitions with
both retained and absent bridge resources. The source integration check
also requires both production sites to use the arrangement-aware policy.

## Avoided full-image work

Full-image C previously filled the periphery before overwriting the entire
target with the final DLSS image. The non-TAA path now omits that fill only
when the current center dispatch completed DLSS, the visible rectangle
covers every output pixel, and its zero-origin source rectangle and
allocation exactly match the target extent. Mask visualization, C+FOV,
unprepared output, and periphery TAA retain their existing paths.

The guard uses DLSS completion rather than NR success: empty character
masks and a failed/unavailable NR provider still produce complete normal
DLSS output. A failed or partial preparation cannot enable the omission.
The existing final composite and stereo publication/fallback rules remain
in control. Tests include odd-sized geometry, incomplete borders, and a
NaN-poisoned destination that a full-image blend must completely replace.
This removes a structurally redundant dispatch; no timing improvement is
claimed without runtime measurement.

## Validation and limits

-   `pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target neural_rendering_pipeline_policy_test --parallel 2`: passed.
-   `./build/ALL/Release/neural_rendering_pipeline_policy_test.exe`: passed.
-   `pwsh ./tools/cmake.ps1 -P tests/neural_rendering_submit_pair_contract_test.cmake`: passed.
-   An isolated CMake project compiled the unchanged production test sources
    `foveated_region_plan_test.cpp` and `character_mask_gpu_test.cpp`, avoiding
    concurrent regeneration of the DLL build:
    `pwsh ./tools/cmake.ps1 --build build/reference/reduced-review-tests/out --config Release --parallel 2`.
-   `ctest --test-dir build/reference/reduced-review-tests/out -C Release --output-on-failure`:
    all three tests passed (region coverage, character mask/capture, and
    final-LDR blend). This includes eight odd-sized full-image cases with
    poisoned HDR destinations and 104 final-LDR blend cases. The region-plan
    test is now registered in the normal controller suite as well.

The focused build regenerated CMake and emitted the existing FidelityFX
CMP0116 deprecation notice. No live Feature 18 image, timing, stability or
quality result was obtained by this source review. The complete DLL build
and broader validation belong to the enclosing review.
