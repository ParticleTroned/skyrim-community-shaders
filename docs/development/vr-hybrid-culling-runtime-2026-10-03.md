# PR 104 Hybrid culling: noon runtime evidence, 2026-10-03

The completed four-mode comparison fails the performance-neutral gate for
Hybrid in this Whiterun exterior scene. Both repeats are materially slower
than Advanced and Legacy. The limited static stereo review found no
obvious missing solid geometry in the sampled previews. Motion correctness,
disocclusion and lifecycle qualification remain open; Advanced stays the
default and Hybrid remains experimental.

## Measured identity and scope

All eight approximately 20-second fpsVR windows use the same developer AIO:

| Field               | Measured identity                                                               |
| ------------------- | ------------------------------------------------------------------------------- |
| Build ID            | `66b6efdee77bd3e03a565b8698cf395e332c44c4e62d0af479ad7a47ec152088`              |
| Compiled source     | `29ce68539c1fa489b241135d1a7629ccddf6a4e3`, `csx3.19.2-79-g29ce68539-dirty`     |
| Dirty digest        | `635f3da88d720a30e98b3c1ce8569da3b193afdece582e226ea6d4596a42569f`              |
| DLL SHA-256 / bytes | `fc4df6f883c5e9023ec27f90a1032751bd0e41020e9eaabf0aed573f81b8bfda` / 29,623,296 |
| Configuration       | Universal ALL Release, DevBench ON, Tracy OFF; Standard Z                       |
| Process             | SkyrimVR PID 15248, started `2026-10-03T20:08:36.7654641Z`                      |

