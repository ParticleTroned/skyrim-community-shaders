# VR RenderScale performance audit

## Scope, revisions, and acceptance rule

This document combines the architectural requirements in the repository-root
[CSX 3.15-VR RenderScale port notes](../../PL3.15-VR-render-scale-port.txt)
with a code-level audit of Main-VR RenderScale, its earlier implementations,
PR #31, and Open Shaders performance mode.

The inspected reference points are:

- Main-VR: `02f61ff344924c8097031b7c6a33d039fd297fe7`.
- PR #31 final HAM implementation:
  `c2249f2dd333a63f0f7b2a2f04f0fa45d0ffc73b`.
- RC166: `b95958cc0c119625d8a7dd9657442c92122f240d`.
- Open Shaders `dev`:
  `b3f955d8cafe7f918ba8538f0710b2286eadd5ea`.

The PR implementation is documented as code that is proposed for Main-VR, not
as code already present in the Main-VR baseline.

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

PR #31's sparse nine-tap mode is therefore a performance candidate, not yet a
quality-preserving conclusion.

## Executive conclusions

1. Ignoring foveation, Open Shaders is still the likely faster stable-frame
   pipeline at the same engine resolution and vendor preset. Confidence is
   medium, because Open Shaders also performs a display copy, a render-domain
   3x3 downscale, underwater repair, resource swaps, and conditional replay
   work. A whole-frame capture is required to settle the result.

2. This is not a quality-equivalent comparison. Open Shaders uses an
   input-domain exact-zero HAM test and does not provide Main-VR's thresholded,
   dilated, final-display-domain guarantee or its verified repair behavior.
   It is faster partly because it solves a narrower problem.

3. PR #31 changes Main-VR's ordinary HAM cost from a full 5x5 reference to a
   sparse radius-two nine-tap pattern. For display pixels that would execute
   all 25 reference loads, this removes 16 of 25 depth loads, a 64% reduction
   in depth-load instructions for that pass. It is not a 64% frame-time claim:
   dispatch cost, coordinate arithmetic, cache hits, stores, and the robust
   kernel's early exit remain.

4. The nine sampled locations are a strict subset of the 5x5 reference.
   Under identical mapping and threshold it cannot clear a pixel that the 5x5
   reference would retain, but it can retain a pixel when a hidden sample
   exists only in one of the 16 omitted locations. Those false negatives are
   exactly the edge-leak risk that dilation was designed to prevent.

5. The final PR contains two modes only:
   `sparse_depth_9tap` is the production default and
   `robust_depth_5x5` is the DevBench/reference and verified-repair path.
   Earlier `legacy_exact_zero`, reduced-resolution-mask, and persistent
   OpenVR-mesh-mask experiments were removed. There is no in-game HAM toggle.

6. Main-VR's stable RenderScale submit route performs one final display-domain
   HAM dispatch per eye. Phase ownership prevents the ordinary vendor route
   from also clearing those eyes. The ordinary non-RenderScale vendor route
   clears input and output per eye. Verified post-load repair can force 5x5,
   but that is bounded recovery work rather than normal-frame cost.

7. Both Main-VR and Open Shaders pay the same core D3D11-to-D3D12 shared
   resource bridge when runtime FSR3/4 is selected in the inspected code:
   per eye, five input copies and one output copy, twelve begin/end barriers,
   cross-API fence handoffs, and one D3D12 submission. Stereo totals are
   twelve copies, twenty-four barriers, and two D3D12 submissions. That is not
   a Main-VR-only tax.

8. The full radius-two 5x5 scrub predates RC166. ParticleTroned introduced it
   in `bce8cbc86` on 2026-04-25; RC166 descends from it. Most post-RC166
   elaboration protects transitions, lifetimes, ownership, and recovery and
   is gated away from the stable path. The major recurring GPU cost existed
   before RC166.

9. The best quality-preserving direction remains building the exact
   thresholded 5x5 decision once in the lower-resolution depth domain and
   reusing that mask for temporal input and final output. Unlike the removed
   experiment, the mapping must be proved equivalent at eye seams, edges, and
   every render/display ratio. A tiled exact 5x5 kernel is an alternative that
   changes no decision at all.

10. The previously quoted 50-80% saving applies only as a plausible range for
    the old full-scrub cost when an exact reusable lower-resolution mask is
    used. It is not a measured total-frame saving, and it does not describe
    the final nine-tap implementation.

## Architectural invariants from the port

