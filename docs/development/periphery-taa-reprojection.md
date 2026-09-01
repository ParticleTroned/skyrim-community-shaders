# Periphery TAA camera motion correction

The FOV + TAA history lookup now uses the supplied per-eye motion vector
once. Those vectors contain both camera and object motion. Adding the
depth-derived camera displacement a second time sampled the wrong
previous-frame pixel whenever camera motion was present.

The correction applies to the shared DLSS/FSR periphery TAA shader in
both main-pass and submit-stage dispatch, with the FOV blend curve on or
off. SE/AE and native full-screen TAA do not use this custom VR pass.
Upscaling shader metadata and the checked-in minimum version advance to
2.6.1 so the fixed shader accompanies the corresponding DLL.

## Relationship to the 3.19.2 regression report

Source review compared `csx3.19.2`,
`d1980b8e151a814f1e6de5b7036da1b9ef21686e`, with main-VR
`43b4cb31c838127ddb923baeb20f19e924a3246f`.

The double displacement already existed in 3.19.2.
`658087a457e7e11ef77f3fc2ccfb2a2de4577322` changed the depth supplied to
that pass to typed R32_FLOAT data. Previously, boxed copies from the
engine depth-stencil resource violated the
[D3D11 copy contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion).
Supplying valid depth can activate camera reprojection where unusable
depth previously suppressed it. The tester's old depth contents and
installed build have not been captured, so this regression attribution
remains a hypothesis rather than a runtime finding.

The curve commit `c047c6dde14af22f187306111d9edcf96b8ff2b5` retains the
original blend calculation when disabled; it did not change motion
arithmetic. Open Shaders' peripheral temporal smoothing, inspected at
its depth-port commit `aeba846179bad44a7560a583a8f019f1e2cf0c42`, uses
motion vectors without a second depth-derived camera displacement.

## Quality, robustness and work performed

The change corrects the history address without replacing camera-inclusive
motion with camera-only motion, which would discard moving-object motion.
Typed depth remains available for closest-surface selection and existing
rejection heuristics. History clipping, accumulation weights, rejection
thresholds, reset handling, stereo ownership and FOV geometry are unchanged.
The depth-derived camera displacement is retained only for the existing
rejection relaxation; its lookup contribution is removed.

There are no additional texture-sampling instructions, resources,
dispatches, history buffers or synchronization points. The source removes
one float2 addition. Each existing path retains its sampling work, but
correcting an address can change history acceptance, memory locality and
thread divergence. In particular, an accepted history pixel executes
sampling that a rejected pixel skips. Unchanged instructions therefore do
not prove unchanged GPU time; measured performance neutrality is pending.
The correction does not lower sampling quality or disable TAA. Reverting
the typed-depth port would restore the invalid copy operation instead of
correcting the consumer's motion contract.

## Validation and limits

`python tests/periphery_taa_motion_test.py -v` passes all eleven tests.
The test extracts the NDC-to-motion return expression from
`MotionBlur::GetSSMotionVector` as well as the production `historyVelocity`
and `historyUV` expressions. Their evaluated addresses are compared with
independently projected scene positions. Cases cover both eye offsets and
peripheral sides, head rotation, camera translation on all three axes,
object-only motion, combined motion, an object following the camera, a
stationary scene, subpixel output offsets, unavailable camera depth,
asymmetric eye projections, full-eye UV units across render scales, and
preservation of attenuated encoder motion.

The expanded test against the unmodified main-VR shader fails 42
assertions. Six additional negative controls also fail assertions:
camera-only lookup, halved motion, reversed history lookup, reversed
producer X or Y sign, and a producer using packed-stereo X scaling.
No negative control relies on a parser or execution error to fail.
The initial eight-test version failed 36 baseline subcases; that earlier
result remains preserved in the investigation evidence.

The regression evaluates selected scalar expressions, not GPU execution,
matrix uploads, encoding/dilation, texture sampling, clipping, rejection
or blending. It is registered as `PeripheryTAAMotion` in the controller
test group. The adversarial runner and its logs are retained under
`build/analysis/taa-regression-20260927/adversarial` in the primary checkout.

Whitespace and metadata consistency checks pass. Clang-format 22.1.4
reports no replacements for the changed shader lines. Its full-file hook
also reformats unrelated existing HLSL, so that hook is skipped after
the scoped check to retain the focused source diff.

The initial implementation did not compile code, following the user's
instruction. The later requested production AIO build passed, with
DevBench and Tracy disabled and matching producer symbols included:

-   Compiled source: `e57acb6c7c3a2c1552f14ec90878dc7c11651d8e`.
-   Original TAA commit: `76a5d07d3a70a99a63de6f83c5d68713f9162cda`.
-   Build ID: `36a8a117dcd3c8167869582dcc1a92c380e72c4f90f829d503439b197565b14f`.
-   DLL SHA-256: `2522dd0e2c122b496c6cccf0ddb0446f5cf624fea66442631e9e71cba91026e5`.
-   Evidence: `build/periphery-taa-aio-20260927/archive-verification.json`.

Archive integrity, exact extraction, DLL/PDB identity, and canonical
FOMOD/cache validation passed. The standalone periphery shader was not
compiled during packaging; it compiles on demand in the game. The later
adversarial amendment changes tests and documentation only, preserving
the exact historical producer identity rather than claiming a rebuild.
No new compilation, in-game validation, physical-HMD qualification or
matched performance sampling was run during that review. No runtime
measurement or numbered ledger snapshot is claimed. Overall image
quality and performance relative to 3.19.2 remain unqualified.

## Adversarial review

The review retained the one-line production correction after tracing the
geometry producer, both encoders, per-eye copies, shared DLSS/FSR TAA
dispatch, and history read/write coordinates. Per-eye extraction does not
halve normalized X motion. DLSS dilation and seam/mask attenuation preserve
the vector convention, so adding camera displacement after them is also
incorrect. The rejection heuristics still use the existing camera term;
their tuning is outside this address correction and remains unqualified
by runtime evidence.

Two validation gaps were corrected: the regression originally supplied
idealized motion instead of exercising the producer's sign/scale, and the
documentation needed to distinguish unchanged sampling instructions from
unchanged executed GPU work. The test now reuses the repository's existing
function extractor, scopes expressions to the relevant functions, and
ignores comments. No new production helper, pass, resource, knob or vendor
branch was introduced. Shader/DLL metadata remains 2.6.1; SE/AE and native
TAA stay outside the affected dispatch. No additional rendering change
was warranted by the source review.

A runtime comparison must hold scene, settings and build inputs fixed
and cover head rotation, character translation and moving objects with
a still camera, for both DLSS + TAA and FSR + TAA, with both curve states.
It must also verify stable detail, disocclusion trails, stereo consistency
and GPU cost. A source-level correctness result is not a substitute for
that comparison.
