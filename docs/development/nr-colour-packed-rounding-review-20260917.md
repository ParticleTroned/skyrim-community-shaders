# Adversarial review of Preserve Source packed rounding

Reviewed correction `a30c3d6fa5d33234c63dbce2151302248d1961ed` for
correctness, robustness, duplication and scope. No blocking production
defect was found. Two validation issues were corrected in this follow-up.

## Findings addressed

1. **Compiler coverage:** the GPU fixture compiled with IEEE strictness,
   while the normal game path uses strictness plus optimization level 3.
   The packed helper, actual UAV round trips, integrated reconstruction
   and invalid-candidate cases now run under both flag combinations.
   This checks the production optimization path without changing it.
2. **Explicit adapter selection:** the fixture first created a device on
   the default hardware adapter, even when another adapter was requested.
   An unavailable default device could prevent testing the selected GPU.
   It now enumerates the requested adapter before creating the single
   test device. Default WARP and unqualified hardware selection remain.

The follow-up changes only the existing test fixture, its DXGI link
dependency and documentation. Production shader bytes are unchanged.

## Correctness and robustness

-   Red/green rounding retains six fraction bits; blue retains five.
    Integer rounding handles ties to even and exponent carries. The
    separate subnormal path uses exact power-of-two scaling and covers
    the transition to normal values. Already representable inputs are
    unchanged.
-   Existing finite, nonnegative and per-channel maximum checks bound the
    helper's packed inputs. The largest admitted values cannot round to
    infinity. Nonpacked storage returns its input unchanged.
-   The CPU oracle enumerates neighbouring packed values and uses their
    distances and code parity. It does not copy the shader bit algorithm.
    Tests include every finite packed code, each midpoint and adjacent
    FP32 values, zero, subnormals, exponent transitions and maxima.
-   Integrated tests compare packed stores with independently rounded
    FP32 reconstruction. They cover all working domains, tiny and larger
    detail, appearance mixing and rejected candidates. Hidden, identity,
    zero-strength and zero-stop endpoints remain exact in these cases.
-   Raw, Managed, transport and full-appearance endpoints keep their
    existing paths. The correction changes the final Preserve Source
    store, including intermediate appearance mixing.

Nearest rounding removes the reproduced one-sided truncation bias; it
does not promise zero mean error for arbitrary images. Independent channel
quantization can slightly change chroma or exceed a prequantization stop
bound by the storage rounding error. Sub-precision neural edits can still
disappear. These are precision limits, not evidence of retained useful
detail or complete recovery of the earlier face-brightness difference.

## DRY, scope and compatibility

A full source/shader search found no existing packed nearest-rounding
helper to reuse. Existing half-float/BC6H encoders and CPU packed decoders
serve different contracts. The helper remains local to its only
production consumer; tests reuse the existing dispatch/readback fixture.

No defaults, colour-domain policy, mask, ROI, transport, resource
allocation, graphics state or renderer ownership change is introduced.
The shader is shared by SE, AE and VR without a new runtime branch.
The intent of `5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5` remains intact:
neither the correction nor this follow-up touches immediate-context
protection, renderer ownership or locking. This is a scope review, not
validation of a future port onto that branch.

## Validation

-   Release `nr_color_shader_gpu_test` rebuild passed.
-   All 19 NR CTest cases passed in 11.17 seconds.
-   The expanded WARP and explicit RTX 4090 runs passed, including both
    compiler modes. The NVIDIA run performed 1,395,985 checks.
-   Flat/ramp tiny-edit and uniform-half cases retained zero packed luma
    delta in both compiler modes.
-   The original correction's negative control, disabling only the final
    rounding call, remains evidence that the integrated test detects the
    original defect.
-   Existing HLSL X3571/X4000 warnings remain. Live Skyrim/NGX and SE/AE/VR
    gameplay were not exercised by these standalone tests.

Commands used the repository CMake wrapper to build
`.tmp/hmd916-tests-color`, then
`ctest --test-dir .tmp/hmd916-tests-color -C Release --output-on-failure`
with the preserved HMD Python dependencies. Hardware execution used
`nr_color_shader_gpu_test.exe <colour-shader-directory> --hardware 1`.
Exact output is retained under
`build/validation/nr-packed-rounding-review-20260917`.

The next check is the corrected DevBench AIO in a short counterbalanced
HMD comparison, with FOV 0.95 and peripheral TAA off. Capture private
same-frame measurements alongside final masked images to distinguish
remaining reconstruction bias from later compositing and scene changes.