The root port note sets constraints that performance work must retain:

- RenderScale and final submission form one pipeline.
- When presentation RenderScale is inactive, submission falls through to the
  original compositor path.
- There is no second legacy submit-replacement path.
- One runtime plan owns display, engine-render, and final-submit dimensions.
- Resources are recreated only when the active plan changes.
- Loading and menu-sized presentation may use the lightweight stretch stage.
- HMD and controller overlays retain separate dimensions and SRVs so menu text
  remains sharp and stable.
- Underwater and projected/worldspace mask repair remains present.
- Normal logging remains bounded.
- VRS, black-square, fade, BootExit, FADERUI, and old dynamic-resolution
  experiments remain outside this pipeline.

These are not incidental checks to delete for speed. They define the supported
state machine and avoid duplicate ownership, stale resources, invalid
submissions, and post-load corruption.

## Pipeline maps

### Main-VR RenderScale, stable vendor frame

The stable path is conceptually:

```text
packed stereo engine render
  -> per-eye vendor inputs
  -> DLSS/DLAA, host FSR, or runtime FSR
  -> optional foveated center/periphery composition
  -> final display-sized eye texture
  -> one HAM scrub for that eye
  -> compositor submission
```

Important ownership details:

- `ShouldClearHMDMaskInPhase()` allows submit-stage clearing only while
  presentation upscaling is active and the runtime plan owner is
  `VRRenderScaleMode`.
- The same function disables input/output clearing in the ordinary vendor path
  while RenderScale owns the presentation.
- Stable non-foveated RenderScale therefore has two final HAM dispatches per
  stereo frame, not input plus output plus submit-stage duplicates.
- A forced robust dispatch is used for verified repair and binding validation,
  not as the normal sparse path.

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

| Concern | Implementation |
| --- | --- |
| Runtime plan, backend routing, HAM ownership and dispatch | [Upscaling.cpp](../../src/Features/Upscaling.cpp) |
| HAM modes, resources, counters, and state | [Upscaling.h](../../src/Features/Upscaling.h) |
| Sparse, robust, probe, and fidelity-audit shaders | [ClearHMDMaskCS.hlsl](../../features/Upscaling/Shaders/Upscaling/ClearHMDMaskCS.hlsl) |
| DevBench control surface | [VRRenderScaleDevBenchBridge.cpp](../../src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp) |
| Measurement contract | [vr-hmd-mask-devbench.md](../development/vr-hmd-mask-devbench.md) |
| Foveated region sizing | [FoveatedRegionPlan.h](../../src/Features/Upscaling/FoveatedRegionPlan.h) |
| Periphery reconstruction | [FoveatedPeripheryCS.hlsl](../../features/Upscaling/Shaders/Upscaling/FoveatedPeripheryCS.hlsl) |
| Periphery temporal reconstruction | [PeripheryTAACS.hlsl](../../features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl) |
| Runtime FSR bridge | [FidelityFX.cpp](../../src/Features/Upscaling/FidelityFX.cpp) and [DX12SwapChain.cpp](../../src/Features/Upscaling/DX12SwapChain.cpp) |
| DLSS/DLAA integration | [Streamline.cpp](../../src/Features/Upscaling/Streamline.cpp) |

## Final PR #31 HAM implementation

### Shared predicate and coordinate contract

`ClearHMDMaskCS.hlsl` uses:

- hidden threshold `depth <= 1e-6`;
- reversed-Z interpretation where the far/unrendered value is near zero;
- a nominal depth-space dilation radius of two pixels;
- eye-local color and depth offsets;
- integer color-to-depth coordinate scaling;
- depth and color resource bounds checks;
- an 8x8 dispatch group for production kernels.

The color texture may be display-sized while depth remains render-sized.
Every output thread maps its local color coordinate to a depth coordinate,
then tests a neighborhood around that depth position.

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

- `S(p) => R(p)`: a sparse positive is also a robust positive;
- `R(p) && !S(p)` is possible: a sparse false negative;
- `S(p) && !R(p)` is impossible if texture, mapping, threshold, and bounds are
  identical.

The quality risk is therefore under-clearing, which may leave hidden-area
color available to temporal reconstruction at a moving edge.

### `robust_depth_5x5`

The reference evaluates all valid offsets in `[-2,+2]^2`, up to 25 depth
loads per color pixel. It early-outs its useful loads once it finds a hidden
sample. It is compiled with `HMD_MASK_ROBUST_DEPTH_5X5`.

