# CSX Astra master implementation plan

Updated 4 October 2026. PR104 implements optional VR Hi-Z culling.
The selected path is guarded projected-face testing with preferred 2x2
source reduction. Alternative proof/coarse-depth selectors and their extra
shaders are removed. The existing 4x4 resource-limit fallback remains.

The latest noon comparison measured source `e9b2a6960`: guarded 2x2 averaged
15.51 ms CPU / 9.95 ms GPU against Advanced's 11.09 / 8.46 ms. Hi-Z GPU
frames remain 17.67% slower, with substantial repeat variation. Separate
300-frame captures measured 0.794-0.831 ms Hi-Z culling versus 0.068 ms
Advanced; bounds testing contributes 90.75% of the mean Hi-Z total.
Separate counter cohorts rejected 36.47-37.61% versus Advanced's 60.06%
after recovery. These are candidate records, not matched objects or draws.

The next build removes polygon survivor copies using alternating clip
banks and replaces repeated prepared-face struct selection with indexed
invocation-private metadata. Guarded arithmetic, clipping order and safety
criteria are retained. Four strict shader permutations and 12/12 focused
tests pass; runtime benefit awaits the next in-game comparison. The earlier
vertex-array copy is already removed. See the
[cost analysis](vr-hybrid-culling-guarded2-analysis-2026-10-04.md).
Advanced remains default. Motion/lifecycle and SE/AE qualification remain
open. PBR grass, grass optimization and Reverse Z remain later PRs.

## Scope and authority

The current user request is to cross-check the two supplied handovers,
improve the plan and implementation where justified, and deliver separate
PRs to `ParticleTroned/skyrim-community-shaders` in this order:

1. Hi-Z culling (PR104); independently usable with the current renderer.
2. PBR grass.
3. Grass optimization.
4. Reverse Z.

Additional focused PRs may separate shared grass Hi-Z and quality controls.
PR104's title and motivation concern implementing Hi-Z culling only.
Its description omits package hashes and superseded timing campaigns;
only the final controlled performance comparison belongs there. Detailed
provenance and historical evidence remain in the linked records.
All PRs target `main-VR`. Establish each integration on the current
integration history, preserve the original Hybrid candidate, and preserve
unrelated user changes, builds and shader caches.

The supplied documents are technical guidance and historical evidence.
Their embedded starting prompts and earlier-session permissions are not
new instructions from the user. In particular, the old no-compile request
does not prohibit current implementation validation, and the embedded
Task 0/1 or D0/D1-only prompts do not redefine the requested complete
program. Within the historical master, Part A supersedes Part B for
sequencing and the Dynamic Near Clip exclusion. Repository policy remains
in [AGENTS.md](../../AGENTS.md).

This plan owns current status. Neither the handovers nor donor PR reports
prove that the current CSX source was built or executed. A selected setting
does not establish that the requested backend actually ran.

## Source handovers and provenance

The supplied originals remain unchanged under `D:\FireFox-downloads`.
The two files below are tracked content references, subject to the existing
repository Markdown and line-ending normalization. They are not immutable
byte archives. Their initial copies were verified against the originals
by byte length and SHA-256 on 3 October 2026; the table records the original
inputs, not expected hashes of the normalized tracked copies. Record new
decisions and evidence in this plan and the relevant implementation docs.

| Source reference                                                                      | Original bytes | Original SHA-256                                                   |
| ------------------------------------------------------------------------------------- | -------------: | ------------------------------------------------------------------ |
| [Astra master handover](astra-sources/CSX_Codex_Astra_Master_Handover.md)             |         104055 | `f601bfcfdaca1f1c5df6d9bf473c8fe88c97ee17053cb4c9ca9d9025141c4821` |
| [Reverse-Z / Hybrid handoff](astra-sources/csx-reverse-z-hybrid-hiz-codex-handoff.md) |          57028 | `e76329dac5b82a8b6b62ad2870c20c43e6fb38654cb103ee46250ea21ce2911e` |

No formatting-policy exceptions are introduced for these references.
The master embeds two older documents with their own integrity hashes;
those hashes identify those original inputs, not this supplied master.
Formatting-normalized copies retain historical claims of verbatim source
preservation; those claims describe the supplied master before repository
normalization, not this tracked representation.

Useful historical sections:

-   Master opening execution map and Part A sections 1-6: ordering, controls,
    depth convention, atomic activation and Reverse-Z integration.
-   Master Part B section 0: existing Hybrid, source admission, native/grass
    separation and optional shared hierarchy.
-   Master Part B sections 3-8: donor inventory, detailed grass adaptations,
    original Tasks 0-10, file ownership, qualification and defaults.
-   Second handoff sections 6-13: native ABI, producer/replay ordering,
    temporal limits, state ownership, UI and telemetry.
-   Second handoff sections 14-18: historical build identity, evidence
    portability, focused checks, runtime acceptance and remaining risks.

## Current baseline and integration record

