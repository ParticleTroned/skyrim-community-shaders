# Bounded Hi-Z refinement: 3 October 2026

Hybrid now tries complete finer depth-cell rectangles when its original
coarse test cannot prove occlusion. This corrects a reproduced rejection
failure caused by unrelated padded or clear pixels. No in-game rejection
rate or performance improvement is claimed for the changed shader yet.
Advanced and Standard Z remain defaults.

## Reproduced problem

The measured 1344 by 1492 eye image reduces to 336 by 373 valid base cells,
inside a 512 by 512 allocation. A guarded pixel rectangle spanning
approximately `[1000, 400]` through `[1323, 723]` is wholly inside the eye.
The original four-cell mip choice nevertheless covers far-valued padding.
It reports visible even when every real source pixel is solid nearer depth.

The [native-count campaign](vr-hybrid-culling-native-counts-2026-10-03.md)
observed approximately 62% native rejection versus 19% Hybrid rejection.
It did not identify matching persistent objects or quantify how much of
that gap comes from padding. Projected rectangular coverage and the box's
nearest depth remain more conservative than native rasterized OBB tests.

## Conservative refinement

The existing four-load coarse proof remains first. On an inconclusive
result, each finer mip recomputes its whole cell rectangle from the
original guarded base-cell bounds. Every cell is checked; partial grid
coverage can never establish occlusion. Recomputing bounds avoids
inheriting irrelevant cells from a coarser rectangle.

The fixed budget is 64 depth loads per eye, including the initial four
and all unsuccessful finer grids. A grid that would exceed the remaining
budget retains visibility. The padding regression needs 4 + 6 + 12 + 36 =
58 loads per eye. A separate fixture needs 4 + 16 + 48 = 68 and must stay
visible, catching an implementation that forgets to charge coarse loads.

Near/far clipping, viewport margins, masks, invalid depth, transform
validation, depth bias and both-eye agreement remain conservative. The
first eye still short-circuits the second when it retains visibility.
No host admission, motion tolerance, history validation, fallback,
resource ownership, user setting or diagnostic contract changed.
The standalone reversed-order test accepts far zero as inconclusive
evidence for refinement; far zero never proves occlusion. Runtime remains
Standard Z. No developer instrumentation was added to the shader.

Finer sampling adds bounded GPU work. A higher rejection rate alone is
insufficient: the complete frame must improve or remain neutral, and
motion/stereo/lifecycle checks still apply.

## Validation

-   Applied-source Release test target built successfully:
    `pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_hybrid_culling_shader_test --parallel 4`.
-   Applied-source `VRHybridCullingShader` CTest passed, 1/1 in 7.23 seconds.
    It compiles and executes both Standard and reversed test orderings.
-   The added tests fail against the original shader specifically on the
    padded interior rectangle, and pass against the refinement.
-   Coverage includes actual eye-layout padding, outside-footprint clear
    pixels, guarded masks/NaNs and clear pixels in either eye, thin bounds,
    viewport borders and the 68-load budget-exhaustion case.
-   An independent CPU oracle exhaustively checks source pixels for 441
    generated rectangle pairs. Any GPU occlusion must have complete source
    evidence; both retained and rejected outcomes are exercised.
-   Applied shader and test SHA-256 values match the independently checked
    candidate: `2542b6af4d848f16f34d83fd5d6d829db921611d344f8703123deb739040c5eb`
    and `9e843c4179047462fef08896e7970fd08669084c63157b116e0a2586422afb6d`.

Evidence is under worktree `build/astra-validation/hiz-refinement/`:
`applied-test-build.log`, `applied-ctest.log`, and the preserved negative
control/scratch compilation record `validation-20261003T221209986Z/`.
This is a behavior change, so the earlier Standard-Z bytecode-equivalence
results apply only to the preceding depth-order refactor.

## Verified developer AIO

The universal Release DLL and DevBench ON / Tracy OFF AIO built
successfully. All 369 extracted files matched staging by path, size and
SHA-256, including the refined bounds shader. Archive integrity and
DLL/PDB/manifest/shader/runtime payload checks passed. The build retained
the same source throughout, and both earlier AIO archives are unchanged.

| Identity              | Value                                                              |
| --------------------- | ------------------------------------------------------------------ |
| Compiled source       | `9a98398dfe24ce77ce24b6437eec248af82c73a0`, dirty                  |
| Compiled dirty digest | `a66617fe24773c32d860f8e4095acf50130a25714988045e33563ff8632c32d2` |
| Build ID              | `72499d97324239126ac1799cf901b866f68e3d700d4d754caeb53fd1a961b75b` |
| Archive bytes         | 90,844,134                                                         |
| Archive SHA-256       | `18eedc59402947dc984829a52eb7c922f3297055c0bbd24febf2444d14a6c6a4` |
| DLL bytes             | 29,625,856                                                         |
| DLL SHA-256           | `724dc60213f1812e4294a186ad7b7836cd8e8fecb44a90d2aee9d8d652dc43cc` |

The [refined AIO](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-refined-DevBench-72499d973242.7z)
and [verification receipt](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-refined-DevBench-72499d973242.receipt.json)
are ready for the user's installation. Exact build/delivery evidence is
under worktree
`build/astra-validation/hiz-refinement-rebuild-20261003T224530742Z/`.
The AIO has not been deployed or validated in game. Preserve this compiled
identity when later source commits add packaging or runtime evidence.

## Remaining runtime comparison

In-game tests remain paused until the other chat's complete build sequence
finishes. Preserve the installed native-count AIO as the original Hybrid
baseline, then test the new package with exact DLL identity, noon resets,
Off/Legacy/Advanced/Hybrid comparisons, separate count and timing windows,
and the compiler/menu/clock guards. Existing captures do not qualify this
new shader. The corrected viewer handoff and retained original motion
frames remain available for the next visual comparison.