The robust shader is used for:

- controlled DevBench A/B comparison;
- verified post-load candidate repair;
- any call that explicitly forces the reference;
- binding-verification work.

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

- sparse final scrub once per submitted eye under RenderScale; or
- sparse input and output scrub per eye under the ordinary vendor route.

Exceptional work:

- robust verified post-load repair;
- resource/shader creation;
- device-loss and transition cleanup;
- probe capture;
- fidelity audit and readback;
- temporal-history reset after a DevBench mode change.

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

The final state must be inferred from `c2249f2dd`, not the original PR title
or an intermediate commit.

### Removed exact-zero path

The exact-zero path was useful for reproducing the earlier
`8de526eb5` behavior and measuring a lower bound. It was not quality
equivalent:

- exact `depth == 0.0` misses small non-zero invalid depths;
- one center sample has no dilation;
- it does not protect the neighborhood consumed by reconstruction.

Its removal from the final mode list is consistent with this audit's
quality-first acceptance rule.

### Removed reduced-resolution mask

The early PR built a thresholded/dilated lower-resolution mask, used it before
temporal reconstruction, and reduced final work toward one mask lookup. That
architecture remains promising, but the particular implementation is not in
the final PR and must not be reported as delivered.

For equivalence, a replacement must prove that each final output pixel reads
the exact robust decision associated with the same mapped input-depth
coordinate. Bilinear filtering of a binary mask, insufficient conservative
coverage, cross-eye dilation, or rounding differences can change edge
classification.

### Removed persistent runtime mesh mask

The intermediate implementation rasterized the OpenVR hidden-area mesh into a
persistent mask and attempted to reuse it. The final commit describes this
experiment as failed and removes it completely.

A compositor mesh can be cheap to sample and may describe the nominal hidden
region well, but it is not automatically equivalent to the depth-derived
predicate. Risks include:

- runtime mesh availability and lifecycle;
- projection/viewport changes and regeneration timing;
- conservative rasterization and dilation at eye boundaries;
- disagreement between nominal compositor geometry and actual depth holes;
- initialization or regeneration hitches;
- stale mask use during relatch, reset, or post-load recovery.

The commit history does not establish one single measured failure cause, so
this audit does not invent one. Reintroducing the approach would require a
separate proof against the depth reference across runtimes and transitions.

## HAM performance model

Let:

- `Pout` be display-domain pixels processed across both eyes;
- `Pin` be input-depth-domain pixels across both eyes;
- `E[K]` be the average valid depth loads in the early-exiting robust kernel;
- `Cd` be effective cost per depth lookup after cache effects;
- `Cbase` be mapping, bounds, dispatch, and branch cost per output pixel.

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

No source-only analysis can convert this into a reliable millisecond saving.
The profiler timers added by PR #31 are the authority.

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

- DLSS makes an input-domain mask relatively cheap compared with a
  display-domain scrub;
- DLAA reduces that resolution advantage because `Pin` approaches `Pout`;
- both still benefit from sanitizing hidden color before temporal history;
- final-output protection remains the robustness backstop.

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

- five D3D11-to-D3D12 input copies: color, depth, motion, reactive, and
  transparency;
- one D3D12-to-D3D11 output copy;
- six begin and six end transitions;
- fence synchronization between APIs;
- one D3D12 command-list submission.

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

- HAM alone: Open Shaders is analytically faster. It uses one input-domain
  exact-zero lookup instead of Main-VR's display-domain nine-tap final scrub
  in the stable RenderScale route.
- Vendor reconstruction: broadly comparable when the same runtime and preset
  are selected; runtime FSR3/4 bridge work exists in both.
- Surrounding pipeline: Open Shaders adds display copy, 3x3 downscale,
  underwater repair, swaps, and conditional replay. Main-VR adds stronger
  presentation ownership, overlay separation, transitions, and final HAM
  protection.
- Overall: Open Shaders remains the likely winner, but only with medium
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

At fixed mask geometry, HAM conclusions remain the same: Open Shaders'
single-sample input clear is cheaper; Main-VR's sparse final scrub is more
protective than that route but not equivalent to its own 5x5 reference.

## RC166 and authorship history

The main HAM lineage is:

| Commit | Author | Effect |
| --- | --- | --- |
| `4b2395a89` | Alan Tse | Original per-eye VR upscaling and exact-zero hidden-area handling |
| `b1715b03a` | ParticleTroned | Reverted the per-eye change |
| `e3f797077` | ParticleTroned | Restored it |
| `8de526eb5` | ParticleTroned | Hardened bounds/scaled mapping while retaining exact-zero behavior |
| `daa711fa9` | ParticleTroned | Added thresholded cleanup and a smaller/cardinal neighborhood |
| `bce8cbc86` | ParticleTroned | Expanded containment to the recurring full radius-two 5x5 scrub |
| `9c09902a1` | ParticleTroned | Ported the clean RenderScale submit pipeline |
| `3f4441c45` | ParticleTroned | Hardened fallback paths |
| `2bd9db067` | ParticleTroned | Gated clearing by the active upscaling owner |
| `6caaae0be` | ParticleTroned | Added the DevBench HAM handoff probe |
| `dfc72cc63` | Treatid2 | Bounded recovery churn and coherent holds |

ColdBomb did not introduce the full 5x5 scrub. The main recurring kernel is
primarily ParticleTroned's work, building on Alan Tse's original per-eye path.

The 5x5 commit `bce8cbc86` is an ancestor of RC166. Post-RC166 code is much
more elaborate, but the distinction is:

- stable GPU cost: full-screen/eye shader dispatches and resource traffic;
- stable CPU cost: plan queries, branches, binds, and constant updates;
- transition-only cost: recreation, validation, repair, relatch, reset, and
  bounded logging;
- DevBench-only cost: counters, audit shader, readback, and control surface.

Most later robustness code belongs to the last two categories. The ownership
gate in `2bd9db067` can improve performance by preventing duplicate clearing.
The recovery work in `dfc72cc63` trades rare bounded cost for stability rather
than adding a normal-frame pass.

## Ranked, quality-preserving optimization plan

The list is ranked by expected value under the audit's no-quality-regression
rule. Items marked conditional are not accepted until their stated proof is
complete.

### 1. Build one exact input-depth-domain 5x5 mask and reuse it

Expected value: highest. Acceptance: conditional on bit-equivalent decisions.

Build `hidden = depth <= 1e-6` at input-depth resolution, dilate it by the
exact radius-two square, use it to sanitize vendor input, and look up the same
mask from the final output mapping.

A separable Boolean dilation is exact:

```text
horizontal[x,y] = OR hidden[x-2..x+2,y]
mask[x,y]       = OR horizontal[x,y-2..y+2]
```

The final lookup is equivalent to the robust shader when:

- integer output-to-depth mapping is identical;
- eye-local bounds prevent dilation across the stereo seam;
- out-of-range samples remain excluded;
- the mask is point-read as a Boolean value;
- offsets and odd dimensions match the existing constant contract;
- resource hazards are resolved before input/final consumers.

Approximate lookup work becomes roughly `10*Pin + Pout` for a simple
two-pass build, plus `Pin` if input color is sanitized separately, rather
than up to `25*Pout`. A tiled one-pass build can reduce this further.

This is the sound version of the removed reduced-resolution-mask idea. It
should not reuse stale masks across frames: depth and projected mask coverage
can change every frame.

### 2. Implement a tiled exact 5x5 reference kernel

Expected value: high. Acceptance: direct decision equivalence.

The present shader asks many neighboring 8x8 threads for overlapping 5x5
depth footprints. Load an eye-local tile plus a two-pixel halo into
groupshared memory, synchronize once, and evaluate the same 25 Boolean
positions from the tile.

For a one-to-one 8x8 tile, the ideal depth fetch count is approximately
12x12 = 144 loads rather than 64x25 = 1600 loads, before edge and scaled
mapping complications. Upscaled color-to-depth mapping creates duplicate
mapped positions and requires careful tile-range construction, so actual
benefit and implementation complexity vary.

The output decision must match the reference bit-for-bit. Keep the untiled
shader as a verification oracle and fallback until DevBench comparison is
clean across all ratios and eye edges.

### 3. Validate sparse nine-tap before treating it as production quality

Expected value: already implemented; acceptance: not yet established.

Use PR #31 to measure `sparse_depth_9tap` against
`robust_depth_5x5`. Required evidence includes:

- zero or explicitly justified false negatives over representative captures;
- zero false positives, otherwise investigate mapping/instrumentation;
- no skipped eligible dispatches;
- no visible blue-border leakage, shimmer, or temporal edge contamination;
- coverage of head motion, sky, interiors, water, menus, load transitions,
  multiple runtimes, DLSS/DLAA, host FSR, and runtime FSR3/4;
- performance captures with the fidelity pass disabled.