| Item                             | Recorded identity or status                                                                                                                                                                                                                                                      |
| -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fetched `main-VR` starting head  | `c3f028b5207a2d9a32d539127aad614d2eef640a`                                                                                                                                                                                                                                       |
| Working branch                   | `codex/astra-hiz-depth`                                                                                                                                                                                                                                                          |
| Worktree                         | `.tmp/worktrees/astra-hiz`                                                                                                                                                                                                                                                       |
| Preserved original Hybrid branch | `origin/codex/vr-hybrid-hiz-culling`                                                                                                                                                                                                                                             |
| Original Hybrid source           | `f17b833b56b5078527bb9a5b413e033455ad855d`                                                                                                                                                                                                                                       |
| Original Hybrid parent           | `dab1874a76fd39175dcefdc52110ba69d7284e12`                                                                                                                                                                                                                                       |
| Divergence at current fetch      | Hybrid is 1 commit ahead and 11 behind the fetched `main-VR` head                                                                                                                                                                                                                |
| Earlier master target            | `43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35`, historical                                                                                                                                                                                                                           |
| Integration operation            | Single existing Hybrid feature delta cherry-picked successfully as `29ce68539`; original branch unchanged                                                                                                                                                                        |
| Identified integration conflict  | `src/MenuDevBenchBridge.cpp`; retain current Sky Sync controls together with the Hybrid additions                                                                                                                                                                                |
| Integration source/Build ID      | Integration `29ce68539c1fa489b241135d1a7629ccddf6a4e3` plus reviewed working changes; final diagnostic DLL Build ID `8246ebf5ca62ab8ac0aefb6d620059f22b03f4dd2a3bb171e0ec2b578aea0169`; verified AIO Build ID `66b6efdee77bd3e03a565b8698cf395e332c44c4e62d0af479ad7a47ec152088` |
| Current runtime qualification    | Noon four-mode performance complete: Hybrid regressed; limited static review complete; motion/lifecycle qualification open                                                                                                                                                       |

The originally published Hybrid source itself had no post-rebase compile/test record.
The second handoff preserves clean pre-rebase source
`be8891ad0486909effb78cc4c47fbe8fcfa3b35f` and earlier dirty-build identities.
Those remain historical and cannot certify this integration.

### Implemented D0/D1 corrections

The integrated source now admits only a completed current render-target
publication and retains source SRV/generation attribution through preparation,
dispatch and delayed readback. Incomplete or replaced publications reject
admission; readable invalid history remains fail-visible. Snapshot phase
labels identify native-downscale and readback observation boundaries. They
do not establish depth-write timing, content freshness or camera freshness.
The broader content/phase proof required by shared grass Hi-Z remains open.

Hybrid retains the actual D3D device/context COM owners even when pipeline
creation fails. Recreate the pipeline and clear its failure latch only on
actual owner replacement or explicit shader-cache clearing. A same-owner
render-target publication change does not trigger compilation retries.
Dispatch rejects changed owners; pipeline recreation preserves pending
history long enough for fail-visible readback rejection.

The narrow `Common/DepthOrder.hlsli` helper centralizes near/far values,
nearest/farthest reduction and conservative biased comparison. Runtime
remains Standard Z; reversed arithmetic is exercised only by standalone
shader tests. The initial depth-order extraction preserved Standard Hybrid
bytecode across all 12 compared permutations. The subsequent finer-cell
refinement intentionally changes bounds-test behavior and has separate
WARP regression evidence. The inaccurate mask-shader Reverse-Z comment was
corrected, with all four compared mask permutations also identical.

