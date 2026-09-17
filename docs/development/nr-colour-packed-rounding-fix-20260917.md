# Preserve Source packed-output rounding correction

Preserve Source could darken small neural detail when its reconstructed
result was stored in an R11G11B10 texture. Tiny positive changes could
disappear while tiny negative changes stepped down to a lower representable
value. Explicit nearest-value rounding before that packed store removes
this numerical bias.

The correction retains the adjacent-tap detail filter, existing defaults,
input profiles, stop limits and character-mask policy. It does not establish
useful new neural detail or resolve the entire previously observed face
brightness difference.

## Evidence

The saved Dragonsreach colour assessment used corrected-detail source
`7f3f6279c6c43f8998a6ff43bfe51d98ae70d57d` and report HEAD
`61ae53f78472ddfa36da75d2b7b1e669c5bf936b`.
Its retained complete private GPU batches compare source and result from
the same frame and sampled pixels. All 16 shown Preserve Source eye
measurements had negative mean RGB changes and zero invalid or nonfinite
samples. Private mean-luma changes were −0.571% to −0.611% left and
−0.591% to −0.621% right. Hidden and zero-strength controls were exact.

These measurements precede the final character mask and describe the
whole sampled private region. They are not face-only display measurements.
No retained private batch exactly matched a saved screenshot's batch key,
so the sequential face-crop change of roughly 2–4 encoded luma codes must
not be equated with this same-frame measurement.

The unchanged shader reproduced the bias on WARP with identical packed
source/neural inputs and different result storage:

| Synthetic case                             | FP32 mean-luma change | Original packed store | Corrected packed store |
| ------------------------------------------ | --------------------: | --------------------: | ---------------------: |
| Flat, tiny alternating edit                |            +0.000930% |            −0.644178% |                     0% |
| Ramp, tiny alternating edit                |            +0.001233% |            −0.640455% |                     0% |
| Ramp, uniform half-brightness neural input |         +0.000000663% |            −0.325712% |                     0% |

The tiny neural edits were requested at ±0.002 stops before packing.
Below-precision edits can disappear after unbiased quantization; that is
not useful-detail success. Larger ±0.25-stop alternating inputs retain
both positive and negative detail after the correction.

## Implementation and review

Only the Preserve Source reconstruction shader changes production behaviour.
After existing finite/range validation, its final result is rounded to the
nearest representable R11G11B10 value, with ties to even. Normal values
retain six fraction bits for red/green and five for blue. Packed subnormals
use their fixed 2^-20 and 2^-19 steps, including the transition to normal
values. The helper returns other storage formats unchanged.

Raw, Managed, transport bypass, display-hidden, zero-strength/zero-appearance
and full-appearance endpoints retain their existing early-return paths.
Intermediate appearance mixes use the corrected final Preserve Source store.
No source-domain selection, global brightness adjustment or texture-format
change is introduced.

The helper is local to reconstruction; a full shader search found no existing
packed nearest-value helper to reuse. The regression reuses the existing
WARP texture, shader compilation, dispatch and readback fixture. Its
independent CPU oracle enumerates finite packed values and selects the
nearest neighbours rather than duplicating the shader's bit-rounding
implementation.

The shared shader serves SE, AE and VR without runtime-specific divergence.
No production C++, GPU admission, resources, D3D state ownership, protection,
threading, screenshot export or DevBench schema changes are needed.
The intent of `5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5`, removing permanent
D3D11 protection, remains intact. Existing mode/detail controls exercise
the corrected path.

## Validation

The existing Release shader-test target was built through the repository
CMake wrapper:

```powershell
pwsh ./tools/cmake.ps1 --build .tmp/hmd916-tests-color --config Release --target nr_color_shader_gpu_test
```

That build directory points to this task checkout's `tests/neural_color`.
The expanded regression covers every finite positive packed code, exact
midpoints and the adjacent FP32 values around those ties, zero, subnormals,
normal/exponent boundaries, maximum finite values and packed UAV round trips.
It also covers all three working-domain selections, small/large alternating
detail, identity/hidden/zero/zero-stop endpoints, Raw/Managed/transport and
appearance endpoints, intermediate appearance mixing and invalid candidates.

-   WARP and NVIDIA RTX 4090 pass the final shader regression, including
    906,925 checks on the explicit NVIDIA adapter.
-   A derived shader with only the final rounding call disabled fails the
    new production-store assertion as expected.
-   `ctest --test-dir .tmp/hmd916-tests-color -C Release --output-on-failure`
    passes all 19 tests with the validated HMD Python dependencies.
-   Source asset inventory and shared-route/controller contracts pass in that
    suite.
-   Existing HLSL X3571/X4000 warnings remain; no warning-free build is claimed.
-   Skyrim/NGX, SE/AE/VR gameplay and a corrected-build HMD comparison have not
    been rerun. The standalone GPU test is not a live game qualification.

The shader executable accepts an optional explicit hardware adapter:

```text
nr_color_shader_gpu_test.exe <colour-shader-directory> --hardware 1
```

It prints the adapter name/vendor/device; the recorded adapter 1 is
NVIDIA GeForce RTX 4090, vendor 10de, device 2684. Default execution remains
WARP for reproducible automated tests.

## Evidence and next step

Local immutable assessment and reproduction evidence remains under:

-   `build/validation/nr-colour-followup-20260916T214643Z`
-   `build/validation/nr-same-frame-bias-20260916T233000Z`

Implementation validation is under
`build/validation/nr-packed-rounding-fix-20260917`, including original
shader copies, the derived failing control, compiler output and test receipts.
Raw image/measurement trees are not committed.

Prepare a corrected DevBench-enabled AIO, then run a short counterbalanced
Preserve Source shown/hidden comparison at existing default strength,
FOV 0.95 and peripheral TAA off. Preserve fresh private measurement batches
and exposure readbacks during capture. Recheck both the private signed bias
and final face brightness before attributing the entire visual difference
to this fix. Useful-detail, final masked-composite precision, exposure
recovery and headset-rate temporal questions remain distinct follow-ups.