If meaningful false negatives exist, sparse is not accepted under this
document's rule. Prefer item 1 or 2, or retain robust as production.

### 4. Use audit data to choose a denser exact-or-proven pattern

Expected value: medium. Acceptance: conditional.

PR #31 classifies center, thresholded 3x3, radius-two cross, radius-two
nine-tap, and full 5x5 decisions. This can identify where misses occur.
However, every strict subset of the 5x5 footprint can theoretically miss.

A denser fixed pattern is acceptable only if the intended quality contract is
redefined and validated, or if geometry guarantees make omitted positions
impossible. Sampling statistics alone do not prove universal equivalence.

### 5. Preserve and extend phase-ownership deduplication

Expected value: medium to high when a duplicate is found. Acceptance: safe
when ownership is proven.

Keep `ShouldClearHMDMaskInPhase()` as the single authority. Instrument the
stable frame signature and assert that:

- RenderScale owns only submit-stage clears;
- ordinary vendor presentation owns only input/output clears;
- post-load repair is separately classified;
- each eye and phase executes at most once for the resolved frame.

Removing an actually duplicate dispatch is quality-neutral. Removing a
distinct input or final guarantee is not.

### 6. Optimize runtime FSR bridge traffic only within the API contract

Expected value: backend-dependent and potentially high. Acceptance:
conditional on synchronization proof.

Measure copy engines, barrier time, queue waits, and submission gaps. Explore
resource reuse, batching two eyes, or eliminating a copy only where resource
format, ownership, runtime API, and lifetime permit direct use.

Retain all required fence ordering and resource transitions. A sporadic
cross-API race is a larger regression than the performance gain.

### 7. Snapshot immutable stable-frame decisions once

Expected value: low to medium CPU saving. Acceptance: straightforward.

Resolve method, owner, dimensions, foveation plan, and transition generation
once per frame and pass an immutable snapshot through eye processing. This
can reduce repeated policy queries without deleting validation.

The snapshot must be invalidated on settings, VRAPI, save/load, D3D reset,
runtime, and resolution-plan generation changes.

### 8. Prewarm required shaders and resources at a bounded safe point

Expected value: transition smoothness rather than stable FPS. Acceptance:
straightforward.

Compile sparse/reference HAM variants and create constant/mask resources when
the VR device and plan become valid, outside presentation-critical work.
Retain lazy fallback and device-loss recreation.

This removes first-use hitch risk; it does not reduce steady-state pixel work.

### 9. Reduce redundant D3D11 state traffic for the eye pair

Expected value: low. Acceptance: conditional on state restoration.

Where surrounding code permits, reuse immutable shader/CB bindings across two
eye dispatches and update only offsets. Preserve SRV/UAV hazard unbinding and
the full save/restore behavior of verified repair.

Do not add binding readback to the stable path. Its current restriction to
verified repair is appropriate.

### 10. Keep measurement code completely outside release work

Expected value: small but deterministic. Acceptance: already largely done.

PR #31 places quality resources, counters, and audit work behind
`DEVBENCH_BRIDGE_ENABLED`. Maintain that boundary. Profiler scopes should
remain cheap in normal builds, and quality capture must always be an explicit,
bounded session.

## Rejected optimizations

The following are not accepted without new proof:

1. Restore exact-zero center-only clearing as production. It removes the
   threshold and dilation.
2. Assume sparse nine-tap is quality-equivalent because it reaches radius two.
   It omits 16 reference positions.
3. Remove the final scrub solely because input was sanitized. Vendor
   reconstruction can repopulate edge color and the final guarantee is part of
   Main-VR's robustness contract.
4. Replace the depth-derived mask with the OpenVR mesh without establishing
   equivalence across runtimes, projections, relatches, and depth holes.
5. Use bilinear filtering for a Boolean mask. Interpolation changes the
   classification unless a carefully proven conservative threshold is used.
6. Reuse a depth-derived mask across frames. The content is dynamic even when
   projection and mask geometry appear stable.
7. Delete bounds, identity, transition, post-load, or ownership checks to save
   stable-frame time. Most are gated; failures are severe and intermittent.
8. Remove D3D11/D3D12 barriers or fence handoffs based only on a successful
   local run.
9. Replace Main-VR FOV + TAA with crop-and-stretch while claiming unchanged
   fidelity. It is a different reconstruction method.
10. Run fidelity instrumentation during a performance capture. Its separate
    pass contaminates the measurement.

