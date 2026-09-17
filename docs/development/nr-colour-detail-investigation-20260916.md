# Preserve Source detail investigation

The saved brighter-scene images still do not establish useful new neural
detail. The follow-up found and corrected a separate, reproducible shader
defect: Preserve Source cancelled alternating fine detail because its
low-frequency estimate sampled every second pixel. Increasing detail
strength could not recover that lost signal.

This is a shader-level correction, not a demonstrated improvement to the
captured faces. The corrected build has now been deployed and exercised in
the [focused HMD strength comparison](nr-colour-detail-hmd-comparison-20260916.md).

## Saved-image evidence

The [brighter-scene comparison](nr-colour-brighter-comparison-20260916.md)
already contains blinded colour/detail assessments for both acquisition
orders. Follow-up inspection of the native face crops is unblinded and
does not replace those assessments. Existing canonical regional metrics
also give no clear added-detail signal:

| Preserve Source, shown minus hidden | Left forward | Left reverse | Right forward | Right reverse |
| ----------------------------------- | -----------: | -----------: | ------------: | ------------: |
| Face edge RMS                       |    -0.225529 |    +0.004655 |     -0.199196 |     -0.005637 |
| Face local contrast                 |    +0.031790 |    +0.115790 |     +0.048928 |     +0.117869 |

Every difference is smaller in magnitude than the corresponding full-run
NR-off span: edge RMS 0.645893 left / 0.543389 right; local contrast
0.166109 left / 0.208536 right. These are descriptive measurements in
encoded image values, not significance tests or proof of neural quality.
Motion, expression and lighting limitations remain as previously recorded.

## Reproduced defect and correction

Preserve Source subtracts a local average from the neural log-brightness
residual, bounds the remaining detail in stops, and applies that scalar
gain to source RGB. Appearance mix zero retains source chroma in the
selected working domain. Unknown/native remains explicitly uncalibrated.

With offsets -2, 0 and +2, all nine samples land on the same parity for
alternating horizontal stripes, vertical stripes or a checkerboard.
On a constant source, the local average therefore equals the residual
at each interior pixel. Subtraction removes the entire alternating
signal, regardless of detail strength. The original WARP shader test
covered identity and uniform lighting, which cannot reveal this defect.

The correction uses adjacent offsets -1, 0 and +1. It retains nine taps,
the existing source-dependent weights, centre weight, physical ROI
clamping, four-pixel edge fade, stop bounds and fallback rules. It changes
the frequency response: broader residuals can be attenuated more than
before. It must not be described as preserving every spatial frequency
or proving that additional high-frequency content is useful detail.

The new regression runs the real preparation and reconstruction shaders
on WARP with a constant coloured source and alternating neural edits of
plus/minus 0.25 stops. For interior bright samples, observed results are:

| Pattern                        | Strength 0 | Strength 1 | Strength 2 |
| ------------------------------ | ---------: | ---------: | ---------: |
| Horizontal or vertical stripes |          0 |   0.250000 |   0.500000 |
| Checkerboard                   |          0 |   0.166667 |   0.333333 |

Results agree within floating-point tolerance across unknown/native,
linear and sRGB working-domain settings. Tests cover both signs/parities,
source chroma and alpha, exact zero-strength output, ROI borders and
untouched surrounding pixels, and enforced 0, 0.125 and 1 stop limits.
The old shader fails the new retained-detail assertion.

## Scope and compatibility review

Only the shared reconstruction shader changes production behaviour. Raw
NR, Managed, display-hidden and full-appearance endpoints retain their
existing branches. Settings, defaults, schemas, inference input/history
and character-mask application are unchanged. Existing DevBench detail
and appearance controls exercise this correction without a new API.

The same shader serves SE, AE and VR, without runtime-specific branches.
No D3D resource, context access, lock, render-thread ownership, readback or
stereo commit boundary changes. The intent of
`5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5`, removing permanent D3D11
protection, is preserved. The regression reuses the existing WARP fixture
and colour conversion helpers; no parallel production filter was added.
Nine taps is a scope fact, not a measured performance claim.

## Validation and live comparison

Source checkout is `work/face-of-gogh-colour-managed-20260914`, starting
from `fd6afdec41c21cd5d600ba2bf029b1509c878e62`. Local evidence is under
`build/validation/nr-detail-investigation-20260916`.

-   Release build of `nr_color_shader_gpu_test` through `tools/cmake.ps1`
    passed using the existing `.tmp/hmd916-tests-color` build directory.
-   New regression against the original shader failed as expected with
    `alternating neural detail must not disappear`.
-   `ctest --test-dir .tmp/hmd916-tests-color -C Release --output-on-failure`
    passed all 19 tests with the task's validated Python dependencies.
-   The expanded shader test passed 417,859 checks, including existing
    identity, uniform-lighting and appearance-endpoint checks.
-   Existing HLSL X3571 and X4000 warnings remain in the saved compiler output.
    No warning-free compilation or live NGX/NVIDIA validation is claimed.

The completed focused comparison used Preserve Source at detail strengths
0, 1 and 2, appearance mix zero and maximum detail stops one, plus
inference-running display-hidden controls. It retained the user's FOV
0.95, foveated dispatch, peripheral TAA off and existing DLSS. Thirteen
sequences produced 156 attributed stereo pairs in forward/reverse order.
See the [HMD report](nr-colour-detail-hmd-comparison-20260916.md) for image
findings, physical DLL/shader identity, restoration and retained limits.

The installed AIO now contains the corrected shader. A plugin refresh and
full host reload exposed the exact typed NR actions; all live work used
the direct MCP lane. Strength two remains an experiment, not a new default.
An increase in edge contrast alone does not demonstrate useful detail.