The user additionally requires appropriate in-game DevBench diagnostics
and performance measurements. PR 1 must expose enough producer-attributed
snapshot/admission/pipeline status, reason counts and CPU/GPU measurement
evidence to evaluate actual execution, fallback, rejection and cost. All
new diagnostic/performance machinery must be behind
`DEVBENCH_BRIDGE_ENABLED` and absent from production compiler output;
runtime disablement alone is insufficient. The extensions now include bounded source observations, cumulative reason
counts, native and Hybrid CPU histograms, gated GPU scopes, explicit
measurement windows and a drained-writer indicator. The final universal
DLL and 11 focused tests passed. Production preprocessing and syntax
checks passed for all three affected translation units; a separate
production DLL link has not run.
The completed noon comparison records a Hybrid regression of
5.214575-6.729403 ms CPU and 1.893462-2.560592 ms GPU versus Advanced.
Both Hybrid windows ran the actual backend with all batches accepted and
zero recorded fallback/history rejection. Static stereo review is limited.
Four final motion ROI bursts of 160 consecutive frames each are verified;
all 640 original PNGs passed artifact checks. They cover 8.7075% of each eye.
The planned 64-step route appears at 5/9 checkpoints in Advanced, 6/9 in
Legacy and 9/9 in Hybrid/Off, so only common poses 0 through 32 support
four-mode comparison. Hybrid route deltas are +84 submitted, +20 accepted,
+64 `view_changed` invalidations and zero native fallback; changes in
observed cache `cameraAdjust` were recorded. Raw native PNGs open without
resizing. Sampled common-pose and adjacent-frame review found no obvious
culling holes or eye-specific geometry disappearance. This bounded result
does not qualify temporal behavior, physical head motion or the whole
image; fail-visible rejection limits inference about culling efficacy.
The [completed review and player](vr-hybrid-culling-runtime-2026-10-03.md#bounded-motion-review)
retain the exact selection and limitations.

New native result counts before/after recovery passed two focused tests
and ON/OFF syntax/isolation checks. They are absent from the original
measured DLL. Their separate universal DevBench ON AIO now links and passes
all 369-file archive/staging checks. Compiled source is
`c684ff32c9f75c97f6743fe2829ca7eb040525f2`, dirty digest
`92ea1d0bb80e669a84b01843dd8cb1ed4403257ddd298e60051c43cb01839c86`;
Build ID is
`ee0c10f34abe0a5a7197ce4f77436273355c80b1c72747deb7a9331d9a44d5c3`.
The 90,846,788-byte archive has SHA-256
`c4b48a061544cb84a45c58189d51803368a00dce5078c8a18746e954618b9449`.
After the user's installation/restart, the
[native-count campaign](vr-hybrid-culling-native-counts-2026-10-03.md)
verified that identity and completed all four modes. Native Advanced and
Legacy rejected about 61-62% of observed results; Hybrid rejected about
19%, with all observed Hybrid batches accepted and no fallback. Separate
telemetry-disabled timings are descriptive only: the user confirmed a
concurrent DLL build, and simulation-clock progression differed between
windows. Those timings are superseded by the linked final projected-face
comparison; the first quiet preflight correctly stopped on build activity. Preserve both measured identities;
the changed player position prevents a matched cross-build comparison.

For measurement windows, capture the coherent bounded source payload before
disabling telemetry; disabled telemetry hides it. Wait for admitted writers
to finish before reading stable cumulative totals. Counters and stage bins
are individually atomic rather than a single aggregate snapshot, and a
pending pre-reset submission can read back after reset. Do not require exact
submitted/readback cohort conservation across that boundary.

## PR boundaries and original task mapping

The [projected-face refinement](vr-hybrid-culling-faces-2026-10-04.md)
addresses both rectangular over-coverage and the whole-box nearest-depth
limitation. Its runtime comparison still regressed. The subsequent
adaptive traversal also trails Advanced in the repeated runtime test;
its measured bounds-testing cost is the next optimization target. Grass
and renderer depth changes remain later PRs.

PR labels below denote workstreams, not assigned GitHub numbers. Add the
actual number/link after creation and follow the repository title/branch
identity rules. Do not invent a number or rename an open PR's branch.

| PR/workstream                          | Master stage         | Original detailed tasks                             | Boundary and exit condition                                                                                                                              |
| -------------------------------------- | -------------------- | --------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1: Hi-Z and depth culling              | D0, D1               | 0; native portion of 7a; applicable controls from 9 | Current-main Hybrid integration, narrow depth contract, exact-source checks and attributed native qualification; Standard Z and Advanced remain defaults |
| 2: PBR grass                           | G1                   | 1; material/cache/UI portions of 9                  | Authored PBR grass works through the existing renderer; basic/complex and disabled paths remain valid                                                    |
| 3: Grass optimization                  | G2                   | 2-6; activation/cache prerequisites from 9          | Correct opt-in stereo batching/frustum path with explicit parity mode and complete recovery                                                              |
| Additional: optional grass Hi-Z        | H1                   | Remaining cross-consumer 7a, then 7b-7c             | Reuse the reduction foundation with proved snapshot admission; preserve separate native history and grass compaction                                     |
| Additional if useful: quality controls | Quality stage        | 8; remaining applicable 9                           | Stereo-consistent density/LOD/shading controls with savings reported separately from parity gains                                                        |
| 4: Reverse Z                           | R1                   | Part A 4-6 plus the current-code audit below        | Coordinated optional scene-depth conversion and consumer updates; Standard Z baseline; initially restart-required                                        |
| Qualification for each workstream      | Stage-specific gates | 10 and relevant 9                                   | Exact evidence and rollback for the feature being proposed; repeat combined checks when dependencies change                                              |

Follow the requested serial preference. If Hybrid runtime qualification
finds an unresolved defect, retain that experimental work separately and
continue independent PBR material work. Neither PBR nor batching/frustum
grass requires successful native Hybrid qualification. A separately tested
reduction shader is not qualification of native delayed-result handling.

## Shared implementation boundaries

-   Keep depth representation, native object-culling method, grass renderer,
    grass occlusion and material selection independent.
-   Retain Advanced/Legacy/Hybrid at normal Info visibility, the central
    method setter, epoch invalidation, DevBench controls, effective backend
    and explicit fallback reasons. Advanced remains the default.
-   Standard Z stays active until all R1 requirements are satisfied. Reverse
    Z is a separate renderer mode, not a fourth culling method.
-   Exclude Dynamic Near Clip entirely: no controller/probe/readback, adaptive
    near writes, hooks, settings, fog suppression or new dependency. Keep
    existing clipping distances and fog behavior, ordinary conservative
    near-plane handling, and necessary mask/native-occluder corrections.
-   Preserve CSX lighting, wetness, material, deformation, render-scale,
    resource-generation and shader-cache ownership. Do not import the donor
    shared wind/collision replacement or another render-scale controller.
-   Name graphics resources through the existing utility and restore D3D and
    ImGui state deterministically. Keep native engine buffers engine-owned.
-   New controls require the matching DevBench action, description/schema,
    settings validation, requested/applied diagnostics and save behavior.
    Compile developer-only capture/measurement code out of production.
-   Keep conservative failure behavior explicit. Unknown native occlusion
    retains visibility when the result array is writable. Missing grass depth
    disables only grass occlusion; an invalid optimized representation needs
    complete original-renderer recovery.

## D0: baseline and narrow depth contract

Inventory actual resource owners and consumers before sharing depth or
changing its convention. Prefer existing renderer ownership and accepted
resource/temporal state over a parallel framework. The typed read-only
snapshot must identify:

1. Retained resource/view lifetime, actual resource and view formats,
   encoding/convention, sample count and near/far semantics.
2. Resource generation and distinct content generation, producing frame,
   render phase and camera/projection identity, including jitter convention.
3. Allocation size, active source rectangles for each eye, eye layout and
   any nonlinear/foveated mapping.
4. Mask provenance/validity, relevant occluder coverage and bounded lifetime.
5. Whether the requesting consumer can prove the snapshot is suitable.

An unchanged SRV, frame number or texture name is insufficient. Current
`Deferred::CopySceneDepth` can replace `kPOST_ZPREPASS_COPY` contents later
in the same frame. The generic current-scene accessor can select Terrain
Blending depth before final scene depth is published. Preserve the native
Hybrid boundary's exact source admission.

`Util::GetTexture2DDesc` and `TryGetDepthSrvDimensions` already provide
resource inspection. `DetectVRDepthLayout` uses tolerant ratios and broad
fallbacks; it is not sufficient evidence for conservative culling. A
different utility name alone does not justify another implementation.

Add tested depth-ordering helpers only where new/changed culling needs them.
Represent standard and reverse ordering without activating Reverse Z or
performing a repository-wide behavior-preserving rewrite first.

## D1: existing Hybrid integration and qualification

Status: implemented in [PR 104](https://github.com/ParticleTroned/skyrim-community-shaders/pull/104),
with the first four-mode noon assay complete. Performance neutrality failed
in that fixture; static sampled fidelity is provisionally clear of large
defects, while motion/lifecycle correctness remains unqualified. Diagnose
native-versus-Hybrid rejection efficacy without loosening conservative
history or depth admission. See the [runtime evidence](vr-hybrid-culling-runtime-2026-10-03.md).

The candidate retains native affine OBB collection, per-submission result
indices, the existing result/staging buffers and delayed CPU consumption.
It replaces only admitted depth-reduction/visibility production. The native
limit is 4096 objects; it must not become a grass-instance limit or a stable
cross-frame identity.

Preserve these implementation contracts while reconciling current main:

-   Run the outer native downscale routine for its camera/depth-ready side
    effects. Suppress only the verified inner draw after successful Hybrid
    preparation. A later failure must replay that draw under bypass before
    native production and Advanced recovery. Retire suppression once.
-   Check the native batch's pointers, count, selector, upload state, resource
    identity/shape and exact submitted bounds before accepting results.
    Inspect/correct the array before the native caller resets the batch.
-   Maintain per-eye conservative coverage, all relevant source samples,
    far padding, complete query rectangles, both-eye occlusion requirement,
    large-world subtraction and near-plane failure-open behavior.
-   Preserve camera/world/eye/projection/epoch admission and exact one-frame
    history age. A readable invalid batch becomes all-visible; an unreadable
    batch records that limitation and retires history.
-   Small-motion limits and moving occluders remain acceptance gaps. Do not
    loosen thresholds or add selective history reuse without a defensible
    disocclusion/coverage argument and measured evidence.
-   Native staging avoids an extra readback but its `Map(READ)` can block.
    CPU dispatch timing is not GPU duration. Existing nested profiler scopes
    must not be summed as independent costs.
-   Keep strict depth/layout admission, shader-failure latching, isolated
    graphics state, exact replay ordering and producer-attributed diagnostics.
    Additional optimizations require a measured cost or concrete defect.

Build the exact integration and rerun the focused controller/WARP/FXC
checks. Exercise Advanced, Legacy, Hybrid and native-disabled configurations
with grass optimization absent/off. Verify actual effective backend,
producer identity, fallback and method-round-trip behavior.
Use all four conditions for full qualification; the user's current focused
optimization comparisons use Advanced and Hybrid only. Reset and verify noon before
each condition and each separate capture phase, then settle five seconds.

Runtime evidence must include both eyes, stationary and moving views,
doorway/occluder edges, near/distant geometry, moving occluders, loads/cell
changes and supported scaled/upscaled layouts. Observe cache invalidation,
failure replay and state restoration. Real-HMD correctness remains a
separate gate from null-driver automation.

Report correctness, run health, fallback frequency and performance
separately. A correct opt-in experiment can remain experimental when some
scenes do not improve. Claim neutral-or-better only with repeated matched
measurements; do not use missing geometry or recovery-induced extra draws
to conceal the actual result. Do not promote Hybrid by default on the
basis of a successful compile or a menu setting.

## G1: PBR grass on the existing renderer

Port the complete material-construction, render-pass/technique, constant,
descriptor, resource-binding and shader contract. Populate all material
members before interning. Preserve the diffuse selected by the grass form
and supply valid normal/RMAOS/subsurface defaults. Verify VR hook offsets
and vtable/callsite assumptions rather than copying SE/AE relocation pairs.

Preserve basic and complex grass and define behavior when either global
True PBR or Grass Lighting is switched off after PBR materials exist.
Keep valid constant/output layouts and clear or safely bind optional
resources; never depend on the preceding material's bindings.

Retain semantic color/ambient balance, Wetterness, optional Foliage
Lighting, Skylighting and CSX shadow ownership. Assign transmission and
foliage-scattering ownership explicitly to avoid double-counting. Do not
reintroduce the removed vanilla dimming or obsolete shadow clamp behavior.

Reflect and verify GrassPS offsets, PS `b1`, texture/sampler ownership,
PBR-only reflectance output, and unchanged VR PerGeometry layout. Shader
cache identity must include actual technique/define/ABI generation.

Exit checks include two authored materials sharing diffuse but differing
in other maps, alternate draw/load orders, optional textures, basic/complex
unchanged behavior, runtime-off paths and agreement of depth/color passes.
PBR shading cost is measured independently from subsequent batching gains.

## G2: parity-mode stereo grass optimization

Establish activation/cache/recovery before enabling optimized drawing.
Keep Hi-Z, density removal, minimum-pixel rejection, mesh-cost thinning,
mesh LOD and simplified shading off in explicit parity mode. Retain
original distance/fade, materials, population and deformation.

The implementation must include:

-   Immutable staged captures, removal invalidation and serialized queue
    ownership. Carry both donor lifetime fixes; consuming a deferred capture
    must not dereference a destroyed shape. Prove material lifetime/identity
    across queue swap, reload and address reuse or retain immutable identity
    with an appropriate generation.
-   Material/mesh-safe buckets, descriptor-specific layouts, checked capacity
    arithmetic for both eyes/all bins/offsets, valid oversize splitting or
    fallback, and complete count resets. Never clamp away an instance tail.
-   Corrected absolute-instance to eye-relative projection, matching previous
    coordinates, per-eye survivors/indirect arguments and explicit extras
    eye-slot base. One decoded instance may evaluate both eyes; each eye keeps
    its own frustum/occlusion result.
-   An adapter for CSX's existing collision field. Keep the legacy wrapper's
    `World[0]` transform, and allow optimized positions already in the required
    coordinate frame to enter the displacement evaluator once. Preserve
    current/previous samples, alpha weighting and existing VS wind behavior.
-   Shared actual-matrix CPU/GPU frusta, consistent coordinate conversion,
    binocular coarse rejection and bounds covering model-center offsets,
    scaling, deformation, jitter envelope and selected meshes. Missing camera
    proof skips rejection. Check representative-shape/native rejection cannot
    suppress a cross-cell bucket outside that shape's local bounds.
-   Requested/prepared/applied state; resource/shader-generation preflight;
    transactional draw representation, both engine patches and permutations;
    complete original-mode restoration. A vanilla draw helper alone is not
    safe with optimized shaders or the fade-buffer skip patch still active.
-   Checked input-layout and Map failures, shader-failure latches until an
    explicit generation/reload action, retained captures for recovery and
    correctly isolated shared compute state. Log operational failures once
    per relevant generation instead of entering per-frame retry storms.

Both eyes must agree on placement, deformation, depth and motion vectors
away from the world origin. Test nonzero second-eye base, unequal survivor
counts, asymmetric projections, outer edges, near-plane/cell boundaries,
empty/large buckets and load/unload/re-enable without a forced cell reload.
Fault injection must recover without stale-eye draws or permanent grass
loss. Keep the culler within D3D11's eight-UAV budget.

## H1: optional shared grass Hi-Z

Extract the existing conservative reduction foundation only when the grass
consumer requires it. Native routing/readback/history stays private;
grass compaction remains GPU-owned without a new CPU visibility readback.

Capture actual CSX ordering. The native `POST_ZPREPASS_COPY` snapshot and
the first-grass live `MAIN` snapshot can differ in content, camera phase
and availability. Reuse one built immutable pyramid only for equivalent
admissible snapshots. Otherwise build separate attributed contents through
the same builder and measure that cost, or disable grass occlusion.
Never silently reuse last frame's pyramid.

Adapt the donor's `Texture2D` SBS ABI to the two-layer eye-local view as a
single declaration/binding/coordinate/dimension change. Keep grass's own
frustum, deformed-bound, instance, counter, LOD and per-eye survivor logic.
Do not use the native union-result OBB shader as the complete grass culler.

Qualify Advanced/Legacy/Hybrid/native-disabled independently against GO and
grass-Hi-Z requested states. A grass-only source must not enable native
culling merely to trigger Terrain Blending's alias guard. Missing depth
turns off occlusion while retaining valid optimized drawing. Unsupported
layouts report their reason and retain visibility.

## Grass quality controls

Introduce density/minimum-size/mesh-cost/LOD/simple-shading changes only
after parity is sound. Use stable original-instance identity for stochastic
choices. Shared randomness is insufficient when each eye independently
crosses a size or distance threshold: choose a conservative binocular
quality metric, then apply independent visibility rejection per eye.

Keep compatible materials for LOD meshes, preserve the full mesh when an
optional asset is missing, and size memory from the actual compacted ABI.
The historical donor's wind extras are not mandatory for CSX. Report
capacity/high-water use, quality settings and savings independently.

## R1: coordinated optional Reverse Z

First finish the consumer audit below. Use Bottle's conversion/state design
and Open Shaders' VR corrections as selectively reviewed sources; do not
stack independent camera/D3D detours or copy a donor default-on policy.
The native Hybrid replay path and reversed downscale/proxy correction need
one dispatch owner and a verified convention-compatible fallback.

Preflight required textures/views, shaders and consumers, then publish one
consistent depth-convention/resource generation. On failure keep the last
complete Standard-Z configuration; do not claim active Reverse Z after
converting only some targets. Track resource convention by ownership and
generation, not float format or a potentially reused raw DSV address.
Explicitly standard shadow/cubemap resources remain distinguishable.

| Operation                        | Standard Z                  | Reverse Z                   |
| -------------------------------- | --------------------------- | --------------------------- |
| Near / far                       | 0 / 1                       | 1 / 0                       |
| Farthest coverage reduction      | Maximum                     | Minimum                     |
| Unknown/far conservative padding | 1                           | 0                           |
| Nearest object depth             | Minimum                     | Maximum                     |
| Occlusion comparison             | `nearest > farthest + bias` | `nearest < farthest - bias` |

Coordinate projections, inverses, clears, depth/raster states and bias,
viewport intervals, comparisons, reconstruction and upscaler inputs.
Preserve stencil where needed when widening to D32/S8. Keep raw reversed
float precision in the hierarchy; scope conversion only to consumers that
require it. A raw zero can be far background under Reverse Z and must not
automatically identify the hidden-area mask.

### Current-code depth-consumer audit

These are observed integration surfaces, not implemented Reverse-Z fixes.
Before R1, add each touched owner's final admission/convention behavior,
source/format, generation, tests and evidence to its implementation record.

| Owner / concrete source                                                                                                | Observed contract and required audit                                                                                                                          |
| ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/Deferred.cpp`: `CopySceneDepth`, `IsSceneDepthFinal`, water fallback                                              | `POST_ZPREPASS_COPY` can receive completed opaque depth later in the frame; retain content/phase identity and compatible copy formats                         |
| `src/Utils/D3D.cpp`: `GetCurrentSceneDepthSRV`, dimension/layout helpers                                               | Source selection changes with final-depth state and Terrain Blending; heuristic layout detection does not prove conservative culling admission                |
| `src/Features/VRHybridCulling.cpp`, policy/history, three production shaders                                           | Current gate accepts standard R24/R32 views; R32_FLOAT_X8X24 admission needs coordinated reduction/mask/clip/comparison semantics, not a relaxed format check |
| `src/Features/VRDepthCullingTemporal.cpp` and lifecycle helper                                                         | Preserve native hook chaining, delayed results and replay; fallback must use the active convention's downscale and occlusion proxy                            |
| `src/Features/TerrainBlending.cpp`, `features/Terrain Blending/Shaders/TerrainBlending/DepthBlend.hlsl`                | Alias/backup ownership and R32/R16 derivatives; shader currently chooses `min(main, terrain)`; retain replay/depth-test policy                                |
| `src/Utils/Game.cpp`, `package/Shaders/Common/SharedData.hlsli`                                                        | CameraData near/far coefficients and shared linearization; maintain raw-depth versus metric-depth distinction                                                 |
| `features/Upscaling/Shaders/Upscaling/ClearHMDMaskCS.hlsl`                                                             | Raw near-zero hidden-depth test and dilation; misleading reversed-Z comment corrected with identical DXBC; separate mask evidence from scene ordering         |
| `features/Upscaling/Shaders/Upscaling/EncodeTexturesCS.hlsl`                                                           | Raw-zero mask detection, nearest-neighbor depth comparison, linearization and depth output to temporal consumers                                              |
| `features/Upscaling/Shaders/Upscaling/DepthRefractionUpscalePS.hlsl`                                                   | Minimum-depth 2x2/3x3 selection and distinct SAO camera-Z output; define nearest ordering and destination convention together                                 |
| `features/Upscaling/Shaders/Upscaling/PeripheryTAACS.hlsl`                                                             | Minimum-depth neighborhood, far rejection near 1, padding 1 and matrix reconstruction; update history validity and convention identity                        |
| `CameraMotionVectorsPS.hlsl`, `CopyDepthToSharedBufferPS.hlsl`, per-eye copy/encode host paths                         | Direct raw-depth copy/unprojection must match the matrix and actual SDK input convention; inspect every intermediate rather than infer from its name          |
| `src/Features/Upscaling/Streamline.cpp`                                                                                | Currently publishes `depthInverted=false`; match per-eye depth, near/far, projection/inverse and retained temporal snapshot                                   |
| `src/Features/Upscaling/FidelityFX.cpp`                                                                                | Audit each FSR context creation flag, depth resource/encoding and dispatch near/far contract, including fallback paths                                        |
| `src/Features/Upscaling/VRSubmitTemporalSnapshot.h`, accepted render-scale/resource ownership                          | Reuse existing frame/generation/method/dimension/compositor-cycle contract; invalidate history when convention changes; no competing controller               |
| `src/Features/ScreenSpaceShadows.cpp` and its shaders                                                                  | Explicit near=0/far=1 plus Terrain Blending selection and stereo reconstruction                                                                               |
| `src/Features/ScreenSpaceGI.cpp` and its depth/reprojection shaders                                                    | Depth source/format, hierarchy ordering, linearization, temporal rejection and stereo reconstruction                                                          |
| Water/refraction, UnderwaterDepthOfField, volumetrics and depth-sensitive lighting                                     | Preserve clipping/fog policy while making comparisons, reconstruction and copied-depth contracts consistent                                                   |
| Main/copy/decal/post-prepass/post-water resources, masks, native fallback, shadow/cubemap exceptions, external effects | Enumerate all required resources and state hooks before atomic activation; audit externally expected standard-depth conversion separately                     |
| PBR and optimized grass, optional shared Hi-Z                                                                          | Material independence; convention-correct clip fading, source admission and reduction; retain parity and both-eye drawing                                     |

The list is a concrete starting inventory, not a claim of exhaustive
coverage. Search actual code for depth reads/writes, compare functions,
clears, format construction, `SV_Depth`, reconstruction and SDK contracts
before implementing R1.

R1 validation includes both conventions, native fallback, culling/grass
combinations, masks, water/fog, per-eye matrices/inverses, cold/warm caches,
resize/load/recovery, external effects and supported SE/AE paths. Compare
the same binary with controlled restarts and reproducible saves/settings.
Record resource bytes/VRAM and whole-frame CPU/GPU behavior. Real-HMD motion
and performance evidence are required before any default promotion.

## Donor selection and attribution ledger

The complete historical inventory is preserved in master Part B section 3.
Re-resolve moving refs and inspect relevant newer deltas before each stage.
An ancestor/merge record is not proof CSX retained a selectively deferred
feature. Use patch comparison and actual current code; preserve verified
authorship for material contributions.

| Reference                               | Pinned source / treatment                                                                                                                              |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Existing CSX Hybrid                     | `f17b833b56b5078527bb9a5b413e033455ad855d`; preserve original branch and original author                                                               |
| Historical upstream grass donor         | `97a7db04f50643efce30e8632a32082b8eebcd3f`                                                                                                             |
| Historical OS VR grass donor            | `d6bfd6b7b552e70d23eec00fce4e7f14cbece0af`                                                                                                             |
| PBR material/technique foundation       | UP #2709 `2d68ed181ada895807ac87fdaaa9e1b83ceb01bd`                                                                                                    |
| Grass batching / UAV correction         | UP #2688 `dc11b1af4082ff3200e0518cf463a8b07dc735ca`; #2706 `10ec5fca6d78c942f466c43859430689405105c8`                                                  |
| Compile failure hardening               | UP #2781 `e66deb7ab31134b782147cf32865ea052bcddbbd`; includes broader complex-detection and hierarchy latches                                          |
| Final VR projection                     | OS #817 `8444f3c628feaa51e3de0415f83c29269ca08e04`; assess together with #752                                                                          |
| Complete enabled transition             | OS #816 `b0f16fd4c292704c4f09591241eeaf5d687f8c7f`; extend for CSX readiness/generation/recovery                                                       |
| Capture lifetime fixes, both required   | OS #815 `404024aea2c2376343febf2fcc1084d837d2f98b`; #822 `f938b5344e2a321f74ad1d470070cbd5c198f77a`                                                    |
| Shared compute-state preservation       | OS #726 `6150e8d39ea9b096d35c221d9f1aa19f80608844`                                                                                                     |
| Correct VR combined behavior            | OS #648/#663/#752; retain corrected eye storage/layout, capacity and projection rather than initial #630 behavior                                      |
| Grass lighting follow-ups               | OS #677/#680/#686/#810 and UP #2705/#2716; integrate with CSX lighting ownership; preserve #757 transparency revert                                    |
| Draft/incomplete proposals              | Historical OS #653/#812 and UP #2804/#2810 statuses are not fresh metadata or validation; use diagnoses selectively and avoid duplicate ports          |
| Historical Reverse-Z VR donor           | OS #818 `188c80060a186fc0dbb293eeca45fac58d6554bf`; current status and drift require re-resolution                                                     |
| Bottle snapshot in master               | `3ab152056eb553408429cfa72bf18f7892050349`, `Bottle-Compendium`                                                                                        |
| Later Bottle snapshot in second handoff | `68356b57f624eacf98033632500724c0d0cf66a1`, recorded 3 October 2026 17:28:40 UTC; inspect drift from the earlier snapshot                              |
| Bottle design lineage                   | `6db6512c96`, `0a9f8f8ac0`, `42d5129c75`, `9a6d3b2426`, `1e88b27471`, `ca51444e8a`; review relevant final behavior instead of replaying whole branches |

For each applied contribution, add exact donor commit, affected hunk/file,
retained CSX differences, verified author, resulting source identity and
validation. Keep unrelated broad-merge changes out of the feature PR.

## Original task register and exit gates

All original task identifiers remain available for cross-reference.
Detailed original requirements remain in master Part B sections 4-7.

| Task                             | Current state                                                                                                           | Required exit gate                                                                                                            |
| -------------------------------- | ----------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| 0: baseline and ledger           | In progress                                                                                                             | Exact source/build identity, current-main worktree, donor/hook/material/permutation inventory and reproducible baseline       |
| 1: PBR without GO                | Planned for PR 2                                                                                                        | Distinct authored materials, valid disabled/basic/complex paths, consistent depth/color and runtime settings                  |
| 2: geometry/deformation contract | Planned for PR 3                                                                                                        | Documented coordinate spaces; legacy no-op shader evidence where applicable; matching CPU/VS/collision fixtures               |
| 3: capture/bucket infrastructure | Planned for PR 3                                                                                                        | Destruction/queue-swap/material identity tests and failure-safe capacities while original rendering remains valid             |
| 4: conservative stereo drawing   | Planned for PR 3                                                                                                        | Correct both-eye placement/depth/deformation with actual descriptors, eye bases and matched parity settings                   |
| 5: unified conservative culling  | Planned for PR 3                                                                                                        | CPU reject implies both-eye rejection of represented geometry; asymmetric-HMD/near-plane/off-center bounds checks             |
| 6: activation/recovery           | Design prerequisite for Task 4                                                                                          | Transactional patches/shaders/applied state; every injected failure yields a known valid renderer                             |
| 7a: Hybrid foundation            | Integrated in PR 104; noon performance regressed; static review limited; motion/lifecycle and cross-consumer gates open | Exact-source build/tests plus attributed native runtime/fallback/history and later source ordering for grass                  |
| 7b: shared Hi-Z provider         | Planned for additional H1 PR                                                                                            | Bounded attributed snapshots, independent demands, WARP coverage and no stale/wrong-phase or duplicate same-content build     |
| 7c: grass Hi-Z consumer          | Planned after 7b                                                                                                        | Full independent native/grass control matrix, array ABI, eight-UAV budget and valid missing-depth drawing                     |
| 8: quality controls              | Planned after parity                                                                                                    | Stable stereo/temporal choices, valid LOD fallback and separately reported quality changes                                    |
| 9: UI/cache/profiling            | Distributed across owning feature PRs                                                                                   | Valid settings/reset/malformed input, DevBench schema/actions, requested/applied state and cold/warm/managed cache identity   |
| 10: qualification/release        | Applied per stage; not complete                                                                                         | Shader/configuration coverage, affected-runtime checks, real-HMD evidence, lifecycle soak, measured CPU/GPU/VRAM and rollback |

## Validation and evidence record

The [current guarded 2x2 analysis](vr-hybrid-culling-guarded2-analysis-2026-10-04.md#evidence-and-validation)
preserves measured source `045fe4e8c`, producer identity, eight noon timing
windows, separate complete GPU captures and frozen counters. It distinguishes
the earlier measured DLL from the subsequent guarded-only/copy-optimization
source. Guarded-only removal passed 12/12 focused tests, four maintained
and four strict shader equivalence comparisons, and production isolation
for four translation units. The subsequent private-vertex change passed
12/12 focused tests and all four strict copy-removal/reflection checks;
its bytecode intentionally differs. Exact commands and evidence are in
the linked report. A separate production DLL link, SE/AE runtime checks and
motion/lifecycle qualification remain open.

The [integration record](vr-hybrid-culling.md#current-main-integration-2026-10-03)
and [original noon report](vr-hybrid-culling-runtime-2026-10-03.md) retain
historical commands, intermediate artifacts and original-build visual
evidence. The table below describes that integration stage, not the
current test count or performance verdict.

The three documentation files passed scoped `trailing-whitespace`,
`mixed-line-ending` and `prettier` hooks. All five relative links in this
plan resolved. The supplied originals retained their recorded hashes after
formatting the tracked references. The initial all-hook documentation
invocation stopped while initializing an unrelated local Python environment
because its user-cache lock was inaccessible; the three applicable hooks
were then run individually. `dev-doctor.ps1 -Network` reported no failures
and a GitHub CLI authentication warning, with SSH origin access passing.

Use the maintained Git/CMake/pre-commit wrappers and scoped checks. The
focused Hybrid suite consists of the depth-culling settings/UI/telemetry,
temporal, Hybrid policy/history and production-shader WARP tests. The WARP
test is a controller target; disabling the broad shader test group does
not replace or remove that requirement. Validate production compute
shaders with the documented strict FXC configurations.

The full local validation entry point is `tools/validate-local.ps1`.
Record actual commands/toolchain, source/dirty digest, manifest Build ID,
DLL SHA-256/size, passed/failed checks and preserved evidence locations.
Syntax coverage is not a full non-DevBench DLL link; WARP is not hook,
native readback, state restoration, camera-cache freshness or HMD evidence.

Before runtime attribution, match the built artifact with the exact enabled
AIO's physical DLL, adjacent manifest, build receipt and runtime producer.
Check enabled loose providers, Overwrite and unmanaged Data using bounded
inspection. Follow installed automation skills for any deployment/session
actions. Preserve supplied logs before analysis under the user-specified
archive policy.

If a PR changes or evaluates VR render-scale behavior, apply the current
[render-scale qualification](render-scale-pr-qualification.md) and
[comparison reporting](vr-render-scale-comparison-reporting.md) contracts,
including exact ledgers where required. Do not claim this plan, static
source review or focused WARP validation satisfies those protocols.

| Evidence                                    | Historical integration status                                                                                                                                                                                                  |
| ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Supplied source provenance                  | Passed: initial copies matched the recorded original byte lengths and SHA-256; tracked references use repository formatting normalization                                                                                      |
| Cross-document scope/sequence audit         | Completed; obsolete prompts and differing donor snapshots identified                                                                                                                                                           |
| Current-code depth-consumer reconnaissance  | Completed for the concrete surfaces listed above; exhaustive R1 audit remains open                                                                                                                                             |
| Current-main Hybrid integration             | Integrated as `29ce68539c1fa489b241135d1a7629ccddf6a4e3`; publication/SRV and D3D-owner corrections implemented; final diagnostic artifact recorded in the Hybrid document                                                     |
| Integration changed-file hooks              | Passed except gersemi skipped after proposing an unrelated 438-line CMake baseline rewrite; that rewrite was restored                                                                                                          |
| Exact integration DLL build                 | Final universal ALL Release DLL passed (SE/AE/VR, DevBench ON, Tracy OFF); manifest and artifact hash/size verified                                                                                                            |
| Focused controller/WARP tests               | Final pass: 11/11 in 3.75 seconds, including diagnostic serialization, concurrent admission/snapshots and Standard/reversed WARP                                                                                               |
| Standard shader equivalence                 | Passed: 12/12 Hybrid and 4/4 mask DXBC comparisons identical against integration `29ce68539`; separate strict-FXC sweep not run                                                                                                |
| Production without DevBench validation      | Passed: actual-flag OFF syntax/preprocessor audit for Hybrid, Temporal and Menu bridge; 29 markers absent per TU after native-count follow-up; no separate OFF DLL link                                                        |
| DevBench AIO archive                        | Original archive preserved; native-count AIO 90,846,788 bytes, 369-file verification passed; installed physical DLL/manifest/receipt match runtime identity                                                                    |
| Integration deployment/runtime identity     | Runtime receipts match original AIO Build ID/source/hash; measured identity retained in the noon report                                                                                                                        |
| Native/HMD fidelity, fallback and lifecycle | Static and common-pose motion ROI samples show no obvious defects; 640 originals verified; unequal route coverage, temporal and lifecycle gates remain open                                                                    |
| Comparable CPU/GPU/VRAM performance         | Original eight noon windows show regression; later native-count-build timings are descriptive because concurrent compilation was confirmed; projected-face repeat also regressed; adaptive still regressed; VRAM not qualified |
| PBR/optimized grass/Reverse-Z qualification | Not started                                                                                                                                                                                                                    |

For every measurement report backend admission, requested/effective mode,
sample counts, warmup/transition boundaries, exact configuration and source
identity. Use repeated matched scenes, population/materials, weather,
camera routes, render scale and telemetry state. Report CPU/GPU, p50/p95/p99,
spikes, relevant pass costs, survivors/draws, fallback/invalidated/promoted
counts and VRAM. Compare changes with the headset's actual frame budget
and baseline variance. Keep correctness, completion/health and performance
as separate results.

## Next bounded work

Keep PR104 experimental and pursue guarded 2x2 only. Next compare the
indexed-face/alternating-clip build with Advanced at noon. Existing GPU
scope timers and rejection counters are sufficient for that first decision;
additional diagnostic captures are not required. Keep traversal/shadow
diagnostics separate from timing.

The [ordered investigation](vr-hybrid-culling-guarded2-analysis-2026-10-04.md#ordered-follow-up)
retains matched native/Hi-Z outcomes and selective source-depth refinement
for the rejection gap. If bounds cost still dominates, measure clip-plane
work before skipping already-containing planes or caching repeated planes.
Eagerly preparing all twelve planes would exceed the observed plane-attempt
count by more than threefold. Shared quad proofs need numerical residual
bounds before replacing the guarded triangle tests. Distinguish
useful hidden geometry from zero-fragment/offscreen counts. Do not raise
the read budget: every observed budget exit could recover at most 0.278
percentage points in the earlier diagnostic cohort. No measured occupancy
or spilling diagnosis exists. Preserve masks, guards, depth allowance and
stereo/history safety; require a repeatable improvement and motion/lifecycle
qualification before promotion.

PBR grass remains the next independent feature PR, followed by grass
optimization and Reverse Z. No render-scale qualification is claimed.