## DevBench measurement and quality contract

PR #31 exposes the versioned `communityshaders.renderscale` tool. Every call
must include the discovered `expectedBuildId` so automation fails closed when
MO2 loads a stale DLL.

Modes:

- `sparse_depth_9tap`: production candidate/default;
- `robust_depth_5x5`: reference and verified-repair implementation.

The Upscaling UI intentionally has no HAM controls. `ham_set_mode` performs
the live A/B switch, requests temporal-history reset, and logs at Info, so
Info logging is sufficient.

Stable timers are:

- `Upscaling::HAM::InputRobust`;
- `Upscaling::HAM::InputSparse9`;
- `Upscaling::HAM::FinalRobust`;
- `Upscaling::HAM::FinalSparse9`;
- `Upscaling::HAM::VerifiedRepairRobust`;
- `Upscaling::HAM::DevBenchQualityAudit`.

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

- median, p95, p99, and dispersion for each HAM timer;
- total GPU frame time and CPU frame time;
- per-eye/phase dispatch counts;
- backend and exact vendor/runtime versions;
- resolution and resolved center rectangle for FOV comparisons.

### Fidelity sequence

After performance capture is complete, start a separate bounded audit:

```json
{
  "action": "ham_quality_start",
  "maxFrames": 60,
  "expectedBuildId": "<build-id>"
}
```

Poll `ham_quality_status` until complete. The audit samples a stable
one-in-four display-domain grid and compares the active decision to the
thresholded two-pixel-dilated 5x5 reference. It never writes presentation
color.

Preserve:

- evaluated and robust-clear samples;
- candidate-clear samples;
- false negatives and false positives;
- mismatches and skipped dispatches;
- sparse miss classification for center, 3x3, radius-two cross, radius-two
  nine-tap, and full 5x5;
- audit timer and exact frame count.

The one-in-four grid is strong regression evidence, not a mathematical proof
that unsampled pixels never differ. Automated image captures around both eye
edges and motion sequences remain necessary.

### Whole-pipeline Main-VR/Open Shaders test

Use the same:

- modlist and save;
- GPU/driver and power state;
- headset/runtime resolution;
- engine render resolution;
- vendor backend, preset, sharpening, and frame-generation state;
- FOV resolved center rectangle and feather width when FOV is included;
- scripted camera/head motion and capture duration.

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

Promote as quality-preserving only if:

- fidelity captures show no meaningful false negatives in the supported test
  matrix;
- false positives remain zero;
- visual sequences show no edge leakage or temporal contamination;
- post-load verified repair remains robust;
- no route duplicates or skips eligible dispatches;
- performance improvement is repeatable outside measurement noise.

If these gates fail, retain robust production behavior and use an exact
optimization from ranking item 1 or 2.

### Exact reusable mask

Accept only if:

- its decision is bit-equivalent to robust 5x5 over exhaustive synthetic
  textures and runtime captures;
- odd sizes, scale ratios, offsets, both eye seams, and out-of-bounds
  footprints pass;
- input and final consumers use the correct frame generation;
- transition/reset/post-load behavior cannot consume stale resources;
- release builds contain no diagnostic pass or readback;
- the total GPU improvement remains positive after mask build and hazards.

### Persistent compositor mesh

Do not restore based only on nominal mask shape. Require proof that it is at
least as conservative as the depth reference across supported OpenVR and
OpenComposite paths, plus lifecycle and projection-change tests.

## Final recommendation

PR #31 is valuable because it turns the HAM question into a measurable,
versioned experiment and removes failed or misleading modes from the final
surface. Its sparse nine-tap default likely recovers a substantial portion of
the old full 5x5 pass cost, but it obtains that reduction by sampling fewer
positions. Under the stated quality requirement, it remains provisional until
the new false-negative and visual evidence passes.

For a production-quality endpoint, retain the robust 5x5 decision as the
oracle and prioritize an exact input-depth-domain mask reused before temporal
reconstruction and at final presentation. A tiled exact 5x5 shader is the
next-best path. Keep phase ownership, bounds, transitions, final-output
protection, and post-load repair intact.

Until controlled DevBench results exist, the complete analytical comparison
remains:

- Open Shaders is likely faster, both without FOV and with its closest
  crop/stretch/temporal/feather equivalent.
- Main-VR provides the stronger robustness and fidelity contract.
- PR #31 narrows Main-VR's HAM performance gap, but does not by itself prove
  that the gap was closed without a quality compromise.