The per-call runtime identity receipts match this physical AIO DLL and
producer. Packaging provenance remains in the
[AIO record](vr-hybrid-culling.md#devbench-aio-archive-2026-10-03).
Later source commits and the newly added native-result counters are not
the measured binary. No new DLL was deployed for this report.

All scene-start observations identify `WhiterunExterior01`,
`SkyrimClearTU`, and player position
`[16848.859375, -12267.0322265625, -4752.4814453125]`.
Noon was reset for each condition/phase and followed by settling; recorded
start hours range from 12.043215 to 12.058635. Simulation and physical HMD
pose were not frozen. Execution order was Advanced 1, Hybrid 1, Legacy 1,
Off 1, Hybrid 2, Advanced 2, Off 2, Legacy 2. Repeats remain separate;
earlier night/mixed-time captures are excluded from this comparison.
`advanced` filenames refer to the Balanced/Advanced preset. Off retained
that configured preset while its effective depth-culling backend was
disabled.

## CPU/GPU frame-time observations

Means are arithmetic sample means, not time-weighted. Samples are fpsVR
observations, not asserted unique presented frames. Quantiles use linear
interpolation at `(n - 1) * p` (type 7). All times below are milliseconds;
displayed means are rounded to six decimals. Full precision, sample
variance, standard deviation, timestamps and receipts are preserved in the
[timing CSV](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/noon-statistics-20261003T205441732Z.csv)
and [statistics JSON](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/noon-statistics-20261003T205441732Z.json).

| Condition  | Samples |  CPU mean | CPU p50 | CPU p95 | CPU p99 |  GPU mean | GPU p50 | GPU p95 | GPU p99 |
| ---------- | ------: | --------: | ------: | ------: | ------: | --------: | ------: | ------: | ------: |
| Off 1      |    1102 | 19.704537 |    19.6 |  27.695 |  29.999 | 11.637931 |    11.7 |  14.895 |    17.2 |
| Off 2      |    1075 | 19.360093 |    19.1 |    27.4 |    29.3 | 11.681488 |    11.9 |      14 |  17.026 |
| Legacy 1   |    1325 | 11.615019 |    11.4 |   17.38 |  22.752 |  8.608075 |     8.3 |    11.2 |    12.8 |
| Legacy 2   |    1449 | 10.922291 |    10.9 |      15 |  17.952 |  8.244168 |       8 |   10.42 |    12.5 |
| Advanced 1 |    1335 | 12.088390 |    11.7 |   19.43 |  24.466 |  8.722472 |     8.3 |   11.43 |    14.2 |
| Advanced 2 |    1399 | 11.310150 |    11.3 |    15.7 |      18 |  8.431094 |     8.1 |    10.9 |    13.3 |
| Hybrid 1   |    1051 | 18.817793 |    18.9 |    27.3 |   29.65 | 11.283064 |    11.1 |   14.85 |    17.4 |
| Hybrid 2   |    1181 | 16.524725 |    15.8 |    24.3 |    26.1 | 10.324555 |    10.6 |    12.7 |    15.6 |

| Hybrid minus baseline | CPU mean delta | CPU delta % | GPU mean delta | GPU delta % |
| --------------------- | -------------: | ----------: | -------------: | ----------: |
| Advanced, repeat 1    |      +6.729403 |  +55.668318 |      +2.560592 |  +29.356264 |
| Advanced, repeat 2    |      +5.214575 |  +46.105265 |      +1.893462 |  +22.458081 |
| Legacy, repeat 1      |      +7.202774 |  +62.012587 |      +2.674988 |  +31.075335 |
| Legacy, repeat 2      |      +5.602434 |  +51.293574 |      +2.080387 |  +25.234650 |
| Off, repeat 1         |      -0.886745 |   -4.500205 |      -0.354867 |   -3.049230 |
| Off, repeat 2         |      -2.835368 |  -14.645427 |      -1.356933 |  -11.616096 |

The Index session initialized at 120 Hz: its frame budget is
8.333333 ms. This refresh observation was not requeried for every window.
CPU and GPU intervals overlap and must not be added. Hybrid's mean CPU
cost is 225.814%/198.297% of budget and GPU cost 135.397%/123.895% in
repeats 1/2. All Hybrid CPU observations exceed budget; GPU exceedance is
88.963%/85.013%. All four conditions have CPU means above budget. Legacy 2
alone has a GPU mean below budget, while its GPU p95 remains 10.42 ms.

Hybrid's extra cost versus Advanced consumes another 62.575%-80.753% of
one CPU frame budget and 22.722%-30.727% of one GPU frame budget. Native
culling saves substantially more work than Hybrid here. Repeat variation,
nonrandom order, physical pose drift and animated scene contents prevent
these two repeats from proving a universal causal effect or confidence
interval. They nevertheless do not support a neutral-or-better claim in
this fixture.

## Backend execution and diagnosis boundary

All eight final counter snapshots report drained/frozen telemetry.
Advanced/Legacy report native execution; Off reports disabled execution.
Both Hybrid windows report the actual Hybrid backend with every submitted
batch accepted:

| Telemetry window   | Submitted/accepted batches | Tested records | Occluded records | Visible records |
| ------------------ | -------------------------: | -------------: | ---------------: | --------------: |
| Hybrid 1, window 5 |                6518 / 6518 |     26,697,728 |        3,996,627 |      22,701,101 |
| Hybrid 2, window 8 |                3205 / 3205 |     13,127,680 |        1,931,345 |      11,196,335 |

Both have zero fallback, invalidated and unreadable batches, dropped
source snapshots, pipeline builds and pyramid allocations in their warm
windows. About 15% of tested records are occluded. These are repeated batch
observations over the broader telemetry window, not unique objects or a
cohort restricted to the 20-second fpsVR interval. Exact counts are in
[Hybrid 1 frozen](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/noon-hybrid-1-frozen.json)
and [Hybrid 2 frozen](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/noon-hybrid-2-frozen.json).

The evidence rules out frequent recorded fallback or warm resource
recreation as explanations for this regression. Reduced occlusion efficacy
is a plausible source-based explanation: the Hybrid shader uses a
conservative projected rectangle, nearest bound depth and farthest depth
reduction; background, masks or holes within that rectangle can keep an
entire object visible. The native producer rasterizes OBB proxy coverage.
The measured DLL lacks native visible/occluded totals, so relative
rejection efficacy has not yet been measured. Metadata admission and
accepted history do not prove depth-content or cached-camera freshness.

### Follow-up investigation: padded coarse cells

Source review found a concrete loss of rejection efficacy, without
establishing its contribution to the measured regression. For the observed
1344 by 1492 pixels per eye, reduction by four gives 336 by 373 real base
cells, padded to 512 by 512. A guarded pixel rectangle
`[1000,400]..[1323,723]` lies wholly inside the eye. Its base-cell bounds
are `[250,100]..[330,180]`; the current two-by-two-cell selection reaches
mip seven. Selected X cell two covers source X 1024 through 1535,
including artificial padding beyond X 1343. The test therefore remains
visible even if solid occluding depth covers every real source pixel.

The relevant paths are `VRHybridCullingPolicy.h`'s power-of-two layout,
`BuildDepthCS.hlsl`'s far value outside the eye, `ReduceDepthCS.hlsl`'s
farthest-depth reduction and `TestBoundsCS.hlsl`'s single-mip rectangle
test. Padding is correctly fail-visible; the avoidable loss comes from
sampling a coarse cell whose footprint exceeds the guarded rectangle.
Existing `CoversAllReductionPixels` shader tests verify padding and every
source pixel's reduction contribution. `CoversEveryOverlappingCell` checks
that an interior clear pixel defeats occlusion. Neither tests recovery of
an occluded interior rectangle whose coarse cells overlap padding.

A bounded shader proposal is to retain the current coarse test, then try
successively finer complete cell rectangles when it cannot prove
occlusion. Recompute each level from the original base-cell bounds;
shifting the already-coarsened bounds back would lose the original extent.
Require every overlapping cell to pass the same nearest-bound/farthest-depth
test, in both eyes. An initial fixed budget of 64 depth loads per eye,
including the coarse test, bounds work; insufficient remaining budget,
invalid values or an unproven base level return visible. This avoids a
per-thread traversal stack. In the example, complete grids at mips seven
through four contain 4, 6, 12 and 36 cells: 58 loads would reach tiles that
exclude artificial padding. The budget is a proposal requiring shader and
performance validation, not an accepted tuning result.

Keep source admission, mask handling, clip/border guards, depth bias and
history checks unchanged. Do not change far padding into occluding depth.
This refinement still uses an axis-aligned rectangle and one nearest OBB
depth, so it cannot recover all native rasterization efficacy. Large or
clipped bounds remain conservative.

Before implementation acceptance, extend the existing WARP suite with this
non-power-of-two fixture in both depth-order test permutations; require a
solid real-eye occluder to reject the box. Add an in-footprint clear/masked
pixel in either eye that must retain it, an out-of-footprint hole that
finer cells can exclude, a thin rectangle, and budget exhaustion that must
remain visible. Use an independent CPU oracle over every covered base cell
for generated small rectangles, plus existing CPU layout/limit checks, to
verify complete coverage and the load bound. Retain near-plane, border,
stereo and invalid-input regressions. No shader change or new test was run
for this investigation; a fresh attributed build and repeated runtime
comparison are required before any efficacy or performance claim.

The later [refinement implementation](vr-hybrid-culling-refinement-2026-10-03.md)
now passes those shader regressions and has its own verified developer
AIO. The measurements in this report continue to describe the earlier
shader; the new refinement has not been tested in game.

The follow-up `depthCullingTemporal.nativeVisibility` diagnostic now counts
native CPU results before and after recovery, including explicit empty and
unreadable batches and excluding Hybrid-owned readbacks. It scans the
existing bounded array under one telemetry writer, adds no GPU readback,
and is entirely compiled out without DevBench. Two focused tests, ON
syntax checks and the OFF compiler/preprocessor audit passed. It is absent
from the original measured DLL. The separate new DLL/AIO now builds and
validates successfully. Its separately attributed
[native-count runtime report](vr-hybrid-culling-native-counts-2026-10-03.md)
records the later four-mode comparison after the user's installation and
restart. The original measurements below retain their original identity.
No culling safeguard or shader policy was weakened.

### Native-count AIO build, separate from measured runtime

| Field                   | New developer AIO                                                               |
| ----------------------- | ------------------------------------------------------------------------------- |
| Compiled source         | `c684ff32c9f75c97f6743fe2829ca7eb040525f2`, dirty                               |
| Dirty digest            | `92ea1d0bb80e669a84b01843dd8cb1ed4403257ddd298e60051c43cb01839c86`              |
| Build ID                | `ee0c10f34abe0a5a7197ce4f77436273355c80b1c72747deb7a9331d9a44d5c3`              |
| Archive bytes / SHA-256 | 90,846,788 / `c4b48a061544cb84a45c58189d51803368a00dce5078c8a18746e954618b9449` |
| DLL bytes / SHA-256     | 29,625,856 / `7ca6b10542a836e33b59033220e3c477bea97aaef02a272f34f4a391d4d7f1a3` |
| PDB SHA-256             | `44ae59c2ec313f6b45b76cba1c962d689f89de9b6ce73f2f96fe633b71e055e2`              |

The [new archive](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-native-counts-DevBench-ee0c10f34abe.7z)
and [verification receipt](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-native-counts-DevBench-ee0c10f34abe.receipt.json)
are distinct from the preserved original AIO. The universal DevBench ON,
Tracy OFF build linked, and all 369 extracted files (374,724,498 bytes)
matched staging by path, size and SHA-256. Archive integrity,
DLL/PDB/manifest, shaders and pinned runtime payloads passed validation.
Tracked diff, HEAD and the untracked diagnostic header/report remained
stable through the build. Both old archive copies retained their original
hash. The [build delivery record](D:/Coding/GitHub/skyrim-community-shaders/.tmp/worktrees/astra-hiz/build/astra-validation/native-count-rebuild-20261003T213736453Z/delivery-receipt.json)
retains exact provenance and log locations. The user installed/restarted
this AIO; its later measurements are in the linked native-count report.

## Static stereo review

The [independent review](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/noon-stereo-review-20261003T210516008464Z.json)
validated four completed sequences: eight stereo frames per condition,
64 original 2016x2240 PNGs, with zero reported dropped/failed/cancelled
captures. Visual inspection covered both eyes at ordinals 1 and 8 for all
four conditions: 16 viewed artifacts. It found no obvious missing solid
geometry, terrain holes, large grass-patch loss, missing/reversed eye or
inconsistent stereo occlusion in those samples.

The tool could not display the large original PNG payloads. Review used
quality-95 JPEG derivatives retaining native dimensions, displayed at
1497x1664; original PNGs remain untouched. Compression and display scaling
can hide small defects. Grass, clouds/fog, fire and NPC animation were not
synchronized, and physical HMD pose varied slightly. This is not
pixel-exact equivalence, quantitative color/fine-grass assessment, proof
against transient popping, or motion qualification. Near-plane/disocclusion,
moving occluders, cell/load transitions, failure replay and additional
scenes still require evidence.

## Bounded motion review

The original measured `66b6ef...` DLL completed final ROI bursts for all
four conditions: 160 consecutive engine frames per condition, with
8.7075% of each eye captured at native pixel resolution. Raw PNGs open
without resizing. All 640 original PNGs passed artifact verification;
most of each eye remains outside the captured region. The completed
[motion review](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/final-motion-review.json)
and [local review player](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/motion-review/index.html)
preserve the exact selection and comparison limits.

The planned route has 64 steps, with checkpoints numbered 0 through 64
at eight-step intervals. Capture coverage contains 5/9 observed checkpoints
for Advanced, 6/9 for Legacy, and 9/9 for Hybrid and Off. Fair four-mode
comparison is therefore limited to common poses 0, 8, 16, 24 and 32;
full-route parity must not be inferred from equal frame counts. See the
[Advanced coverage](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/final-motion-balanced-audit/route-coverage.json),
[Legacy coverage](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/final-motion-legacy-audit/route-coverage.json),
[Hybrid coverage](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/final-motion-hybrid-audit/route-coverage.json)
and [Off coverage](D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z/final-motion-off-audit/route-coverage.json).

No obvious culling holes or eye-specific geometry disappearance were
found in the reviewed common-pose samples and adjacent-frame candidate
neighborhoods. Visual inspection sampled eight native atlases at poses
16 and 32 plus consecutive candidate contact sheets; artifact verification
and numerical screening of all 159 adjacent pairs per condition do not
mean every pixel of every frame was visually reviewed. NPCs, vegetation,
clouds and other animation differed between conditions.

Across the Hybrid route, counters increased by 84 submitted batches,
20 accepted batches and 64 `view_changed` invalidations, with zero native
fallback. Changes in observed cache `cameraAdjust` were recorded. These
are execution observations, not proof of cache/content freshness or image
correctness. The route uses scripted free-camera/world-camera movement
with a physically tracked HMD; it does not replay physical head motion.
The 64 view-change rejections exercise fail-visible invalidation, so clean
samples do not prove sustained effective occlusion during rapid movement.
This bounded review does not qualify whole-image temporal behavior,
physical head motion, controlled moving occluders or lifecycle transitions.

## Comparison with the Reverse-Z donor evidence

[Open Shaders PR 818](https://github.com/alandtse/open-shaders/pull/818)
reports Guardian Stones null-HMD `ALL-TRACY` measurements in alternating
3.5-second windows, paced near 33 fps. Its reported culling-on/off frame
times are 30.80/35.32 ms in Standard Z and 30.93/36.27 ms in Reverse Z:
losing native culling costs about 4.5/5.3 ms. The author reports that an
unconverted `kMAIN_DOWNSAMPLE` made queries pass and erased culling
savings; converting that target restored them. The report includes one
static culling-on capture and explicitly leaves moving-view correctness
unchecked.

That evidence motivates checking both rejection efficacy and every depth
consumer's convention. It does not validate CSX Hybrid or motion safety.
Its scene, binary, null driver, pacing and Tracy timing differ from this
physical-Index fpsVR assay, so the millisecond values are not a shared
baseline. Reverse Z remains a later, separate workstream.

## Evidence retention and next gates

The complete local evidence directory is
`D:/Coding/GitHub/skyrim-community-shaders/build/astra-runtime/20261003T200331Z`.
The linked statistics and review retain paths and hashes for raw receipts,
samples, sequences and original images. Their SHA-256 values are:

| Artifact                                         | SHA-256                                                            |
| ------------------------------------------------ | ------------------------------------------------------------------ |
| `noon-statistics-20261003T205441732Z.csv`        | `9245f7934fe4b37e49504ab054fcb88498790e419d1710352a5560e96585a7db` |
| `noon-statistics-20261003T205441732Z.json`       | `629e816cef135d08697781a87deaf8351ae882d2e95256d83c8d82eb2dff1176` |
| `noon-stereo-review-20261003T210516008464Z.json` | `aa554c02661e53f64366b2126d1d44143fd265fdddb65c76ee64f75a0f0179a0` |
| `final-motion-review.json`                       | `e590af85060b41da803dae0a089e5b637df9cb95db78c32fc85528da9ac1654c` |

D1 remains open: preserve this measured regression, extend temporal and
lifecycle checks beyond the bounded motion review, and investigate the
efficacy gap confirmed by the later native-count campaign. Capture
artifact completion is separate from rendering
correctness and from performance acceptance. G1 PBR grass, G2 grass
optimization, H1 shared grass Hi-Z and R1 Reverse Z are not qualified by
these results. No render-scale transition or release qualification is
claimed.
