# VR RenderScale performance audit

## Scope, revisions, and acceptance rule

This document combines the architectural requirements in the repository-root
[CSX 3.15-VR RenderScale port notes](../../PL3.15-VR-render-scale-port.txt)
with a code-level audit of Main-VR RenderScale, its earlier implementations,
PR #31, and Open Shaders performance mode.

The inspected and revalidated reference points are:

-   Main-VR, fetched 2026-08-25:
    `02f61ff344924c8097031b7c6a33d039fd297fe7`.
-   [PR #31](https://github.com/ParticleTroned/skyrim-community-shaders/pull/31/)
    HAM code head audited after the correctness fixes and exact-mode test
    instrumentation: `726642c0dea538021d439ec8ddb02f0713649baa`.
-   PR #31 final experiment cleanup: `a408d340a`. The later correctness commits
    are `2d84cd651` (eye-local sample bounds) and `4074774a4` (robust
    submit-stage input sanitization). Commit `726642c0d` adds two DevBench-only
    exact-quality performance candidates; neither has measured results yet.
-   RC166: `b95958cc0c119625d8a7dd9657442c92122f240d`.
-   Open Shaders `dev`:
    `b3f955d8cafe7f918ba8538f0710b2286eadd5ea`.

The PR implementation is documented as code proposed for Main-VR, not as code
already present in the Main-VR baseline. PR #31 is directly based on the
Main-VR reference above. Outside its HAM implementation, DevBench bridge, CI,
and documentation changes, it inherits the current Main-VR RenderScale, FOV,
runtime-FSR, controller, and OpenVR presentation paths unchanged.

Performance conclusions are analytical unless explicitly called measured.
Pass count, pixel domain, resource traffic, synchronization, and shader work
can be established from code. Milliseconds cannot. Headset resolution, render
scale, GPU, driver, backend, scene, menu state, thermal state, and recovery
state can change the measured result.

The acceptance rule for this audit is deliberately strict:

> An optimization is not quality-preserving merely because it usually looks
> correct. It must retain the threshold, effective dilation, bounds and
> stereo-eye isolation, final-output guarantee, transition behavior, and
> post-load repair, or demonstrate equivalence with automated and visual
> evidence.

PR #31 contains a controlled single-scene measurement from before the two HAM
correctness fixes. It establishes a historical output-kernel timing improvement
and quantifies sampled divergence, but it does not establish the performance or
fidelity of the current PR head. Under this audit's strict rule, sparse nine-tap
remains a performance candidate until the repaired oracle and expanded quality
matrix pass. A product decision may accept a low sampled miss rate, but that is
an explicit quality-risk decision rather than proof of equivalence.

## Executive conclusions

1. PR #31's pre-fix capture demonstrates a real output-HAM-pass win in one
   controlled scene. Sparse measured `0.127607 ms` versus `0.212565 ms` for
   robust 5x5: `0.084958 ms`, or `39.97%`, less final-pass HAM time. That is
   about `0.76%` of an 11.11 ms 90 Hz frame budget, not a 39.97% whole-frame
   improvement and not a current-head total-HAM measurement.

2. Sparse nine-tap is not decision-equivalent to robust 5x5. The capture found
   2,640 false negatives and zero false positives. False negatives were
   `0.002570757%` of robust-clear samples and total mismatches were
   `0.000650664%` of evaluated samples. This is encouraging engineering
   evidence, but it confirms rather than eliminates the under-clear risk.

3. Commit `2d84cd651` fixes a shared correctness defect in the PR: production,
   probe, and DevBench reference samples now require both backing-texture and
   explicit eye-region membership. The historical capture predates that fix
   and still samples only one fixed 2x2 parity, so its percentages must be
   rebaselined against the corrected eye-local oracle.

4. Commit `4074774a4` closes the submit-stage temporal-input gap in the PR. After
   copying an eye, RenderScale now requires a robust eye-local scrub before
   DLSS/FSR or optional Periphery TAA. Failure returns to the original compositor
   submission rather than temporalizing unsanitized color. Main-VR base still
   lacks this submit-stage phase.

5. At the corrected PR head, non-foveated RenderScale performs one robust input
   and one final output scrub per eye: four HAM dispatches per stereo frame.
   Foveated RenderScale also clears inside
   `DispatchSubmitStageFoveatedVendorEye()`, producing six total dispatches per
   stereo frame, four of which are final-class. The early foveated clear remains
   dependency-conditional rather than automatically redundant.

6. PR #31 changes the HAM shader, diagnostics, and submit-stage input phase; it
   does not change the surrounding controller, stable resource validation,
   runtime-FSR bridge, FOV composition, mirror writeback, or OpenVR hook. Those
   wider findings apply equally to the PR head and current Main-VR base.

7. RC190 itself is not a RenderScale source-code regression. `RC189..RC190`
   changes no `Upscaling.cpp`, `Upscaling.h`, `FidelityFX.cpp`, or OpenVR hook
   code. Current-versus-RC190 regression reports remain plausible because
   heavier steady resource validation first appears in RC207 and the expanded
   controller snapshot in RC216. Matched profiling is required for causality.

8. The strongest universal CPU opportunities are to make resource validation
   generation-driven, split the hot presentation contract from cold metrics,
   reuse one compact snapshot through a compositor cycle, and make telemetry,
   memory sampling, and normally-zero recovery services state-proportional.
   These retain every authoritative revalidation at mutation boundaries.

9. Runtime FSR remains the largest backend-specific structural opportunity.
   Stereo currently performs twelve D3D11 copies, two D3D12 submissions, and
   two round trips comprising four directional cross-API handoffs. In foveated
   FSR, oversized center staging descriptions can also cause five display-sized
   input copies per eye even though only the center render rectangle is
   dispatched.

10. Three unchanged integration defects must be resolved before broad cache or
    lock optimization: duplicate-eye reuse lacks compositor-cycle/overlay
    ownership, normalized OpenVR bounds discard legal flip orientation, and
    the direct in-scene overlay does not restore all D3D11 state it changes.

11. Ignoring foveation, Open Shaders remains the likely faster stable pipeline
    at the same engine resolution and vendor preset, with medium confidence.
    This is not quality-equivalent: it solves a narrower HAM and presentation
    problem. Only a matched whole-frame capture can settle the actual gap.

12. The optimization order is therefore: repair shared correctness contracts;
    remove only dependency-proven redundant work; make stable safety checks
    state-proportional; reduce FSR/FOV traffic; then consider exact HAM kernel
    redesign. The old 50-80% estimate concerned a different exact reusable-mask
    proposal and was never measured. PR #31's `0.084958 ms` delta measures only
    sparse versus robust output scrubbing in its tested scene; the two claims
    are not directly comparable.

## PR #31 measured result and evidentiary limits

The PR description records one controlled HAM A/B result. The same stable
scene and latched RenderScale contract were used for each mode, counters were
reset before capture, 120 resolved frames were collected per mode, and the
fidelity pass was inactive while timing.

This capture predates `2d84cd651`, `4074774a4`, and the two exact candidates in
`726642c0d`. It therefore measures neither the corrected seam decisions nor the
current route's additional robust input scrub, and contains no reusable-mask or
tiled result. Treat the table as historical evidence for the old
sparse-versus-robust kernel substitution and rerun both pass-level and
whole-frame captures before making a current-head performance claim.

The PR summary does not report the exact GPU, driver, VR runtime, upscaling
backend, render/display dimensions, render scale, FOV route/state, HAM dispatch
count, or timer aggregation statistic. Those fields must accompany the next
capture before this result is generalized across configurations or releases.

| Path                  | Measured final HAM time |                        Delta | Quality result                                                                          |
| --------------------- | ----------------------: | ---------------------------: | --------------------------------------------------------------------------------------- |
| Robust depth 5x5      |           `0.212565 ms` |                     Baseline | Reference for this capture                                                              |
| Sparse depth nine-tap |           `0.127607 ms` |   `-0.084958 ms` (`-39.97%`) | 2,640 false negatives, zero false positives                                             |
| Removed runtime mesh  |           `0.119253 ms` | `-0.008354 ms` versus sparse | `6.237591715%` false negatives among robust-clear samples and 5,782,080 false positives |

The sparse result is meaningful: it is well outside a mere instruction-count
estimate and shows the observed pass-level saving in this capture.
The remaining limitations are equally important:

1. It measures HAM, not total GPU frame time or render-thread CPU time.
2. It covers one scene, runtime/backend combination, resolution, FOV state,
   driver state, and thermal history.
3. Sparse is a strict subset of robust, so zero false positives is expected
   when mapping and bounds are shared. False negatives are the useful signal.
4. The quality dispatch samples `dispatchID * 2 + 1`, one fixed 2x2 parity.
   Rotate all four phases or use exhaustive synthetic input before treating the
   grid as representative of odd sizes and every render/display ratio.
5. Both historical candidate and reference shared the packed-stereo seam-bounds
   defect. Their recorded agreement could not detect that common wrong decision;
   the current shader fixes it but has not yet been remeasured.
6. Visual head-motion sequences remain necessary because a small spatial miss
   rate can be amplified by temporal reconstruction at a high-contrast edge.

The mesh result justifies its removal. It bought only `0.008354 ms` beyond
sparse, roughly `0.075%` of a 90 Hz frame budget, while introducing failures in
both directions. It should not be restored without a new conservative proof.

## Correctness and integrity prerequisites

These findings were discovered against the shared Main-VR/PR pipeline. C1 and
C2 are resolved in the current PR head but remain absent from the inspected
Main-VR base. C3-C6 remain open in both. They stay in fix-before-optimization
order because performance work must not make an ambiguous presentation contract
faster.

### C1. Constrain HAM neighborhoods to the current eye

Severity: high correctness risk; visual severity is not yet measured.

Status: resolved in PR #31 by `2d84cd651`; still present in the inspected
Main-VR base and in the historical capture.

The shader now shares `IsDepthSampleInRegion()` across sparse, robust, probe,
and quality-audit neighborhoods. Every read must be inside both the backing
texture and `[DepthOffset, DepthOffset + DepthExtent)`, preventing a radius-two
sample from entering the peer eye. Discard and rerun the historical fidelity
ratios because the old candidate and oracle shared the seam defect.

### C2. Sanitize temporal input before vendor and Periphery TAA history

Severity: critical quality-contract gap.

Status: resolved in PR #31 by `4074774a4`; still present in the inspected
Main-VR base.

`ShouldClearHMDMaskInPhase()` now admits `SubmitStageInput` while presentation
RenderScale owns the frame. Immediately after copying an eye, the PR requires a
robust eye-local scrub before DLSS/FSR or optional Periphery TAA. Missing depth,
ownership deferral, shader/resource failure, or dispatch failure returns false
to the original compositor submission. A future reusable exact mask may remove
the repeated classification, but only with compositor-cycle and source-depth
generation ownership.

### C3. Give duplicate-eye reuse compositor-cycle and overlay ownership

Severity: high correctness risk.

`SubmitVRUpscaledFrame()` reuses a ready eye when frame, source, dimensions,
method, generation, and content options match. The key omits the compositor
cycle token. The OpenVR hook then alpha-composites the in-scene overlay into
that returned texture in place. A duplicate submit can reuse an already
composited image and blend the overlay twice; two compositor cycles in one game
frame can also reuse stale eye content.

Include compositor-cycle and overlay-generation ownership, or keep pristine
vendor output separate from the mutable presentation image. Performance cache
hits are valid only when the complete post-processing identity matches.

### C4. Preserve reversed OpenVR bounds

Severity: high compatibility risk.

`TryGetNormalizedVRBounds()` sorts U and V endpoints, discarding horizontal or
vertical flip orientation. Successful RenderScale output then uses canonical
`{0,0,1,1}` bounds. Legal reversed input bounds can therefore be submitted in
the wrong orientation.

Retain orientation bits while resolving the source rectangle and reapply them
to output bounds. If a layout cannot be represented exactly, fall back to the
original submission rather than silently changing it.

### C5. Restore every D3D11 state changed by direct overlay work

Severity: medium-high integration risk.

`RenderInSceneOverlay()` saves render targets, viewport, rasterizer, blend, and
depth state, but changes vertex/pixel shaders, constant buffers, vertex/index
buffers, input layout, topology, SRVs, and samplers without restoring them. A
previous null rasterizer state is also not explicitly restored.
`FillMenuCameraMotionVectors()` has the smaller related defect of clearing the
index buffer without saving and restoring it.

Use complete scoped guards for the bindings each pass mutates. Measure the
guard overhead, but do not depend on a later pass accidentally overwriting the
leaked state.

### C6. Keep format and retirement contracts explicit

Severity: medium robustness risk.

The optimized presentation path assumes a narrow color/resource contract while
accepting generic DirectX submission metadata, and rapid relatches batch retired
generations. Standard Skyrim inputs are likely inside the intended envelope,
but unsupported format/color-space combinations should fail back explicitly,
and each retired generation should retain a proven compositor tail plus fence.

The positive baseline is strong: dimensions are clamped and latched, epochs and
contract generations separate ownership, invalid source regions fail back,
resource publication is staged, stereo acceptance is proven, and device-loss
and retirement paths are defensive. The recommendations below relocate or
deduplicate checks; they do not weaken these invariants.

## Current Main-VR stable-path re-audit

PR #31 is directly based on `02f61ff34`. A zero-context diff of the PR's source
changes shows HAM/resource additions around `ClearHMDMask` and DevBench only;
the following wider hot paths are inherited unchanged.

### Stable CPU and synchronization costs

1. **Full resource validation on the first eligible call each frame.**
   `EnsureResourcesCurrent()` caches completion only within one frame. Its
   stable branch still evaluates FSR terminal lifecycle state and calls
   `AreCommonVendorTexturesReady()`. Compatibility checks perform `GetDevice`,
   COM identity work, and `GetDesc` for common textures. Full validation is
   valuable at publication and device/generation changes, but repeated COM and
   descriptor queries are not the only way to preserve that proof.

2. **Large transition snapshots copied too often.**
   `GetVRRenderScaleTransitionSnapshot()` copies the complete controller under
   a mutex, including a 50-record metrics ring, lifecycle, recovery, fidelity,
   and presentation diagnostics. It then derives owner and phase fields with
   additional atomic reads. Preparation, classification, explicit hook checks,
   submit-stage work, FSR lifecycle checks, and accepted-presentation recording
   can each request the structure during one stereo cycle.

3. **Per-eye telemetry mutates the main controller.**
   Fidelity and presentation observations lock and rewrite controller state for
   successful eyes. Presentation recording then takes another full snapshot to
   evaluate stereo promotion even after the controller is already Active.

4. **Stable memory polling remains render-thread work.**
   `ConfigureUpscaling()` calls the sampler each eligible frame. On 29 of 30
   frames the interval fast path still locks the controller; on the sampling
   frame it performs process/system commit and DXGI video-memory queries.

5. **Normally-zero recovery paths lock before checking their gate.**
   Native-restore classification takes the recreate-queue and controller locks
   before discovering that the guard epoch is zero. Other promotion/recovery
   services follow the same pattern with normally empty candidates.

6. **The presentation commit lock spans the expensive transaction.**
   The OpenVR hook holds the recursive recreate-queue lock across policy,
   D3D/vendor work, optional overlay work, and the selected OpenVR call. It
   protects lifetime and epoch coherence, but broadens contention and tail
   latency.

7. **Repeated policy queries reduce DRYness and coherence.**
   Runtime-plan refresh queries known menu context twice, and pending-profile,
   guard, transition, and ownership helpers repeat locks and copies inside one
   logical cycle. A compact event-generation-backed contract would be both
   drier and easier to validate.

### FOV, HAM, and GPU traffic

1. **Two foveated final-class HAM calls are confirmed; redundancy is
   dependency-conditional.**
   `DispatchSubmitStageFoveatedVendorEye()` clears
   `SubmitStageFoveatedOutput`; common finalization later clears
   `SubmitStageOutput`. The default DLSS RCAS sharpener can consume the first
   sanitized result, while menu final composition can write before the later
   clear. Keep both when they protect distinct dependency edges. Where the
   graph proves no intervening consumer or writer, eliminate or exactly fuse
   the earlier classification and retain one authoritative post-last-writer
   clear.

2. **Region planning and Periphery TAA tile-list caching are already good.**
   Foveated encode uses the cached `FoveatedRegionPlan`, and tile lists are
   keyed by dimensions, padding, quantized scales, and eye offsets. CPU tiles
   are rebuilt and uploaded only when that key changes. Do not propose a new
   per-frame tile rebuild.

3. **Center staging still copies five resources per eye.**
   Color, depth, motion vectors, reactive mask, and transparency mask are copied
   into center textures before vendor evaluation. Direct subrect use is only
   valid where the DLSS/FSR API contract, pinhole offsets, motion-vector scale,
   formats, resource lifetimes, and padding requirements are proven.

4. **Runtime-FSR foveation can copy allocation size instead of active size.**
   FSR center inputs are allocated at the maximum of center-input and full
   output-eye dimensions. `FidelityFX.cpp` derives its five shared-input copy
   rectangles from resource descriptions even though the dispatch declares the
   smaller active render dimensions. This can turn a center dispatch into five
   display-sized copies per eye.

5. **The runtime-FSR bridge serializes eyes.**
   Each eye copies five inputs, signals D3D11, waits/dispatches/signals D3D12,
   waits on D3D11, and copies one output. Stereo therefore means twelve copies,
   two command-list submissions, and two round trips comprising four
   directional cross-API handoffs.

6. **Periphery TAA histories are full display-eye allocations.**
   Two slots for each eye are retained for color, velocity, and lock history.
   Cropping to a padded outer region or stable tile atlas could reduce residency
   and bandwidth, but only after reprojection, Catmull-Rom taps, eye-motion, and
   mask-motion padding are proven.

7. **Foveated fallback is expensive but exceptional.**
   A failed foveated dispatch can force full-eye re-encode/replay, request a
   history reset, and arm retry backoff. Preserve this fail-safe path; prevent
   predictable failures through admission validation and measure fallback rate
   rather than charging it as ordinary-frame cost.

8. **Desktop-mirror writeback is conditional, not a leading universal cost.**
   A compatible full-size source receives two eye copies after both outputs are
   ready, while only the incompatible fallback observes the stabilization
   setting. Normal reduced RenderScale sources often do not meet the compatible
   condition. Gate direct writeback on a real mirror/capture consumer, but rank
   it from profiler evidence rather than assuming every frame pays it.

Foveated vendor dispatch and Periphery TAA are disabled in code defaults, so
their savings apply only to configurations that enable them. They remain
important because the user-visible FOV path can otherwise erase its vendor
pixel saving with composition, copy, history, and potential multi-pass HAM
overhead.

## RC190 attribution

RC190 is `53dff9bc8176edf729a7b614eb0c8c3dccb48a31`, titled
`feat(mgo-presets): adopt CSX Adaptive Balance settings`. `RC189..RC190` has no
changes to `Upscaling.cpp`, `Upscaling.h`, `FidelityFX.cpp`, or
`InSceneOverlay.cpp`; the inspected old/new balanced Upscaling settings are
also identical. RC190 therefore cannot itself be the source-code RenderScale
regression.

Current versus RC190 can still be slower:

-   `314ccdff54`, first tagged RC207, introduced the stronger common-texture
    device/descriptor validation now reached by the stable resource check.
-   `a9c75a765`, RC216, expanded the transition snapshot accessor with physical
    and presentation ownership/phase derivation.
-   Both are credible CPU contributors, but source inspection proves only that
    extra work exists, not how much frame time it consumes.
-   The broad presentation lock, periodic memory sampling, telemetry, runtime-FSR
    bridge, and foveated two-call HAM route predate RC190. They are optimization
    opportunities, not explanations for a post-RC190 regression by themselves.

Use matched `RC206` versus `RC207` and `RC215` versus `RC216` captures to isolate
these two boundaries. Keep save, camera/head motion, runtime, backend, render
scale, resolved FOV rectangle, overlays, logging, DevBench state, and preset
values identical. Default-to-default or preset-name comparisons are not
controlled evidence.

## Architectural invariants from the port

The root port note sets constraints that performance work must retain:

-   RenderScale and final submission form one pipeline.
-   When presentation RenderScale is inactive, submission falls through to the
    original compositor path.
-   There is no second legacy submit-replacement path.
-   One runtime plan owns display, engine-render, and final-submit dimensions.
-   Resources are recreated only when the active plan changes.
-   Loading and menu-sized presentation may use the lightweight stretch stage.
-   HMD and controller overlays retain separate dimensions and SRVs so menu text
    remains sharp and stable.
-   Underwater and projected/worldspace mask repair remains present.
-   Normal logging remains bounded.
-   VRS, black-square, fade, BootExit, FADERUI, and old dynamic-resolution
    experiments remain outside this pipeline.

These are not incidental checks to delete for speed. They define the supported
state machine and avoid duplicate ownership, stale resources, invalid
submissions, and post-load corruption.

## Pipeline maps

### PR #31 RenderScale, stable vendor frame

The stable path is conceptually:

```text
packed stereo engine render
  -> per-eye vendor input copy
  -> robust eye-local input HAM scrub (fail to original submission if unavailable)
  -> DLSS/DLAA, host FSR, or runtime FSR
  -> optional foveated center/periphery composition
  -> final display-sized eye texture
  -> one HAM scrub for a non-foveated eye
     or currently two final-class scrubs for a foveated eye
  -> compositor submission
```

Important ownership details:

-   `ShouldClearHMDMaskInPhase()` allows submit-stage clearing only while
    presentation upscaling is active and the runtime plan owner is
    `VRRenderScaleMode`.
-   The same function disables input/output clearing in the ordinary vendor path
    while RenderScale owns the presentation.
-   The corrected PR has four total HAM dispatches per stable non-foveated
    stereo frame: one robust input and one final output dispatch per eye.
-   Stable foveated RenderScale has six total: the two robust input dispatches
    plus four final-class dispatches because both `SubmitStageFoveatedOutput`
    and `SubmitStageOutput` execute per eye.
-   Phase ownership prevents ordinary vendor input/output clears from duplicating
    the submit-stage route, but it does not deduplicate those two submit phases.
-   A forced robust dispatch is used for submit-stage temporal input, verified
    repair, and binding validation. The ordinary final path remains sparse.
-   The inspected Main-VR base lacks `SubmitStageInput`, so its old totals remain
    two non-foveated or four foveated final-class dispatches per stereo frame.

### Main-VR ordinary vendor path without presentation RenderScale

The ordinary vendor route is:

```text
per-eye vendor input
  -> input HAM scrub
  -> vendor reconstruction
  -> output HAM scrub
  -> original presentation route
```

It can execute four HAM dispatches per stereo frame: input and output for each
eye. This must not be confused with the stable RenderScale count.

### Open Shaders performance mode

The inspected Open Shaders route is conceptually:

```text
packed stereo engine render
  -> exact-zero input HAM clearing
  -> vendor reconstruction
  -> display-domain test texture
  -> copy to refraction temporary
  -> 3x3 box downscale to render-domain kMAIN
  -> underwater-mask repair
  -> resource swaps and conditional refraction/menu replays
  -> compositor path
```

Open Shaders avoids Main-VR's final thresholded display-domain HAM guarantee,
but its performance mode is not a single vendor call with no surrounding work.

## Code map

The relevant Main-VR/PR files are:

| Concern                                                   | Implementation                                                                                                                        |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Runtime plan, backend routing, HAM ownership and dispatch | [Upscaling.cpp](../../src/Features/Upscaling.cpp)                                                                                     |
| HAM modes, resources, counters, and state                 | [Upscaling.h](../../src/Features/Upscaling.h)                                                                                         |
| Sparse, robust, probe, and fidelity-audit shaders         | [ClearHMDMaskCS.hlsl](../../features/Upscaling/Shaders/Upscaling/ClearHMDMaskCS.hlsl)                                                 |
| DevBench control surface                                  | [VRRenderScaleDevBenchBridge.cpp](../../src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp)                                       |
| Measurement contract                                      | [vr-hmd-mask-devbench.md](../development/vr-hmd-mask-devbench.md)                                                                     |
| Foveated region sizing                                    | [FoveatedRegionPlan.h](../../src/Features/Upscaling/FoveatedRegionPlan.h)                                                             |
| Periphery reconstruction                                  | [FoveatedPeripheryCS.hlsl](../../features/Upscaling/Shaders/Upscaling/FoveatedPeripheryCS.hlsl)                                       |
| Periphery temporal reconstruction                         | [PeripheryTAACS.hlsl](../../features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl)                                                 |
| Runtime FSR bridge                                        | [FidelityFX.cpp](../../src/Features/Upscaling/FidelityFX.cpp) and [DX12SwapChain.cpp](../../src/Features/Upscaling/DX12SwapChain.cpp) |
| DLSS/DLAA integration                                     | [Streamline.cpp](../../src/Features/Upscaling/Streamline.cpp)                                                                         |

## Final PR #31 HAM implementation

### Shared predicate and coordinate contract

`ClearHMDMaskCS.hlsl` uses:

-   hidden threshold `depth <= 1e-6`;
-   reversed-Z interpretation where the far/unrendered value is near zero;
-   a nominal depth-space dilation radius of two pixels;
-   eye-local color and depth offsets;
-   integer color-to-depth coordinate scaling;
-   depth and color resource bounds checks;
-   an 8x8 dispatch group for production kernels.

The color texture may be display-sized while depth remains render-sized.
Every output thread maps its local color coordinate to a depth coordinate,
then tests a neighborhood around that depth position.

Commit `2d84cd651` completes the intended eye-isolation contract. Neighborhood
samples are checked against both the full depth texture and the eye-local
`[DepthOffset, DepthOffset + DepthSize)` rectangle. Production, probe, and
DevBench reference paths use the same predicate, so radius-two samples cannot
enter the peer eye. The historical fidelity capture predates this correction
and is not evidence for the repaired oracle.

### `sparse_depth_9tap`

The production shader evaluates:

```text
(-2,-2) ( 0,-2) (+2,-2)
(-2, 0) ( 0, 0) (+2, 0)
(-2,+2) ( 0,+2) (+2,+2)
```

It retains the threshold, radius-two reach, mapping, bounds, and final color
clear. It does not retain full radius-two coverage. In particular, all
positions involving +/-1 and the non-corner/non-axis positions of the 5x5
footprint are omitted.

The sparse code accumulates all nine comparisons. The robust code can stop
loading once a hidden sample is found. Consequently, nine versus twenty-five
is a maximum-instruction comparison, not a universal 2.78x shader-speed claim.

For a fixed mapped depth coordinate `p`:

```text
R(p) = OR hidden(p + q), q in every coordinate of [-2,+2]^2
S(p) = OR hidden(p + q), q in {-2,0,+2} x {-2,0,+2}
```

Because the sparse set is a subset:

-   `S(p) => R(p)`: a sparse positive is also a robust positive;
-   `R(p) && !S(p)` is possible: a sparse false negative;
-   `S(p) && !R(p)` is impossible if texture, mapping, threshold, and bounds are
    identical.

The quality risk is therefore under-clearing, which may leave hidden-area
color available to temporal reconstruction at a moving edge.

### `robust_depth_5x5`

The reference evaluates all valid offsets in `[-2,+2]^2`, up to 25 depth
loads per color pixel. It early-outs its useful loads once it finds a hidden
sample. It is compiled with `HMD_MASK_ROBUST_DEPTH_5X5`.

The robust shader is used for:

-   controlled DevBench A/B comparison;
-   verified post-load candidate repair;
-   any call that explicitly forces the reference;
-   binding-verification work.

`EnsureHMDMaskClearResources()` deliberately treats the sparse shader and
constant buffer as the production requirement. Failure to compile the
optional robust shader does not disable sparse clearing, although robust
diagnostics and verified repair then cannot run.

### Dispatch and state safety

`DispatchHMDMaskClear()`:

1. rejects non-VR, null views, zero dimensions, missing context, or missing
   production resources;
2. selects robust for forced, verified, or DevBench-reference requests;
3. rejects a robust request when its optional shader is unavailable;
4. binds SRV, UAV, constant buffer, and compute shader;
5. optionally reads bindings back and verifies object identity;
6. dispatches ceil(width/8) by ceil(height/8);
7. unbinds SRV, UAV, constant buffer, and shader through scoped cleanup;
8. records profiler/counter data only when the DevBench bridge is compiled.

The normal dispatch does not perform expensive binding readback. Verified
post-load repair does, because it is validating a recovered compositor
candidate and preserving/restoring surrounding compute state.

### Stable versus exceptional work

Stable-frame GPU work:

-   sparse final scrub once per submitted eye under non-foveated RenderScale;
-   sparse final scrub twice per submitted eye under the current foveated
    RenderScale route; or
-   sparse input and output scrub per eye under the ordinary vendor route.

Exceptional work:

-   robust verified post-load repair;
-   resource/shader creation;
-   device-loss and transition cleanup;
-   probe capture;
-   fidelity audit and readback;
-   temporal-history reset after a DevBench mode change.

The exceptional work is important for robustness but must not be charged to
every stable frame in an analytical comparison.

## PR #31 evolution

PR #31 is a sequence of experiments, not one unchanged implementation:

1. `988e2f3c3`, `perf(upscaling): add VR HMD mask A/B paths`, added the
   original A/B controls and measurement framework. It included robust,
   historical exact-zero, and reduced-resolution-mask directions.
2. `bcc16c895` added legacy miss classification.
3. `b6c5a293b` replaced the first UI controls.
4. `b44e53afe` changed the performance candidate to the sparse nine-tap
   depth scrub.
5. `f26f58df0` added a persistent runtime OpenVR hidden-area mesh mask.
6. `c2249f2dd` removed the failed mesh experiment, UI/config state, supporting
   resources, shader paths, counters, and fallback dispatches. It retained
   sparse nine-tap as production, 5x5 as reference/repair, and DevBench-only
   A/B control.
7. `a408d340a` finalized the experiment cleanup. Its HAM code is equivalent to
   `c2249f2dd`; it additionally carried the original audit document.
8. `2d84cd651` constrained production and diagnostic HAM neighborhoods to the
   current eye.
9. `4074774a4` added a required robust submit-stage input scrub before temporal
   consumers, with original-submission fallback on failure.
10. `726642c0d` added `exact_reusable_mask` and `tiled_exact_5x5` as
    DevBench-selectable experiments with separate timers, counters, robust
    fallbacks, and fidelity-capture paths.

The final state must be read from `726642c0d`, not the original PR title, the
historical measurement commit, or an intermediate experiment.

### Removed exact-zero path

The exact-zero path was useful for reproducing the earlier
`8de526eb5` behavior and measuring a lower bound. It was not quality
equivalent:

-   exact `depth == 0.0` misses small non-zero invalid depths;
-   one center sample has no dilation;
-   it does not protect the neighborhood consumed by reconstruction.

Its removal from the final mode list is consistent with this audit's
quality-first acceptance rule.

### Removed input-resolution mask and its depth-domain successor

The early PR built a thresholded/dilated lower-resolution mask, used it before
temporal reconstruction, and reduced final work toward one mask lookup. That
architecture remains promising, but the particular implementation is not in
the cleaned-up PR state and must not be reported as delivered.

For equivalence, a replacement must prove that each final output pixel reads
the exact robust decision associated with the same mapped input-depth
coordinate. Bilinear filtering of a binary mask, insufficient conservative
coverage, cross-eye dilation, or rounding differences can change edge
classification.

Commit `726642c0d` revisits the architecture without restoring that
implementation unchanged. Its `exact_reusable_mask` is depth-domain, one mask
texel per eye-local depth coordinate. Input and final color independently use
the direct integer color-to-depth mapping and point-read that shared decision,
avoiding composed input-resolution rounding. It remains a DevBench experiment,
not a promoted production result, until fidelity, lifecycle, and total-cost
tests pass.

### Removed persistent runtime mesh mask

The intermediate implementation rasterized the OpenVR hidden-area mesh into a
persistent mask and attempted to reuse it. The final commit describes this
experiment as failed and removes it completely.

A compositor mesh can be cheap to sample and may describe the nominal hidden
region well, but it is not automatically equivalent to the depth-derived
predicate. Risks include:

-   runtime mesh availability and lifecycle;
-   projection/viewport changes and regeneration timing;
-   conservative rasterization and dilation at eye boundaries;
-   disagreement between nominal compositor geometry and actual depth holes;
-   initialization or regeneration hitches;
-   stale mask use during relatch, reset, or post-load recovery.

The commit history does not establish one single measured failure cause, so
this audit does not invent one. Reintroducing the approach would require a
separate proof against the depth reference across runtimes and transitions.

## HAM performance model

Let:

-   `Pout` be display-domain pixels processed across both eyes;
-   `Pin` be input-depth-domain pixels across both eyes;
-   `E[K]` be the average valid depth loads in the early-exiting robust kernel;
-   `Cd` be effective cost per depth lookup after cache effects;
-   `Cbase` be mapping, bounds, dispatch, and branch cost per output pixel.

Approximate classification work is:

```text
robust final: Pout * (Cbase + E[K] * Cd), 1 <= E[K] <= 25
sparse final: Pout * (Cbase + 9 * Cd)
OS input:     Pin  * (smaller base + 1 exact-zero lookup)
```

For visible pixels with no hidden neighbor, robust executes all 25 tests and
sparse removes 64% of the lookup instructions. For hidden-area pixels robust
may exit early, so sparse can execute more tests than robust at some pixels.
Texture cache locality also makes saved instructions cheaper than saved
uncached memory operations.

At identical FOV mask size, the number of pixels dispatched does not shrink:
both Main kernels cover the full eye rectangle and decide which color pixels
to clear. Mask shape changes the number of writes and robust early exits, not
the dispatch rectangle.

Source-only analysis cannot convert this into a reliable millisecond saving.
PR #31's controlled capture measured `0.084958 ms` less final HAM time in its
scene; the profiler timers remain authoritative for every other configuration.

## Backend-specific pathways

### No vendor upscaler / simple stretch

When no vendor temporal upscaler owns the frame, the HAM policy can be
different because there is no vendor history to contaminate. The lightweight
submit-stage stretch is primarily presentation work and should remain the
loading/menu fallback required by the port.

Avoid enabling vendor-oriented HAM passes merely because a render-scale
texture exists. `ShouldClearHMDMaskInPhase()` correctly gates on a vendor
method and the current presentation owner.

### TAA and conventional AA

Main-VR's FOV + TAA path is not a simple crop stretch. It uses depth, motion,
reactive/transparency information, temporal histories, neighborhood
statistics, reconstruction, disocclusion and lock logic, and center/periphery
composition. It does more work to retain temporal stability and edge quality.

Conventional AA without vendor reconstruction avoids vendor SDK cost, but its
quality and history requirements differ. HAM savings must be measured on the
actual selected route rather than extrapolated from DLSS or FSR.

### DLSS and DLAA

DLSS consumes lower-resolution input and reconstructs display output. DLAA
uses the same temporal family near native input/output dimensions. Therefore:

-   DLSS makes an input-domain mask relatively cheap compared with a
    display-domain scrub;
-   DLAA reduces that resolution advantage because `Pin` approaches `Pout`;
-   both still benefit from sanitizing hidden color before temporal history;
-   final-output protection remains the robustness backstop.

The Streamline integration and its required feature/resource contracts should
not be weakened to save CPU checks. Stable-plan caching and resource reuse are
safer targets.

### Host FSR

Host FSR uses the in-process D3D11 FidelityFX path. It does not pay the
D3D11/D3D12 shared-resource bridge used by runtime FSR3/4. HAM input/output
domain reasoning is otherwise similar: input sanitization protects temporal
history and final clearing protects presentation.

### Runtime FSR3 and FSR4

The runtime path bridges D3D11 game resources to D3D12 FidelityFX resources.
In both inspected projects, each eye performs:

-   five D3D11-to-D3D12 input copies: color, depth, motion, reactive, and
    transparency;
-   one D3D12-to-D3D11 output copy;
-   six begin and six end transitions;
-   fence synchronization between APIs;
-   one D3D12 command-list submission.

Stereo therefore means twelve copies, twenty-four barriers, and two
submissions. Open Shaders supporting FSR4 does not avoid these requirements;
it uses the same class of runtime bridge.

Optimization here must preserve resource ownership, state transitions, and
fence ordering. Coalescing submissions or removing copies is acceptable only
if the runtime API and lifetime model prove it safe. Incorrect cross-API
synchronization can produce intermittent corruption or device removal.

## Main-VR versus Open Shaders

### Ignoring foveation

At the same engine resolution, headset resolution, vendor preset, and
hypothetical modlist:

-   HAM alone: Open Shaders is analytically faster. It uses one input-domain
    exact-zero lookup instead of the corrected PR's robust input scrub plus
    display-domain sparse final scrub.
-   Vendor reconstruction: broadly comparable when the same runtime and preset
    are selected; runtime FSR3/4 bridge work exists in both.
-   Surrounding pipeline: Open Shaders adds display copy, 3x3 downscale,
    underwater repair, swaps, and conditional replay. Main-VR adds stronger
    presentation ownership, overlay separation, transitions, and input/final HAM
    stages. PR #31 now restores eye bounds and submit-stage input sanitization;
    the inspected Main-VR base does not yet contain those corrections.
-   Overall: Open Shaders remains the likely winner, but only with medium
    confidence. The final sparse path narrows the gap substantially relative to
    Main-VR's former full 5x5 default.

There is no strict quality-matched Open Shaders setting. A faster result does
not show that its exact-zero/no-final-scrub behavior is equally robust.

### Same FOV mask size

For Main-VR `FOV + TAA` with a 35% center region, the closest Open Shaders
configuration is the same resolved center rectangle plus its Gaussian 3x3
stretch, temporal smoothing, and feathering. Match the resolved pixel
rectangle, not only the UI percentage, because the two projects may define
the percentage differently.

Open Shaders is likely faster because crop-and-stretch plus a small filter and
feather does less reconstruction than Main-VR's temporal periphery path.
Main-VR is expected to retain better motion stability, edge behavior,
disocclusion handling, and center/periphery continuity. The settings are
similar in visible crop and transition softness, not algorithmically or
quality equivalent.

At fixed mask geometry, the HAM protections are not strictly ordered. Open
Shaders' single exact-zero input clear is cheaper and acts before temporal
reconstruction, but lacks Main-VR's threshold, dilation, and final guarantee.
PR #31 now has a thresholded, dilated robust input guarantee plus a sparse final
scrub. The final sparse decision is still not equivalent to its 5x5 reference.

## RC166 and authorship history

The main HAM lineage is:

| Commit      | Author         | Effect                                                             |
| ----------- | -------------- | ------------------------------------------------------------------ |
| `4b2395a89` | Alan Tse       | Original per-eye VR upscaling and exact-zero hidden-area handling  |
| `b1715b03a` | ParticleTroned | Reverted the per-eye change                                        |
| `e3f797077` | ParticleTroned | Restored it                                                        |
| `8de526eb5` | ParticleTroned | Hardened bounds/scaled mapping while retaining exact-zero behavior |
| `daa711fa9` | ParticleTroned | Added thresholded cleanup and a smaller/cardinal neighborhood      |
| `bce8cbc86` | ParticleTroned | Expanded containment to the recurring full radius-two 5x5 scrub    |
| `9c09902a1` | ParticleTroned | Ported the clean RenderScale submit pipeline                       |
| `3f4441c45` | ParticleTroned | Hardened fallback paths                                            |
| `2bd9db067` | ParticleTroned | Gated clearing by the active upscaling owner                       |
| `6caaae0be` | ParticleTroned | Added the DevBench HAM handoff probe                               |
| `dfc72cc63` | Treatid2       | Bounded recovery churn and coherent holds                          |

ColdBomb did not introduce the full 5x5 scrub. The main recurring kernel is
primarily ParticleTroned's work, building on Alan Tse's original per-eye path.

The 5x5 commit `bce8cbc86` is an ancestor of RC166. Post-RC166 code is much
more elaborate, but the distinction is:

-   stable GPU cost: full-screen/eye shader dispatches and resource traffic;
-   stable CPU cost: plan queries, branches, binds, and constant updates;
-   transition-only cost: recreation, mutation-time validation, repair, relatch,
    reset, and bounded logging;
-   DevBench-only cost: counters, audit shader, readback, and control surface.

Most additional **GPU recovery passes** are transition- or DevBench-gated. It
is no longer accurate to say that most later elaboration is absent from the
stable CPU path: current frames still pay first-frame-call resource validation,
large controller snapshots, per-eye presentation/fidelity mutation, memory
polling, and the OpenVR presentation transaction lock. The ownership gate in
`2bd9db067` prevents ordinary-vendor/submit duplication, but does not prevent
the two foveated submit-phase clears. The recovery work in `dfc72cc63` remains
bounded safety work rather than a normal extra GPU pass.

## Ranked, quality-preserving optimization plan

The ranking balances breadth, likely benefit, confidence, and implementation
risk. A backend- or FOV-specific item can be the largest win for an affected
configuration even when a universal CPU item ranks above it. Correctness items
C1-C6 are prerequisites and are not traded against frame time.

### Stable CPU, controller, and synchronization

#### 1. Make stable resource validation generation-driven

Priority: highest-ranked source-level CPU candidate. The repeated stable-path
work is proven; its frame-time magnitude remains unmeasured pending matched
per-stage timers.

Increment an authoritative device/resource publication generation whenever
common vendor textures, views, dimensions, method, or device ownership change.
Stable frames compare method, work-gate state, generation, and the cached
immutable descriptors. Run the complete `GetDevice`/COM identity/`GetDesc`
proof at creation/publication, relatch, device reset/loss, or generation change.

Add a normally-zero FSR failure/lifecycle fast gate before building the terminal
resource key and taking transition snapshots. Revalidate under the existing
locks when the bit is nonzero. This removes repeated work, not validation.

#### 2. Split a compact hot contract from cold controller diagnostics

Priority: highest-ranked source-level controller candidate. The repeated copy
and derivation are proven; their frame-time magnitude remains unmeasured pending
matched per-stage timers.

The hot contract needs revision, state, epoch, generation, method, dimensions,
owner, FOV profile identity, and exceptional flags. It does not need the
50-record metrics ring, stress history, complete recovery state, or diagnostic
snapshots on every read.

Publish one coherent compact contract per authoritative compositor/presentation
cycle, pass it through every eye produced by that cycle, and revision-check at
the existing commit boundaries. Do not key it only to game-frame identity: VR
may produce multiple presentation cycles within one game frame. Keep the
complete snapshot accessor for UI, DevBench, diagnostics, transition work, and
failure investigation. Do not replace coherence with unrelated atomics.

#### 3. Make telemetry, polling, and recovery state-proportional

Expected value: medium-high CPU/frame-pacing opportunity. Confidence: high.

Publish a compact transition-work bitmask. Use unlocked zero fast paths for
normally empty guard, promotion, recovery, pending-request, memory-pressure,
and trim work; acquire the owner lock and revalidate only when a bit is set.

During `Stabilizing`, recovery, mismatches, stress, or diagnostics, retain exact
per-eye fidelity and presentation evidence. Once Active and stereo-proven,
publish a compact once-per-cycle health marker. Keep forced fresh memory samples
before admission, creation, trim, and recovery; use cached atomic validity and a
slower/event-driven cadence during stable rendering.

#### 4. Shorten the presentation lock with a strongly owned stereo packet

Expected value: medium-high tail-latency opportunity. Risk: high redesign.

Under the existing lock, publish an immutable packet containing both eye
contracts and strong resource ownership. Release the queue lock for D3D/vendor,
overlay, and OpenVR work, then revision-check before observation commit. The
packet and retirement system must prove that neither resources nor policy can
be invalidated in flight. Do this after items 1-3 simplify the contract.

### FOV, HAM, and backend GPU work

#### 5. Copy only active runtime-FSR foveated input rectangles

Expected value: potentially highest FOV + runtime-FSR bandwidth saving.
Acceptance: conditional on provider padding proof.

Build shared-copy extents from active render dimensions and source validity,
not oversized center allocation descriptions. Retain full-resource D3D12
barriers where the API requires them. Validate SDK padding/extent assumptions,
formats, odd sizes, and every center scale before replacing the current path.

#### 6. Batch both runtime-FSR eyes into one interop transaction

Expected value: high for runtime FSR3/4. Acceptance: synchronization proof.

Copy both eyes' inputs, perform one D3D11-to-D3D12 handoff, record both context
dispatches and their barriers in one command list, perform one return handoff,
then copy both outputs. Preserve per-eye contexts, reset flags, failure
quarantine, lifetime ownership, and the current per-eye route as fallback.

#### 7. Conditionally eliminate or fuse the early foveated HAM clear

Expected value: conditional FOV/HAM GPU saving. The source proves two
phase-eligible calls, not unconditional duplicate work.

The current route performs `SubmitStageFoveatedOutput` and later
`SubmitStageOutput` on each eye. Define ownership by output identity and by
intervening reader/writer semantics, not merely by enum phase. The default DLSS
RCAS sharpener may consume the early-sanitized texture, and menu composition may
write before finalization, so the calls can protect different dependency edges.

Only eliminate the early clear when the complete route proves no intermediate
consumer needs it. Otherwise retain its input protection or exactly fuse the
classification into that consumer, then keep one authoritative guarantee after
the last writer. Never remove a distinct pre-temporal, pre-sharpen, or final
protection merely to reduce dispatch count.

#### 8. Gate compatible desktop-mirror writeback on a consumer

Expected value: conditional medium GPU bandwidth saving.

The compatible path can write two display-sized eyes back to the source even
when the incompatible fallback's stabilization setting is off. Require an
actual mirror/screenshot/capture consumer. First profile how often normal
RenderScale sources satisfy the compatible condition; do not count this as a
universal saving without that evidence.

#### 9. Reduce FOV staging and composition passes within provider contracts

Expected value: medium-high FOV opportunity. Acceptance: conditional.

Investigate direct DLSS subrect/viewport use and active-region FSR resources to
remove the five center staging copies per eye. For non-Periphery-TAA FOV,
dispatch the center first and investigate one exact full-eye spatial composite
for periphery and center blend. A Periphery TAA variant must explicitly retain
or fuse all color, velocity, and lock-history writes. Place the authoritative
HAM decision after the last spatial/temporal writer, while retaining
pre-sharpen sanitization when sharpening samples the foveated result. Preserve
pinhole offsets, motion-vector scale, feather shape, history behavior, menu
composition, formats, and SDK lifetime.

#### 10. Crop or atlas Periphery TAA history conservatively

Expected value: medium residency/bandwidth saving at high HMD resolutions.

Store the padded outer-TAA region instead of twelve full display-eye history
surfaces per stereo pair (six per eye), or use a stable tile atlas. Prove
reprojection and
filter tap padding for head motion, disocclusion, changing mask offsets, camera
cuts, odd dimensions, and history resets before accepting the smaller domain.

### Exact-quality HAM options

#### 11. Measure the corrected exact eye-local depth mask and reuse it

Expected value: exact decision consolidation plus conditional GPU saving.

This direction was first attempted by `988e2f3c3` as
`reduced_resolution_mask`; it is not a new idea and that removed implementation
must not be restored unchanged. Commit `726642c0d` supplies a corrected
DevBench candidate named `exact_reusable_mask`: build `hidden = depth <= 1e-6`
within each eye, perform the exact radius-two square decision once per
eye-local depth coordinate, sanitize input before temporal reconstruction, and
point-read the same mask at final output. A future separable Boolean dilation
could reduce mask-build loads while remaining exact:

```text
horizontal[x,y] = OR hidden[x-2..x+2,y]
mask[x,y]       = OR horizontal[x,y-2..y+2]
```

Equivalence requires identical integer mapping, eye-local clipping, odd-size
and offset handling, Boolean point reads, ownership keyed to the authoritative
submit/compositor cycle and source-depth generation, and resource hazards.
Never reuse a depth-derived mask across presentation cycles or depth
generations. PR #31's robust-to-sparse delta does not bound this exact-mask
proposal: it measures two output-scrub kernels, while mask construction and
reuse change the dataflow and require their own matched measurement. At the
current PR head, the candidate validates the current game frame, depth-resource
identity, eye-local dimensions and offsets, point-read resource, and shader
availability before reuse; failure selects the untiled robust reference. That
is enough to run a controlled experiment, not enough to infer a saving or skip
the compositor-cycle, source-depth-generation, transition, and post-load test
matrix. For non-foveated stereo it trades four direct HAM dispatches for six
dispatches: mask build, input point-read, and final point-read per eye. Its
value depends on the saved depth reads outweighing the extra dispatch and mask
traffic.

#### 12. Measure the new tiled exact 5x5 kernel

Expected value: conditional. Acceptance: bit-equivalent output.

The current DevBench `robust_depth_5x5` baseline is the straightforward
untiled kernel with up to 25 direct depth loads per output decision. It is not
this option. Commit `726642c0d` adds the separate `tiled_exact_5x5` candidate:
an 8x8 color group loads an eye-local depth tile plus a two-pixel halo into
groupshared memory and evaluates the same 25 Boolean positions. In the
upscaling/equal-size domain the mapped depth span fits a bounded 12x12 tile;
unexpected wider mappings execute the untiled predicate. The eye-bounded
untiled shader remains the runtime fallback and fidelity oracle.

The DevBench tiled fidelity variant compares the tiled decision to the direct
robust decision at every output pixel rather than the normal stride-two sample.
Static coordinate-model checks cover the bounded halo construction, but only
the same in-game performance and visual matrix can establish whether group
synchronization and groupshared traffic are cheaper on the target GPU.

Run the four modes as one matched, non-foveated A/B set first. Warm shaders and
resources, reset HAM diagnostics before each mode, keep the quality capture off
during timing, collect the same resolved-frame count and statistic, and report
both pass totals and whole-frame GPU time. Sum these mode-specific timers:

-   `robust_depth_5x5`: `InputRobust` plus `FinalRobust`;
-   `sparse_depth_9tap`: `InputRobust` plus `FinalSparse9` on RenderScale;
-   `exact_reusable_mask`: `ExactMaskBuild`, `ExactMaskInput`, and
    `ExactMaskFinal`;
-   `tiled_exact_5x5`: `InputTiledExact5x5` plus
    `FinalTiledExact5x5`.

Then run fidelity captures separately. Reject an exact candidate if mismatches,
skipped dispatches, build failures, or robust fallbacks are nonzero. Repeat the
winner on foveated routing, where extra final-class clears change the total.

Do not select another fixed sparse pattern and call it equivalent. Any strict
subset can miss; it is acceptable only under an explicitly redefined and
visually validated product quality contract.

### Hitch prevention and measurement discipline

#### 13. Prewarm bounded resources and finish D3D state hygiene

Expected value: transition smoothness and robustness more than stable FPS.

Precompile production and verified-repair HAM variants and create required
constant/resources after the VR device and plan become valid, outside the
presentation-critical call. Retain lazy device-loss recreation. Complete the
overlay and menu-motion-vector state guards before attempting eye-pair binding
reuse; fewer state changes are useful only after restoration is correct.

#### 14. Keep diagnostics compiled and activated proportionally

Expected value: deterministic measurement integrity.

`DEVBENCH_BRIDGE` is OFF by default. Preserve that default and the compile-time
boundary around quality resources, counters, audit work, and readback. With a
bridge build, quality capture must remain explicit and inactive during timing.

Add timers/counters for stable resource validation, bytes copied by snapshot,
lock wait/hold time, memory queries, FOV center copies, periphery/TAA/blend,
runtime-FSR handoffs, mirror writes, and per-eye HAM phase counts. Admit FOV
only when its warm whole-pipeline cost beats full-eye vendor by a stable margin.
Falling back to the existing full-eye vendor route is a valid profitability
decision; any quality difference must be measured.

## Rejected optimizations

The following are not accepted without new proof:

1. Restore exact-zero center-only clearing as production. It removes the
   threshold and dilation.
2. Assume sparse nine-tap is quality-equivalent because it reaches radius two.
   It omits 16 reference positions.
3. Treat the historical robust/audit capture as eye-exact. Candidate and oracle
   shared the seam bug when those measurements were collected; the corrected
   shader requires a new capture.
4. Remove RenderScale input sanitization because a final scrub exists. Final
   clearing cannot remove temporal contamination already reconstructed into a
   visible pixel or committed to history.
5. Remove the final scrub solely because input was sanitized. Vendor
   reconstruction can repopulate edge color and the final guarantee is part of
   Main-VR's robustness contract.
6. Remove the early foveated scrub when sharpening or another intervening
   consumer needs sanitized input. Deduplication must follow data dependency.
7. Replace the depth-derived mask with the OpenVR mesh without establishing
   equivalence across runtimes, projections, relatches, and depth holes.
8. Use bilinear filtering for a Boolean mask. Interpolation changes the
   classification unless a carefully proven conservative threshold is used.
9. Reuse a depth-derived mask across frames. The content is dynamic even when
   projection and mask geometry appear stable.
10. Delete bounds, identity, transition, post-load, or ownership checks to save
    stable-frame time. Most are gated; failures are severe and intermittent.
11. Remove D3D11/D3D12 barriers or fence handoffs based only on a successful
    local run.
12. Replace Main-VR FOV + TAA with crop-and-stretch while claiming unchanged
    fidelity. It is a different reconstruction method.
13. Run fidelity instrumentation during a performance capture. Its separate
    pass contaminates the measurement.

## DevBench measurement and quality contract

PR #31 exposes the versioned `communityshaders.renderscale` tool. Every call
must include the discovered `expectedBuildId` so automation fails closed when
MO2 loads a stale DLL.

Modes:

-   `sparse_depth_9tap`: production candidate/default;
-   `robust_depth_5x5`: reference and verified-repair implementation.

The Upscaling UI intentionally has no HAM controls. `ham_set_mode` performs
the live A/B switch, requests temporal-history reset, and logs at Info, so
Info logging is sufficient.

Stable timers are:

-   `Upscaling::HAM::InputRobust`;
-   `Upscaling::HAM::InputSparse9`;
-   `Upscaling::HAM::FinalRobust`;
-   `Upscaling::HAM::FinalSparse9`;
-   `Upscaling::HAM::VerifiedRepairRobust`;
-   `Upscaling::HAM::DevBenchQualityAudit`.

### Performance sequence

For each mode:

1. Call `ham_status` and verify build ID, mode, route, dimensions, render
   scale, and quality-capture inactivity.
2. Stabilize the same save, camera, headset pose sequence, weather, menu
   state, modlist, and backend.
3. Call `ham_reset` immediately before a bounded profiler capture.
4. Preserve profiler history, status, dispatch counters, and session
   metadata.
5. Repeat identical warm-up and capture for the other mode.
6. Randomize or alternate mode order across repeated trials to reduce thermal
   and time drift.

Sum active input and final timers only when the route legitimately executes
both. Keep `VerifiedRepairRobust` separate from stable cost.

Reject captures with mismatched build ID, dimensions, plan state, resolved
frames, mode, dispatch count, skipped work, or active fidelity audit.

Report:

-   median, p95, p99, and dispersion for each HAM timer;
-   total GPU frame time and CPU frame time;
-   per-eye/phase dispatch counts;
-   backend and exact vendor/runtime versions;
-   resolution and resolved center rectangle for FOV comparisons.

For a stable non-foveated RenderScale stereo frame, expect two final dispatches.
The current foveated route should expose four. Any optimization of that route
must document which pre-consumer and final guarantees remain, not merely reduce
the counter.

### Fidelity sequence

After performance capture is complete, start a separate bounded audit:

```json
{
    "action": "ham_quality_start",
    "maxFrames": 60,
    "expectedBuildId": "<build-id>"
}
```

Poll `ham_quality_status` until complete. The audit samples one fixed parity of
a one-in-four display-domain grid and compares the active decision to the
thresholded two-pixel-dilated 5x5 reference. It never writes presentation
color. In the current PR, both decisions share full-texture rather than
eye-local neighborhood bounds; correct that oracle and rotate all four parity
phases before treating a new capture as the quality baseline.

Preserve:

-   evaluated and robust-clear samples;
-   candidate-clear samples;
-   false negatives and false positives;
-   mismatches and skipped dispatches;
-   sparse miss classification for center, 3x3, radius-two cross, radius-two
    nine-tap, and full 5x5;
-   audit timer and exact frame count.

The current fixed-parity grid is useful regression evidence, not a mathematical
proof that unsampled pixels never differ. Add exhaustive synthetic textures,
odd dimensions, scale ratios, and seam tests. Automated image captures around
both eye edges and motion sequences remain necessary.

### Whole-pipeline Main-VR/Open Shaders test

Use the same:

-   modlist and save;
-   GPU/driver and power state;
-   headset/runtime resolution;
-   engine render resolution;
-   vendor backend, preset, sharpening, and frame-generation state;
-   FOV resolved center rectangle and feather width when FOV is included;
-   scripted camera/head motion and capture duration.

Capture at least:

1. Main-VR robust 5x5 reference;
2. Main-VR sparse nine-tap;
3. Open Shaders closest performance-mode configuration;
4. each pipeline without FOV;
5. Main-VR FOV + TAA at 35% and Open Shaders' closest Gaussian
   stretch/temporal-smooth/feather configuration.

Compare whole-frame GPU time, not only the HAM timer. Pair it with image
metrics and visual inspection of hidden-area boundary motion, disocclusion,
water, menus, and post-load behavior.

## Acceptance gates

### Sparse nine-tap

The recorded capture already proves that sparse is not bit-equivalent: 2,640
false negatives were observed. Promote it only under an explicitly relaxed,
risk-accepted contract that permits a bounded under-clear rate, and only if all
of the following pass after the reference is repaired:

-   eye-local reference and candidate tests pass every seam/bounds invariant;
-   the permitted false-negative threshold is defined before capture and remains
    below it over the supported matrix;
-   false positives remain zero;
-   visual sequences show no edge leakage or temporal contamination;
-   post-load verified repair remains robust;
-   no route duplicates or skips eligible dispatches;
-   performance improvement is repeatable outside measurement noise.

Under a strict zero-divergence rule, this gate has failed and sparse remains an
A/B candidate. Use an exact optimization from ranking item 11 or 12.

### Exact reusable mask

Accept only if:

-   its decision is bit-equivalent to robust 5x5 over exhaustive synthetic
    textures and runtime captures;
-   the robust oracle itself is constrained to the current eye;
-   odd sizes, scale ratios, offsets, both eye seams, and out-of-bounds
    footprints pass;
-   vendor and Periphery TAA inputs are sanitized before temporal consumption;
-   input and final consumers use the correct submit/compositor cycle and
    source-depth generation;
-   transition/reset/post-load behavior cannot consume stale resources;
-   release builds contain no diagnostic pass or readback;
-   the total GPU improvement remains positive after mask build and hazards.

### Persistent compositor mesh

Do not restore based only on nominal mask shape. Require proof that it is at
least as conservative as the depth reference across supported OpenVR and
OpenComposite paths, plus lifecycle and projection-change tests.

## Final recommendation

PR #31 is valuable and should remain the measurement base. It removes failed
HAM experiments, compiles DevBench work out of normal builds, fixes eye-local
sampling, restores robust submit-stage input sanitization, and now exposes two
exact candidates for controlled testing. Its historical
capture demonstrates a `39.97%` reduction in the tested final HAM timer, but the
current route adds a robust input pass and changes seam decisions. The absolute
`0.084958 ms` delta therefore describes only the old sparse-versus-robust final
substitution. Reprofile the complete current HAM route and whole frame before
assigning performance or regression causality.

Do not call the current sparse result quality-equivalent. It produced a very
small but nonzero sampled under-clear rate and the fixed-parity grid is
incomplete. The PR now constrains every HAM decision to the current eye and
sanitizes submit-stage temporal input, but the recorded ratios predate both
fixes. Rebaseline sparse versus robust across all parities, synthetic seams/odd
sizes, supported runtimes/backends, head motion, water, menus, loading, and
post-load repair.

For performance, implement in this order:

1. generation-driven stable resource validation;
2. one compact hot presentation contract per authoritative presentation cycle;
3. state-proportional telemetry, memory, and recovery work;
4. active-region and batched stereo runtime-FSR interop;
5. dependency-proven elimination or fusion of the early foveated HAM clear;
6. measured FOV staging/composition/history reductions;
7. a strongly owned stereo packet before shortening the presentation lock;
8. run the now-instrumented exact reusable-mask and tiled-5x5 A/B against the
   corrected eye-local robust oracle; promote neither without zero-divergence
   fidelity and positive whole-route performance evidence.

The comparison with RC190 is plausible but not yet causal. RC190 itself did not
change these pipeline files; RC207 and RC216 are the first high-value matched
A/B boundaries. Preserve every epoch, generation, device-loss, retirement,
stereo-proof, post-load, bounds, format, and final-output safety guarantee while
making their cost proportional to actual state changes.

Open Shaders is still analytically likely to be faster at matched resolution
and vendor preset, especially for its simpler FOV approximation, but it does
not provide an equivalent quality/robustness contract. A whole-pipeline matched
capture remains the only valid performance conclusion.
