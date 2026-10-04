# Latest Hi-Z performance comparison

The tested guarded 2x2 implementation includes source refinement,
conservative direct-intersection proofs and far-depth clamping.
Advanced remains the default. The UI now lists Advanced, Hi-Z and Legacy,
with plain-language tooltips describing Hi-Z's higher performance cost.
The UI changes follow these measurements and do not change culling.

## Final comparison

WhiterunExterior01, SkyrimClearTU, fixed player position, DLSS Quality K,
1344x1492 render pixels and 2016x2240 display pixels per eye. Each phase
reset noon and settled for five seconds. Whole-frame timing disabled
telemetry, traversal/matched diagnostics and the CSX profiler. Compilation
was inactive and no external compiler activity was detected in accepted
frame windows.

| Measure                   |  Advanced | Hi-Z guarded 2x2 |
| ------------------------- | --------: | ---------------: |
| CPU frame mean            | 10.432 ms |        12.091 ms |
| GPU frame mean            |  8.056 ms |         9.188 ms |
| GPU culling mean          | 0.0742 ms |        0.5571 ms |
| Matched hidden candidates |   61.609% |          41.680% |

Whole-frame means give equal weight to three Advanced and two current
Hi-Z 20-second windows. Hi-Z is 15.91% slower on CPU and 14.06% slower on
GPU. Advanced GPU means ranged from 7.766 to 8.349 ms; Hi-Z ranged from
9.140 to 9.237 ms. Baseline drift prevents strong claims about small
whole-frame A/B differences. One separate baseline window with a backwards
SteamVR timestamp was excluded and repeated.

Two complete 300-frame captures per selected mode measured independent
GPU culling self-time scopes. Hi-Z bounds testing averages 0.4900 ms,
87.95% of total culling. All other scopes total 0.0671 ms. A same-build
polygon-baseline comparison measured 0.4967 ms bounds: the direct proof
reduces mean bounds time by approximately 1.35%, despite 18.75% fewer
polygon clips and 18.09% fewer clipping vertex-loop visits. This small
mean timing difference remains subject to capture variation.

## Matched culling and remaining costs

The restored same-build process supplied the missing diagnostic windows;
its timings are not mixed into the original performance measurements.
The selected matched window accepted 752 batches and 3,080,192 candidate
records with zero dropped/failed batches. One pending batch at freeze is
outside the accepted cohort. Counts are repeated candidate records, not
unique objects or draw calls. Matching checks batch, bounds, source and
view identity. Native visibility remains unchanged during shadow tests.

| Hi-Z reason for Advanced-only rejection | Share of those misses |
| --------------------------------------- | --------------------: |
| Wholly offscreen                        |               28.162% |
| Partial viewport crossing               |               21.207% |
| Expanded viewport guard                 |                0.381% |
| Finest depth unresolved                 |               29.926% |
| Nearest depth witness unresolved        |               17.617% |
| Eye-plane crossing                      |                2.140% |
| Depth-read budget                       |                0.569% |

Offscreen proxies need not imply useful extra drawing. Hi-Z-only hiding
accounts for 0.436% of all matched candidates; disagreement does not
establish visual correctness. No images were captured because performance
was not comparable. Motion/lifecycle and SE/AE runtime qualification remain
open. These results do not support performance parity with Advanced.

A source audit found that the new direct-test/proof/fallback and far-clamp
counter lanes omit first-eye contributions during shader aggregation.
Those counters are incomplete and excluded from stereo work conclusions.
GPU timings, matched outcomes, reasons and summed clipping work are
unaffected. This diagnostic defect remains unresolved.

## Evidence and validation

Measured implementation is `bc7af60a9753000ed7733c7f0a973cd612122cbb`.
The archive was built before that commit from the preserved working-tree
snapshot based on `b949dad8185a660e192c355e6b6fa02a2d512194`; it is not a
clean compile of bc7af60a9. Producer Build ID:
`e96ed746701e3ec33612a74705b8664a54671687bd364cf5500759e76044d47a`.

Raw normal-shader captures, frame windows and controller receipts remain
local in `build/astra-runtime/20261004T171741Z-hiz-intersection-ab`.
Completed matched/traversal evidence and the corrected analysis remain in
`build/astra-runtime/20261004T175542Z-hiz-intersection-diagnostics`.
Original receipts are unchanged.

The latest implementation passed twelve distinct focused tests and twelve
strict FXC Standard/reversed normal/diagnostic/A-B permutations. Actual
production compiler/forced-header syntax and preprocessing checks passed
with diagnostics and A/B controls absent. The universal Release DevBench
DLL and AIO verification passed. A separately linked production DLL was
not tested. The subsequent UI change rebuilt and passed
`VRDepthCullingSettingsUI` with the new labels/order and stable selections.
