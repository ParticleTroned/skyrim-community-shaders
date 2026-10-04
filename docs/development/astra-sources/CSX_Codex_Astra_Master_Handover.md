# CSX — Codex / Astra master handover

Consolidated 3 October 2026 from the two existing implementation documents.

**Purpose:** provide one portable Markdown handover containing the detailed grass plan, the Hybrid integration amendment, and the latest landing-order / Reverse-Z decisions, including the complete exclusion of Dynamic Near Clip.

This consolidation does not add a new repository review or claim that any code was implemented, built or tested. The reviewed hashes, donor statuses and runtime evidence in the source documents remain historical snapshots. Re-resolve the relevant refs and inspect current code before implementation.

## Read this first: authority and scope

This opening guide is editorial navigation, not additional source-code evidence. Both source documents are reproduced in full below, without changing their text.

**Apply the latest decisions in Part A over earlier sequencing and scope statements in Part B.** In particular:

-   Prefer qualification of the existing switchable Hybrid candidate first, under Standard Z. Keep Advanced the default. An unsuccessful Hybrid qualification must not force an unsafe merge or block independent PBR work.
-   Then implement PBR on the existing renderer, parity-mode optimized stereo grass, and optional qualified shared grass Hi-Z. Full Reverse Z follows as a separate, initially restart-required mode.
-   Exclude OS Dynamic Near Clip entirely—not just default-off. Preserve CSX's existing clipping distances and fog policy. Retain ordinary conservative near-plane handling and necessary Reverse-Z native occluder/mask corrections.
-   Preserve CSX's renderer, material, deformation, render-scale, cache and resource-generation ownership. Do not import a second wind system or combine the donor camera/D3D hook systems indiscriminately.
-   Do not treat previous source-review findings, donor tests, or pre-rebase evidence as validation of the current CSX implementation.

The authoritative starting prompt is the one in this opening guide. The earlier prompts embedded below are retained for provenance; apply them only consistently with Part A. Repository instructions such as AGENTS.md and applicable local instructions must also be read before edits. Do not modify policy documents merely to make the port easier.

## How detailed is this handover?

| Area                        | What is already specified                                                                                                                                                                              | What remains implementation work                                                                                                                                                                                                            |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PBR and Grass Optimizations | Commit inventory; tasks 0–10, including 7a–7c; file ownership; material and shader contracts; stereo/deformation requirements; failure paths; test matrix; defaults and history-verification commands. | Inspect current code and drift, implement the scoped changes, and produce exact build/test/runtime evidence.                                                                                                                                |
| Existing Hybrid candidate   | Candidate identity and evidence limits; qualification steps; native-versus-grass responsibilities; depth-snapshot requirements; reuse boundaries; toggle and benchmark expectations.                   | Reconcile with current main-VR and execute the qualification. The plan does not establish runtime correctness.                                                                                                                              |
| Full Reverse Z              | Landing position; Bottle/OS source-selection rationale; required format/order/mask changes; atomic activation and native fallback requirements; toggle policy and acceptance gates.                    | This is an architecture/integration plan, **not yet an exhaustive per-file patch specification for every CSX depth consumer**. Expand the current-code consumer audit into reviewable implementation subtasks before changing the renderer. |
| Dynamic Near Clip           | Explicit excluded components, rationale and acceptance boundary.                                                                                                                                       | Verify that donor selections do not introduce its hooks, probes, settings, adaptive camera writes or fog suppression. No redesign of that feature is in scope.                                                                              |

This is enough to begin the baseline and existing-Hybrid qualification work and to guide the detailed grass port. It is not permission to implement all stages as one untested patch.

## Reading map

Read [Part A: latest decisions and exclusions](#part-a-latest-decisions) first. Then use [Part B: detailed grass and Hybrid handover](#part-b-detailed-grass) for the commit ledger, code findings and task implementation details.

Within Part B, sections 3–6 contain the commit inventory, code findings, dependency-ordered task table and file map. Section 7 contains qualification requirements. Section 10 contains history-verification commands, explicitly recorded as not executed during the source review.

## Execution map: preserve the original task identifiers

| Authoritative stage from Part A                | Detail to use from Part B                                        | Boundary                                                                                                                                                                    |
| ---------------------------------------------- | ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D0 — Baseline and small depth contract         | Task 0; sections 0, 4 and 6                                      | Record current source/build identity. No Reverse-Z activation and no broad renderer rewrite.                                                                                |
| D1 — Existing Hybrid/native comparison         | Task 7a's existing-candidate qualification; section 0            | Run with GO off. Later native/grass timing checks belong to H1 once grass exists. Preserve the original branch; keep failed qualification isolated.                         |
| G1 — PBR grass without GO                      | Task 1; relevant material/shader entries in sections 4 and 6     | Material correctness is independent of selecting Hybrid.                                                                                                                    |
| G2 — Parity-mode optimized stereo grass        | Tasks 2–6 and the early requirements from Task 9                 | Establish recovery/cache/state contracts before enabling draws. Hi-Z and quality reduction stay off for parity comparisons.                                                 |
| H1 — Optional shared grass Hi-Z                | Remaining cross-consumer evidence from Task 7a, then Tasks 7b–7c | Share a built pyramid only when its content/frame/phase/eye contract is valid for both consumers.                                                                           |
| Grass quality controls and final qualification | Tasks 8–10                                                       | Separate quality-reducing savings from matched-quality rendering gains. Integrate UI/DevBench/cache controls alongside the features that require them, not only at the end. |
| R1 — Full optional Reverse Z                   | Part A sections 4–6 and its R1 gate                              | First expand the current CSX depth-consumer audit into concrete subtasks. Standard Z remains the baseline; no Dynamic Near Clip dependency.                                 |

## Copy/paste starting instruction

```text
Use CSX_Codex_Astra_Master_Handover.md as the handover for
ParticleTroned/skyrim-community-shaders, targeting main-VR.

Read the opening guide and Part A first, then Part B's relevant details.
Part A overrides older sequencing or scope statements in Part B.
Read AGENTS.md and the applicable repository instructions before editing.
Re-resolve the source refs; preserve user changes and the original
codex/vr-hybrid-hiz-culling candidate.

Start with D0 and D1 only: establish the baseline/narrow depth contract,
reconcile the existing Hybrid candidate on an isolated current-main-VR
branch, and run the applicable available checks. Keep Standard Z and
Advanced as defaults. Reuse the existing in-game method selection,
DevBench controls, epoch invalidation and effective-backend diagnostics.
Do not begin a full Reverse-Z port or import Dynamic Near Clip.

Work in reviewable commits or small commit groups. Record source identity,
changes, exact checks and results, and any outstanding runtime gates.
Do not mark an in-game test passed unless it actually ran. If the HMD/game
is unavailable, deliver the build/test evidence and explicit runtime test
procedure, leaving the qualification gate open rather than claiming it.
Do not push, merge, or promote defaults without explicit authorization.

Later stages follow Part A: PBR on the existing renderer; parity-mode
optimized stereo grass; optional shared grass Hi-Z; then full Reverse Z.
Use Part B's detailed tasks for the grass port. Before R1, expand the
actual CSX depth-consumer audit into file-level subtasks; the current
Reverse-Z section is not an exhaustive patch specification.

Exclude Dynamic Near Clip's controller, probes, readback, hooks, settings,
adaptive near-plane writes and fog suppression. Preserve normal
near-plane correctness, existing clipping distances and fog behaviour.
```

## Embedded-source integrity

The source text below is preserved verbatim. Hashes identify the input files used in this consolidation; they do not identify repository revisions or validation builds.

| Input document                                                         | SHA-256                                                            |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------ |
| `CSX_Depth_Grass_ReverseZ_Landing_Order_v2_No_Dynamic_Near_Clip.md`    | `dea2406c3cf5db15990dec359d6174961d6b2fa886e052f3ff00100a2cc23d2d` |
| `CSX_PBR_Grass_and_Optimizations_Implementation_Plan_v2_Hybrid_HiZ.md` | `9ae25e92496c1c89794d2bd41e943ce553f8e1b4f283605d67c170fd0d033dfe` |

---

<a id="part-a-latest-decisions"></a>

# PART A — Latest decisions, landing order and Dynamic Near Clip exclusion

The following source document is authoritative over Part B for sequencing and exclusion decisions.

<!-- BEGIN VERBATIM SOURCE A -->

# CSX: landing order for Hybrid culling, grass and Reverse Z

Decision addendum v2 to the PBR Grass / Grass Optimizations v2 handover. Reviewed 3 October 2026; amended to explicitly exclude Dynamic Near Clip.

This is a source-review recommendation, not an implementation or runtime-validation report. No repository changes, builds, deployments or in-game tests were performed for this addendum. The sequencing guidance here takes precedence over the earlier general suggestion of two parallel tracks; technical independence is unchanged.

## Decision

**Qualify and land the switchable Hybrid/native-culling path first under conventional depth. Then PBR grass, then optimized grass, then optional shared grass Hi-Z. Land full Reverse Z last as an independently selectable, initially restart-required renderer mode. Define the depth-convention/resource contract now, before adding new depth consumers.**

This is the preferred serial implementation order, not an artificial dependency: PBR materials and batching/frustum-only grass can proceed on current main-VR if Hybrid runtime qualification exposes an unresolved problem. Do not merge unqualified Hybrid code simply to keep the order. Keep Advanced the default. A working experimental mode need not win every benchmark, but must meet correctness and fallback requirements and be clearly labelled rather than promoted by default.

## Explicit exclusion: Dynamic Near Clip (3 October 2026)

**Do not import Open Shaders' Dynamic Near Clip (OS #615) into the Hybrid,
grass or Reverse-Z integration. This excludes the feature, not merely its
default enablement. Preserve CSX's existing camera/near-plane policy and
vanilla fog behaviour. The landing order below is unchanged.**

This exclusion takes precedence over references below to taking applicable
OS VR depth consumers. OS #818 contains compatibility edits for Dynamic
Near Clip because OS already has it; those edits do not make the controller
a prerequisite for Reverse Z in CSX.

Retain ordinary projection/clipping mathematics and conservative handling
of bounds intersecting the camera/near plane. In particular, do not remove
Reverse-Z-compatible native occluder-box, depth-downscale or hidden-area-mask
fixes merely because their code refers to a near-plane value. Those are not
the adaptive controller being excluded.

## Reviewed sources

| Role                               | Pinned reference                                                                                                                                                            |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CSX target                         | ParticleTroned/skyrim-community-shaders main-VR, `43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35`                                                                                 |
| Existing Hybrid candidate          | `origin/codex/vr-hybrid-hiz-culling`; previously identified head `f17b833b56b5078527bb9a5b413e033455ad855d`; branch documentation and host/shaders reread for this decision |
| VR Reverse-Z donor                 | alandtse/open-shaders PR #818, head `188c80060a186fc0dbb293eeca45fac58d6554bf`, open/unmerged at review                                                                     |
| Original Reverse-Z donor inspected | InTheBottle/Bottled-Shaders, `Bottle-Compendium`, head `3ab152056eb553408429cfa72bf18f7892050349`                                                                           |
| Bottle default branch metadata     | `dev`, `f0deaabe1f5d8c675bce06927a5c257f4af4e94d`; do not confuse it with the inspected Compendium implementation                                                           |

Re-resolve refs before implementation and record drift. The original handover's grass donor inventory remains a separate pinned review, not a claim that all later donor changes were audited here.

## 1. Keep three decisions separate

1. **Depth representation:** conventional Z versus floating-point Reverse Z. This affects target formats, projections, clears, depth states, shader interpretation and temporal/upscaler contracts.
2. **Native object visibility:** existing Advanced/Legacy policies versus Hybrid Hi-Z, with object-culling enablement independently controlling whether rejection is used.
3. **Grass rendering:** original versus optimized submission; grass Hi-Z and quality-reducing density/LOD controls remain independent of the native object method.

Reverse Z is not an additional culling backend and must not be inserted as another mutually exclusive item beside Advanced, Legacy and Hybrid. Hybrid can ultimately operate with either depth representation. PBR material selection is independent of all these controls.

The Hybrid branch already exposes Advanced, Legacy and Hybrid at Info logging and provides a DevBench method setter, epoch invalidation, effective-backend reporting and fallback reasons. Reuse this instead of inventing a second selector. Its own documentation explicitly states no post-rebase build/test evidence and no in-game execution of the candidate. [R1]

## 2. Landing steps and gates

### D0 — Baseline and small depth contract

Record the exact current-main build, repeatable camera route and native/grass settings. Inventory actual depth owners and consumers, including CSX-only paths. Define a compact read-only snapshot description using existing accepted-render-state/resource-generation ownership:

-   Actual resource and view format; native depth encoding and near/far semantics.
-   Source content generation, producing frame and phase, camera/projection identity.
-   Per-eye active source rectangles, resource dimensions, sample count and mask validity.
-   Valid lifetime and whether the consumer can prove the snapshot is suitable.

Do not expose a user-facing Reverse-Z mode until it is implemented. Conventional depth remains active. Add tested helpers for depth ordering in new/changed culling code; avoid a repository-wide no-op rewrite as a prerequisite. Reverse is a contract to support and test, not permission to activate reversed rendering prematurely.

### D1 — Hybrid/native comparison under conventional Z

Bring the single Hybrid delta onto an isolated branch from current main-VR, preserving the original candidate and newer main changes. Rebuild exact source, rerun focused CPU/WARP/FXC checks, then execute in-game Advanced/Legacy/Hybrid tests with GO off.

Use the existing selector and method setter. Ensure switching retires submissions/epochs and rejects old readbacks from a different backend. Unknown results retain visibility. The steady-state benchmark runs only its selected producer; a fallback must be explicit. Keep captured native bounds/index/result ownership unchanged.

Gate: real-HMD and lifecycle correctness, safe repeated switches, documented effective backend and acceptable baseline overhead. Keep Advanced default and Hybrid opt-in. If this gate fails, keep Hybrid isolated while independent PBR work proceeds.

### G1 — PBR grass on the current renderer

Implement the original handover's material/technique/shader work without GO. Retain current CSX colour, lighting, Wetterness, foliage and deformation policies. This establishes material correctness independent of batching/culling.

Gate: authored material variants remain distinct, basic/complex grass is unchanged, depth/colour passes agree and runtime settings behave as specified.

### G2 — Optimized stereo grass without Hi-Z or quality reduction

Implement capture, lifetime, material-keyed buckets, resource preflight, descriptor-specific input layouts, corrected camera-relative projection, per-eye survivors and conservative CPU/GPU frusta. Preserve current wind/collision through the proposed coordinate adapter. Integrate valid optimizer enable/disable recovery before activation.

Keep Hi-Z, density/min-pixel removal, mesh-cost thinning, mesh LOD and simplified shading off in the parity benchmark.

Gate: correct both-eye placement/depth/deformation, safe transitions and failure recovery, matched-quality CPU/GPU measurements across native culling choices.

### H1 — Shared conservative Hi-Z builder and optional grass consumer

Reuse the qualified Hybrid reduction foundation only through the explicit snapshot contract. Keep native delayed-readback history separate from same-frame grass compaction.

A native POST_ZPREPASS_COPY snapshot and a grass live-MAIN snapshot are not interchangeable by name or SRV identity. Capture actual CSX ordering. Reuse a build only for genuinely equivalent admissible snapshots; otherwise use the same builder with separately identified contents or disable grass occlusion. Array-layer versus SBS sampling must be adapted explicitly. No new grass readback and no second render-scale controller.

Gate: native culling off/on and Advanced/Legacy/Hybrid are independently compatible with GO and grass-Hi-Z settings. Missing grass depth disables only occlusion, not valid grass drawing. No duplicate build of the same contents and no stale snapshot reuse.

### R1 — Reverse-Z integration and qualification

Use Bottle's original design and relevant fixes, OS's VR work, and CSX-specific ownership/admission improvements. Do not merge entire donor branches or stack two independent sets of camera/D3D detours.

Keep Standard Z default; Reverse Z optional and restart-required initially. Implement all necessary consumers as a coordinated package. Qualify the existing culling and grass matrix under both conventions. Unsupported combinations must report unavailable or use a verified compatible fallback, never silently claim the requested backend ran.

Gate: atomic depth-resource/feature activation, correct per-eye matrices and inverse matrices, hidden mask/water/native fallback, CSX-specific temporal/upscaler inputs, resize/load lifecycles and cold/warm cache qualification. Real-headset motion and VRAM/performance evidence are mandatory before default enablement.

## 3. Why Reverse Z should not block grass

At review PR #818 spans 75 files and changes render-target formats, depth/raster states, projection publication, framebuffer matrices, numerous depth consumers and upscaler flags. Its own report says no demonstrated visual gain in the tested scenes, no real-headset qualification and no culling-while-moving qualification. Its tests are valuable donor evidence, not CSX validation. [R2]

The actual grass changes in #818 show that these implementations are separable: RunGrass gains a helper around clip-space distance fading, while grass Hi-Z/reduction and culling use convention-aware depth ordering. Establish those semantics while implementing grass; do not delay all PBR/batching work behind the broad renderer conversion. [R3, R4]

Floating-point Reverse Z is a precision improvement. It is not inherently an optimization of CPU grass submission. The donor converts 32-bit packed D24/S8 resources to the 64-bit D32/S8/X24 family. Nominal bytes per texel of each converted resource increase from four to eight; actual total memory, compression and timing depend on allocation and hardware. Retain stencil wherever required rather than dropping it just to retain a 32-bit resource. [R5, R6]

## 4. Source selection for the Reverse-Z port

### Bottled Shaders

Inspect the original conversion/state/projection logic and related follow-ups, including its hook-passthrough mechanism and external-effect compatibility paths. PR #818 attributes its starting point to Bottle commits `6db6512c96`, `0a9f8f8ac0`, `42d5129c75`, `9a6d3b2426`, `1e88b27471`, `ca51444e8a`; use this lineage as a review list, not a blind cherry-pick sequence. [R2]

The current inspected Compendium header defaults EnableReverseZ to true and provides the original single-framebuffer fixup interface and SetHookPassthrough. These are differences to evaluate, not defaults/APIs to impose on CSX. The inspected implementation caches verdicts by raw DSV pointer and keeps some requested states as raw pointers. [R7, R8]

### Open Shaders

Prefer its explicit VR camera/framebuffer work, reversed native-culling downscale/proxy fixes, hidden-area-mask correction, both-eye upscaler handling and shared depth helper intent. Its current source pins cached DSVs and source states, unlike the Bottle excerpt; its getters also distinguish requested from mapped states. Review these semantics against CSX's RAII context restoration rather than layering the Bottle bypass on top indiscriminately. Default-off and restart-gated is the appropriate initial policy for CSX. [R2, R9, R10]

The documentation still includes some stale statements; inspect code before reusing a claim. For example, the current OS implementation retains DSV references in its verdict map. Do not repeat an older raw-pointer-lifetime finding as if it still applies unchanged to this head. [R9]

### CSX-owned integration

Keep one authoritative resource/camera/feature-generation decision. Reconcile donor hooks with existing accepted graphics ownership, render-scale lifecycle, native Hybrid routing, shader generations and all relevant CSX-only depth consumers. Prefer narrow adaptation over a new general renderer framework.

The source selection remains provisional until measured in the same CSX build; this review does not establish which implementation is fastest.

## 5. Required corrections before Reverse-Z activation

### 5.1 Hybrid is currently incompatible as-is

Hybrid CaptureFrame accepts R24_UNORM_X8_TYPELESS and R32_FLOAT SRVs, not the R32_FLOAT_X8X24_TYPELESS view produced by the donor's D32/S8 conversion. It would reject that source and use its native fallback if otherwise correctly integrated. Merely selecting Hybrid would not prove Hybrid actually ran. [R11, R5]

Do not fix this by relaxing the format gate alone: BuildDepthCS hard-codes conventional far=1 and maximum reduction, and treats zero as invalid/masked. The other reduction/visibility/clip tests also require coordinated semantics. [R12, R1]

For native-depth storage in the conservative hierarchy:

| Operation                            | Standard Z                                 | Reverse Z                                  |
| ------------------------------------ | ------------------------------------------ | ------------------------------------------ |
| Near / far                           | 0 / 1                                      | 1 / 0                                      |
| Farthest covered depth               | maximum                                    | minimum                                    |
| Conservative invalid/unknown padding | 1                                          | 0                                          |
| Candidate hidden with bias epsilon   | objectNearest > occluderFarthest + epsilon | objectNearest < occluderFarthest - epsilon |

Nearest-object depth, clipping checks, bias interpretation, masks and matrix conventions must also agree. Keep mask semantics explicit. Do not globally equate a raw zero with a lens-mask pixel after reversal.

Prefer preserving raw reversed floating-point values in the pyramid. Repeatedly converting them to standard float depth near 1 can round away the distant precision the feature was intended to provide. Legacy consumers can receive a scoped conversion where genuinely required; matrix unprojection uses depth consistent with that matrix. [R4, R6]

### 5.2 Activation must be atomic across required resources and consumers

OS LatchBootState/HasShaderDefine uses the boot request for activeThisBoot, while SetupDepthTargets converts/publishes targets individually, continues after failures and sets depthTargetsConverted = converted > 0. This permits a requested active mode alongside incomplete required target conversion. This is a source-level failure-path concern, not a reproduced CSX failure. [R13, R14]

CSX should preflight required targets, views, shaders and compatible paths; publish a consistent set and convention generation only when ready. On failure retain the entire last valid standard configuration, not a partially converted mixture. Different intentionally standard resources such as appropriate shadow/cubemap targets remain explicitly tracked; that is different from a failed partial scene conversion.

Use explicit resource ownership/generation for convention membership, not float format alone. Other float depth resources need not use the reversed convention.

### 5.3 Hybrid/native hook composition and fallback

The Reverse-Z donor modifies the engine downscale and occlusion-proxy path that Hybrid may suppress and later replay. Establish one dispatch owner. Hybrid failure must replay a depth/proxy implementation compatible with the active convention; it must not replay a standard-only shader against reversed depth. Shader-identification mismatch or unavailable replacements must disable the incompatible path rather than continue silently. [R2, R10, R1]

## 6. Toggle and benchmark contract

Recommended UI separation:

-   **Object culling:** retain existing exterior/interior enabled switches and method selector Advanced / Legacy / Hybrid Hi-Z. Same-session switching at a safe native submission boundary.
-   **Grass optimizer:** Original / Optimized, with complete shader/engine-patch transition and requested/prepared/applied status. Transition may involve shader preparation; exclude transition frames from steady-state timing.
-   **Grass occlusion:** Off / conservative Hi-Z. Optional developer comparison of donor and shared implementations only if each has a known snapshot/layout contract. Avoid permanent duplicate builders solely for debugging.
-   **Depth representation:** Standard / Reverse Z. Requested/applied-at-boot status and restart-required notification initially.

For each fixed depth convention, run repeated matched A/B windows across native backend choices and grass modes. Check effective backend and fallback rate rather than trusting a menu setting. Freeze or record the same accepted render scale, grass population, distance/fade, materials, wind/collision settings, weather, camera path and unrelated adaptive policies. Reset backend history fairly; warm up; exclude shader compilation and switch recovery from steady-state results while reporting transition costs separately. Report CPU/GPU times, p50/p95/p99, visible counts, draw/dispatch counts and VRAM peaks. Never count disappearing visible geometry as a speedup.

Reverse Z comparisons initially require controlled restarts of the same binary with a reproducible save/replay and warm caches. The requested in-game A/B of culling backends is still available within either qualified depth mode. Do not claim Reverse Z is mathematically impossible to switch live; it is a separate renderer-transition project, not a checkbox-sized addition. Preallocating two modes would also distort VRAM/performance comparisons.

## Sources

R1. CSX Hybrid specification: https://github.com/ParticleTroned/skyrim-community-shaders/blob/codex/vr-hybrid-hiz-culling/docs/development/vr-hybrid-culling.md

R2. OS PR #818 metadata, scope and author-reported validation: https://github.com/alandtse/open-shaders/pull/818

R3. OS PR #818 RunGrass and grass culling changes: https://github.com/alandtse/open-shaders/pull/818/files

R4. OS depth helper: https://github.com/alandtse/open-shaders/blob/188c80060a186fc0dbb293eeca45fac58d6554bf/package/Shaders/Common/ReverseZ.hlsli

R5. OS target conversion and formats: https://github.com/alandtse/open-shaders/blob/188c80060a186fc0dbb293eeca45fac58d6554bf/src/Features/ReverseZ.cpp

R6. NVIDIA depth precision explanation and Microsoft format definitions: https://developer.nvidia.com/blog/visualizing-depth-precision/ ; https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format

R7. Bottle original feature/settings interface: https://github.com/InTheBottle/Bottled-Shaders/blob/3ab152056eb553408429cfa72bf18f7892050349/src/Features/ReverseZ.h

R8. Bottle original implementation: https://github.com/InTheBottle/Bottled-Shaders/blob/3ab152056eb553408429cfa72bf18f7892050349/src/Features/ReverseZ.cpp

R9. OS source, verdict ownership and state translation: same pinned source as R5, beginning of file.

R10. OS state/getter/native-shader hooks: same pinned source as R5, approximately lines 790–1050.

R11. Hybrid CaptureFrame admission: https://github.com/ParticleTroned/skyrim-community-shaders/blob/codex/vr-hybrid-hiz-culling/src/Features/VRHybridCulling.cpp

R12. Hybrid base reduction: https://github.com/ParticleTroned/skyrim-community-shaders/blob/codex/vr-hybrid-hiz-culling/package/Shaders/VRHybridCulling/BuildDepthCS.hlsl

R13. OS activation and per-target allocation: same pinned source as R5, approximately lines 420–520.

R14. OS publication and completion flag: same pinned source as R5, approximately lines 520–550.

## 7. Dynamic Near Clip: evidence and exclusion boundary

Reviewed OS dev at `0d169a2a1844130d146d04cf275430518408ef90` (2 October
2026), including the actual controller, host implementation and probe shader.
This is a source review, not an in-game reproduction of user reports.

### Confirmed upstream history

OS #615 introduced a controller that changes the camera's near clipping
distance in response to previously rendered scene depth. It is distinct from
occlusion-culling backend selection. OS #797, merge commit
`e210f6c2d0c58b7a56bb8e17a8b461f5c741a7f1`, merged 26 September 2026,
changes the default from on to off specifically because enabling the feature
also disables vanilla fog. New installs/Restore Defaults use false; previously
saved true values remain true. That is an opt-in change, not a repair of the
controller or automatic migration of existing users. [N1, N2]

The reviewed public material substantiates that integration problem. It does
not quantify every user report mentioned in the conversation or establish
that every reported visual defect has the same cause.

### Source-level assessment

The idea has a legitimate motivation under conventional depth: increasing
the near distance improves depth precision but clips more nearby geometry;
reducing it does the opposite. The existing implementation also has useful
guards: a common selected distance for both eyes, configuration validation,
nonblocking readback, immediate downward response to an accepted target,
and delayed/hysteretic restoration. Do not describe it as having no safeguards.
[N3, N4, N6]

There are nevertheless material limitations:

-   The shader samples a 16x16 grid in approximately the central 30% of each
    eye's width and height, with a 2x2 neighbourhood per grid point. This is not
    full-eye near-geometry coverage. When at least four grid points are valid,
    it uses the fourth-nearest point minimum instead of the absolute nearest.
    This intentional outlier filtering can discount a real small close object.
    [N5]
-   A scene-depth-only detector has no evidence of geometry that was already
    clipped out or never entered the sampled region. The resulting blind spot
    is an inference from the input and sample coverage, not an in-game failure
    reproduced in this review. [N5]
-   ReadDepth accepts newer samples no older than 0.2 seconds using serial/time
    checks. That is a maximum permitted age, not a measured 200 ms latency.
    This acceptance path contains no camera-pose displacement test. Capture
    does validate projection/depth consistency, but that does not establish
    that an old distance is still safe after head/object motion. [N4]
-   BeforeCameraUpdate calls UpdateVanillaFogOverride before readiness/failure
    admission. That override follows the requested DynamicNearClip setting,
    not the successfully applied state. Consequently a requested-on feature
    can continue suppressing fog even when the camera override cannot run.
    This is a code-path finding, not a reproduced runtime incident. [N4]
-   The host resets SSGI history on specified near-plane transitions. Preserve
    this as evidence of real temporal coupling; do not infer that all other
    temporal consumers are necessarily broken. [N4]

### Required implementation exclusions

Do not add the following as part of this project:

-   The DynamicNearClip.cpp/.h controller, NearClipController.h,
    NearClipProjection.h or DynamicNearClipCS.hlsl runtime/probe dependencies.
-   Its camera-preparation/world-depth interception, adaptive fNear writes,
    staging ring, GPU probe dispatch, debug readout, settings or presets.
-   Its bFogEnabled override, Exponential Height Fog dependency, or history
    resets driven by this adaptive feature.

Do not equate "disabled by default" with "not imported": the reviewed
SetupResources gates on VR/hook availability rather than DynamicNearClip,
so the donor can still prepare resources for an off-by-default feature. [N4]

Keep the established source near/far clipping distances during initial
Reverse-Z conversion and A/B testing. Reverse the depth mapping, not the
scene's chosen clipping bounds. Only later consider an explicitly separate
fixed-near tuning experiment. Floating-point Reverse Z can improve the
precision tradeoff, but does not restore clipped geometry or independently
repair near-dependent fog/water assumptions. [N6, N7]

Acceptance for this exclusion: no new dynamic-near probe/hook/settings path,
no near-distance changes caused by backend/grass/Reverse-Z selection, no fog
setting changes caused by this port, and retained conservative clipping/mask
correctness under both depth conventions. No attempt to redesign Dynamic
Near Clip is included in this project.

### Near-clip review references

N1. OS #615: https://github.com/alandtse/open-shaders/pull/615

N2. Default-off change and migration behaviour: https://github.com/alandtse/open-shaders/pull/797

N3. Current controller and defaults: https://github.com/alandtse/open-shaders/blob/0d169a2a1844130d146d04cf275430518408ef90/src/Features/VR/NearClipController.h

N4. Current host, readback, fog admission and resources: https://github.com/alandtse/open-shaders/blob/0d169a2a1844130d146d04cf275430518408ef90/src/Features/VR/DynamicNearClip.cpp

N5. Current probe shader: https://github.com/alandtse/open-shaders/blob/0d169a2a1844130d146d04cf275430518408ef90/package/Shaders/VR/DynamicNearClipCS.hlsl

N6. NVIDIA depth precision analysis: https://developer.nvidia.com/blog/visualizing-depth-precision/

N7. OS #818's author-reported water/fog tests and VR compatibility scope: https://github.com/alandtse/open-shaders/pull/818

## Starting instruction for implementation

```text
Work in ParticleTroned/skyrim-community-shaders on an isolated branch from current main-VR. Read AGENTS.md and the existing v2 grass handover plus this sequencing addendum. Preserve user changes and the original experimental Hybrid branch. Do not push or merge without explicit authorization.

First inventory/define the narrow depth convention/snapshot contract and reconcile the existing Hybrid A/B candidate, not full Reverse Z. Keep Standard Z and Advanced as defaults. Rebuild and test exact source; report runtime tests as outstanding until actually executed. Reuse the existing selector, epoch invalidation, DevBench schema/actions and effective-backend diagnostics. If Hybrid fails qualification, keep it separate and proceed with independent PBR material work.

The next independent stages are PBR on the existing renderer; parity-mode optimized stereo grass; optional qualified shared Hi-Z; and only then full optional Reverse Z. For Reverse Z compare the pinned Bottle-Compendium original and OS #818 VR adaptation, selecting compatible semantics rather than merging both hook systems. Do not copy default-on behaviour or per-target partial activation. Make the required scene-resource/shader/convention activation consistent and test both depth conventions through native fallback and grass paths.

Exclude OS Dynamic Near Clip (#615) entirely, not just default-off: no adaptive near-plane controller/probe/readback/hooks/settings or vanilla-fog suppression. Preserve CSX camera clipping distances and fog. Do retain ordinary near-plane intersection safety, Reverse-Z-native occluder fixes and hidden-area-mask correctness. OS #818's DynamicNearClip compatibility hunks are not dependencies for this port.

Same-session A/B applies to culling and qualified grass toggles. Reverse Z is restart-required initially. Do not introduce a render-scale controller, extra grass readback, unproven cross-phase Hi-Z reuse or broad rendering framework refactor. Preserve CSX material, deformation, depth ownership, temporal/upscaler, shader-cache and resource-generation contracts. Credit verified source contributions and record exact validation evidence.
```

<!-- END VERBATIM SOURCE A -->

---

<a id="part-b-detailed-grass"></a>

# PART B — Detailed PBR Grass / Grass Optimizations plan and Hybrid amendment

The following source document supplies the detailed grass tasks. Its older parallel-track sequencing and starting prompt are subordinate to Part A and the opening guide.

<!-- BEGIN VERBATIM SOURCE B -->

# CSX: PBR Grass and Grass Optimizations

Implementation handover — original grass audit 1 October 2026; Hybrid Hi-Z amendment 3 October 2026

**Version 2:** incorporates the existing `origin/codex/vr-hybrid-hiz-culling` branch. The Hybrid-specific findings below supersede the previous assumption that the grass Hi-Z foundation needs to be introduced independently. No code was changed, compiled, deployed or tested in-game during this amendment.

## 0. Existing Hybrid Hi-Z branch: authoritative amendment

### 0.1 Identification and provenance

Repository: `ParticleTroned/skyrim-community-shaders`.

| Item                                  | Verified value                                                                           |
| ------------------------------------- | ---------------------------------------------------------------------------------------- |
| Remote-tracking branch                | `origin/codex/vr-hybrid-hiz-culling`                                                     |
| Published head                        | `f17b833b56b5078527bb9a5b413e033455ad855d`                                               |
| Commit subject                        | `feat(vr): add experimental hybrid Hi-Z culling`                                         |
| Merge base with current main-VR       | `dab1874a76fd39175dcefdc52110ba69d7284e12`                                               |
| Current main-VR observed on 3 October | `43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35`                                               |
| Comparison                            | Hybrid is 1 commit ahead and 10 commits behind current main-VR; histories have diverged. |

Source: GitHub branch and comparison responses [H1–H3]. The base is the same commit used by the original grass audit. The new ten-commit main-VR delta was identified, not comprehensively re-audited for grass in this amendment. Start implementation from current main-VR and reconcile it; never replace current main-VR with the older Hybrid branch. Preserve the original experimental branch and its evidence. GitHub establishes the remote branch, not the user's machine-local worktree path.

### 0.2 Validation status — do not overstate it

The branch documentation reports pre-rebase universal DLL builds, focused CPU/controller tests, production-shader WARP tests and FXC compilation. It also records read-only inspection of an existing SkyrimVR process to establish native layouts and call boundaries. These are useful forms of evidence, not in-game execution of the candidate. [H4–H5]

The final `Rebase, 2026-09-29` section states that the change was rebased onto `dab1874a76...` and **no compilation or tests were run for that rebase**. The user's statement that it is not yet tested in-game is consistent with the record. Treat the published `f17b833b56...` as **experimental, not runtime-qualified, with pre-rebase test evidence only**. Earlier dirty-build manifests do not certify the published head or a future integration head. [H4]

### 0.3 What this branch implements

It replaces the native GPU visibility producer for engine-registered objects while retaining native collection, affine OBB records, per-submission indices, result buffers, staging copies and delayed CPU consumption. The native contract has a maximum of 4096 registered OBBs per batch. Indices are not persistent object identities. [H4–H5]

Its implementation includes:

-   `src/Features/VRHybridCulling.cpp/.h`: preparation, resources, native-buffer validation, dispatch and readback handling.
-   `src/Features/VRHybridCullingPolicy.h` and `VRHybridCullingHistory.h`: layout/constant and temporal correspondence policies.
-   `package/Shaders/VRHybridCulling/BuildDepthCS.hlsl`: complete maximum-depth reduction, normally over 4-by-4 source regions; eye-local regions become separate array layers.
-   `ReduceDepthCS.hlsl`: maximum-depth mip reduction. Padded/invalid coverage remains far depth, and unused terminal work is avoided.
-   `TestBoundsCS.hlsl`: all eight affine-box corners, per-eye camera adjustment and actual view-projection, conservative rectangle coverage, nearest-box versus farthest-covered depth, retaining the object unless both eyes establish occlusion.
-   Existing VR depth-culling method selection, fallback routing, shared telemetry admission and DevBench diagnostics.

The hierarchy uses conventional depth (near zero, far one), not Reverse Z. The host keeps the pyramid and its all-mips SRV private in `g_resources`; the public header does **not** expose a general-purpose depth-pyramid consumer API. [H4, H6–H10]

### 0.4 Relationship to Grass Optimizations

These are complementary consumers, not interchangeable renderers:

| Contract                                  | Existing Hybrid                                                           | Planned Grass Optimizations                                                            |
| ----------------------------------------- | ------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| Input                                     | Native engine affine OBB registrations                                    | Captured grass instances, slices and material/mesh buckets                             |
| Visibility output                         | One native-indexed visibility value; either-eye visibility retains object | Eye-specific compacted survivor ranges and indirect draw arguments                     |
| Delivery                                  | Native staging and delayed CPU consumption                                | GPU compaction consumed by grass drawing without adding a grass visibility readback    |
| Other work                                | Native producer replacement and temporal acceptance                       | Batching, materials, wind/collision payload, LOD and optional density/shading controls |
| Hi-Z resource in inspected implementation | `Texture2DArray<float>` with two eye layers                               | OS donor uses `Texture2D<float>` and side-by-side UV mapping                           |

Sources: Hybrid host/shader/native contract [H5–H10] and pinned OS culler/draw code [H11–H12]. PBR grass material and shading integration is still needed. The 4096-entry native result ABI must not become a cap or identity system for grass instances. Do not add a GPU-to-CPU grass readback just to reuse Hybrid's result path.

Do not transplant `TestBoundsCS` as the whole grass culler: it deliberately leaves off-screen rejection to the native frustum stage and reduces stereo visibility to one union result. Grass needs its existing per-eye frustum/distance evaluation and separate survivors. Shared projected-rectangle/depth sampling helpers are a useful candidate; using eight-corner projection and repeated validation per grass instance requires profiling before it becomes the release path.

### 0.5 Reuse the foundation, not unverified frame contents

Preferred long-term direction: a **small conservative stereo-depth builder/view shared by the two consumers**, with separate native and grass visibility algorithms. This is a proposed extraction, not an API that is already implemented. Extract it only as required by the first grass Hi-Z consumer, not as a broad graphics-framework rewrite.

Reuse the existing layout validation, maximum reduction, eye isolation, far-depth padding, resource naming, compile-failure latching and appropriate test fixtures. Preserve the native routing and deferred history logic independently. Keep resources render-thread-owned and use existing CSX context/accepted-render-state ownership.

A shared view needs explicit provenance: source resource/view and format; conventional-depth semantics; content-producing frame and render phase; resource/content generation; producing camera/view and projection convention; each eye's active rectangle; pyramid dimensions/reduction/mips; and a bounded lifetime. An unchanged SRV pointer is not proof that its contents or camera attribution are unchanged.

**Sharing source code does not automatically permit sharing one built texture.** Hybrid captures the effective `kPOST_ZPREPASS_COPY` at the native downscale/culling boundary. OS grass builds from live `kMAIN` when available, with a prepass-copy fallback, on the first grass `SetupGeometry` of the frame. Neither a common frame number nor the word “Hi-Z” proves those snapshots are interchangeable. [H5, H7, H12–H13]

Capture actual CSX event ordering and depth contents. The shared build must be available before each consumer, and its occluder coverage, projection, jitter treatment and active layout must support that consumer's proof. Check whether alpha-tested/deforming grass or other moving occluders are present in the source. Do not move native producer work across its verified boundary simply to make a texture available to grass.

If the snapshots genuinely match, build once and reuse the immutable view. If they do not, either use the same builder for two explicitly attributed snapshots and measure the additional cost, or keep grass occlusion off. Never silently reuse the previous frame's Hybrid pyramid for same-frame grass rejection. Keeping two builds deliberately for different evidence is different from accidentally duplicating the same build.

### 0.6 Concrete adaptations and risks

**Texture layout.** OS `GrassCullingCS.hlsl` declares a 2D Hi-Z at `t2` and packs each eye's UV into half its width. Hybrid supplies two array layers. Change the grass declaration, sampling, dimensions and coordinate conversion as one shader ABI; retain eye-local XY and select the layer with the eye index. Do not bind an array SRV to the existing 2D declaration. Preserve independent eye counters and the eight-UAV culler budget. [H10–H11]

**Terrain Blending.** The inspected CSX `ShouldUseBlendedDepthSRV()` disables the blended alias while VR depth culling is enabled. This is part of Hybrid's source assumptions. A grass-only Hi-Z consumer must not enable native/Hybrid depth culling merely to inherit that condition. Native-culling-off plus grass-Hi-Z-on needs its own verified effective source contract; leave occlusion off if unavailable. [H14]

**Render scale.** `CaptureFrame()` explicitly rejects unlocked non-unit dynamic-resolution ratios, requires an even-width single-sample stereo texture, full normalized eye viewports and conventional depth range, and accepts specific SRV formats. Physical render-scale target sizes are intended to be supported, but that is not proof all CSX upscaling/foveated layouts or transitions work. Preserve fail-open rejection until accepted render-state rectangles/mapping are integrated and tested. Avoid new per-frame resolution surveillance. [H7]

**Temporal safety.** Hybrid checks batch identity, bounds contents, epoch, source identity/dimensions/rectangles, camera and eye coherence. Its small-motion thresholds are heuristics; independently moving occluders remain unresolved. Invalidated readable batches are made visible without Advanced's 64-object recovery quota. These safeguards belong to deferred native results, not to a fabricated cross-frame identity for grass. [H4, H7, H9]

**Fallbacks.** Native Hybrid failure must replay the skipped native downsample before native testing and Advanced recovery. Missing grass Hi-Z should disable only grass occlusion, retaining the valid optimized draw path. Failure of the optimized grass representation still requires its complete vanilla shader/draw/patch restoration. Do not tie the two method selectors together. [H4–H5]

**Allocation/performance.** Hybrid currently rebuilds pyramid resources when its padded dimensions change, scans complete source coverage, issues a reduction dispatch per mip and snapshots/compares native bounds. These are real work, not proof of a regression. When extracting a shared provider, reuse validated state and cache allocations using existing accepted resource generations/capacity policy; do not poll separately per bucket. Measure CPU and GPU costs. The `VRHybridCulling::Visibility` scope currently encloses `BuildHierarchy`, so do not add those inclusive timings together as independent cost. [H7–H8]

**Native readback.** Reusing the existing native staging resource avoids an additional readback; it does not remove the existing potentially blocking staging map. Keep that distinction in performance claims. [H4–H5]

**Upstream culling ownership.** OS queues one representative grass shape per bucket via `OnVisible`, then relies on coarse-slice and compute culling. During combined validation, trace whether any earlier native rejection can suppress the representative or all submissions for a bucket. A shape-local bound must never be assumed to bound an entire cross-cell bucket. This is an integration check, not a reproduced bug. [H12]

### 0.7 Revised work sequence

Run two separately attributable development tracks on compatible current-main bases:

1. **Hybrid qualification track:** integrate the one Hybrid delta onto a fresh branch from current main-VR, preserving newer main changes; rebuild exact source; rerun focused tests/FXC; then run real in-game Advanced-versus-Hybrid tests with GO absent/off. Keep Advanced the default. Do not rewrite the shared experimental history merely to do this.
2. **Grass track:** retain original Tasks 0–6: PBR on the existing renderer, capture/material/deformation contracts, safe optimized stereo submission and conservative frustum culling, with grass Hi-Z, density removal and mesh LOD off. This work does not require Hybrid to pass runtime qualification.
3. **Hi-Z convergence:** after independently qualifying the relevant Hybrid builder/source path and grass renderer, perform Tasks 7a–7c below. A failure in native deferred-history qualification does not logically invalidate separately tested reduction shaders, but reused code must have its own exact-build evidence; do not call the whole Hybrid path qualified by association.
4. **Combined qualification:** test object-culling and grass controls independently, matching population/materials/scale. Keep Hi-Z opt-in until the combined evidence supports enablement.

Minimum combination matrix (each row uses identical materials, density, distance and render scale):

| Object culling                | Grass renderer | Grass Hi-Z   | Purpose                                                          |
| ----------------------------- | -------------- | ------------ | ---------------------------------------------------------------- |
| Advanced                      | Existing       | Off          | Current-baseline reference                                       |
| Hybrid                        | Existing       | Off          | Isolate Hybrid's native replacement                              |
| Advanced                      | Optimized      | Off          | Isolate grass batching/frustum work                              |
| Hybrid                        | Optimized      | Off          | Detect interaction without grass Hi-Z                            |
| Advanced                      | Optimized      | On           | Validate grass-only demand for the shared provider               |
| Hybrid                        | Optimized      | On           | Verify shared-or-separate snapshot cost and correctness          |
| Native depth culling disabled | Optimized      | On requested | Verify independent source admission and safe grass-only fallback |

Within every relevant row, include native/DLAA/DLSS/FSR and transitions supported by the candidate; distinguish supported cases from deliberate fallback. Inspect both eyes under head rotation/translation, moving occluders and cell changes. Check actual `effectiveBackend`, fallback reasons and admitted snapshots rather than trusting the requested Hybrid setting. Match telemetry/profiler state across A/B windows. Use existing baseline evidence where comparable; do not run another unchanged baseline campaign in place of targeted differences.

### 0.8 Additional pinned sources

-   [H1: remote branch inventory](https://api.github.com/repos/ParticleTroned/skyrim-community-shaders/branches?per_page=100&page=2)
-   [H2: current main-VR identity](https://github.com/ParticleTroned/skyrim-community-shaders/commit/43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35)
-   [H3: main-to-Hybrid comparison](https://github.com/ParticleTroned/skyrim-community-shaders/compare/43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35...f17b833b56b5078527bb9a5b413e033455ad855d)
-   [H4: Hybrid implementation and validation record](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-hybrid-culling.md)
-   [H5: native contract and inspection limits](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/docs/development/vr-hybrid-culling-native-contract.md)
-   [H6: Hybrid public interface](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCulling.h)
-   [H7: source admission / host implementation](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCulling.cpp#L165-L228)
-   [H8: pyramid construction / dispatch](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCulling.cpp#L324-L480)
-   [H9: readback validation](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCulling.cpp#L496-L556)
-   [H10: conservative native bounds shader](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/package/Shaders/VRHybridCulling/TestBoundsCS.hlsl)
-   [H11: inspected OS grass compute consumer](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/features/Grass%20Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl)
-   [H12: inspected OS submission / first-grass-update hooks](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/src/Features/GrassOptimizations.cpp#L887-L1000)
-   [H13: inspected OS Hi-Z source selection](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/src/Features/GrassOptimizations/HiZPyramid.cpp)
-   [H14: CSX Terrain Blending depth-alias guard](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/TerrainBlending.cpp#L165-L174)
-   [H15: complete-source reduction shader](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/package/Shaders/VRHybridCulling/BuildDepthCS.hlsl)
-   [H16: layout and shader constant policy](https://github.com/ParticleTroned/skyrim-community-shaders/blob/f17b833b56b5078527bb9a5b413e033455ad855d/src/Features/VRHybridCullingPolicy.h)

The analysis above distinguishes repository implementation, repository-reported tests, and proposed integration work. No runtime performance gain or visual correctness claim is established by this document.

## 1. Decision and scope

Use the final, corrected Open Shaders VR implementation as the reference for optimized instance capture, culling and drawing. Use upstream for the PBR material/technique implementation and its newer compile-failure hardening. Adapt both to CSX. Do not replace `RunGrass.hlsl`, merge all of OS `dev`, or replay the historical commits indiscriminately.

Implement and qualify PBR materials, optimized stereo submission, and optional Hi-Z/density/LOD as separate stages. Preserve existing CSX Grass Lighting runtime controls, Foliage Lighting, Wetterness, semantic colour/ambient balance, shadow and Skylighting behaviour, shader-cache generations, and render-scale ownership. Keep the new shared wind/collision system outside this first integration. For optional grass Hi-Z, use the existing Hybrid branch as the first CSX foundation to evaluate, with the consumer/source-contract separation in Section 0; do not introduce a duplicate standalone foundation by default.

**Status:** analysis and plan only. No repository edits, commits, builds, shader compilation or in-game validation were performed for this handover. Observed code and PR-reported testing are not evidence that the proposed CSX port has been tested.

## 2. Pinned baseline

| Role                                       | Repository                                   | Branch                        | Reviewed HEAD                              |
| ------------------------------------------ | -------------------------------------------- | ----------------------------- | ------------------------------------------ |
| Original grass audit                       | `ParticleTroned/skyrim-community-shaders`    | `main-VR`                     | `dab1874a76fd39175dcefdc52110ba69d7284e12` |
| Current target identity (Hybrid amendment) | `ParticleTroned/skyrim-community-shaders`    | `main-VR`                     | `43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35` |
| Existing CSX Hi-Z candidate                | `ParticleTroned/skyrim-community-shaders`    | `codex/vr-hybrid-hiz-culling` | `f17b833b56b5078527bb9a5b413e033455ad855d` |
| VR donor                                   | `alandtse/open-shaders`                      | `dev`                         | `d6bfd6b7b552e70d23eec00fce4e7f14cbece0af` |
| Upstream                                   | `community-shaders/skyrim-community-shaders` | `dev`                         | `97a7db04f50643efce30e8632a32082b8eebcd3f` |

The target is literally `main-VR`, not `main-vr`, `main.vr`, or `main-vr-nr`. Recheck HEAD before implementation; do not reset or overwrite newer user work.

## 3. Commit inventory and adoption decisions

This is a consolidated inventory of landed feature change sets and relevant follow-ups, not a list of every intermediate development commit inside each PR. Some rows are merge commits containing unrelated work. Preserve the final combined grass behaviour, and record the extracted hunks in the port ledger. Do not count the upstream implementation and its OS integration as independent features. Exact ancestry/patch-equivalence against CSX must be established locally; a merge ancestor alone does not prove a selectively deferred feature is present.

| Source | Commit / PR                                                                                                                                                                                                      | Change                                        | CSX action                                                                                                                                                                                                        |
| ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| UP     | [`dc11b1af40`](https://github.com/community-shaders/skyrim-community-shaders/commit/dc11b1af4082ff3200e0518cf463a8b07dc735ca) · [#2688](https://github.com/community-shaders/skyrim-community-shaders/pull/2688) | Grass Optimizations foundation                | Adopt architecture, not its flat-only final state. Buckets, indirect draws, culling, Hi-Z, density and mesh LOD.                                                                                                  |
| UP     | [`10ec5fca6d`](https://github.com/community-shaders/skyrim-community-shaders/commit/10ec5fca6d78c942f466c43859430689405105c8) · [#2706](https://github.com/community-shaders/skyrim-community-shaders/pull/2706) | D3D11 UAV / RenderDoc correction              | Required. Retain packed LOD counters and the eight-UAV compute layout; do not resurrect pre-fix resources.                                                                                                        |
| UP     | [`2d68ed181a`](https://github.com/community-shaders/skyrim-community-shaders/commit/2d68ed181ada895807ac87fdaaa9e1b83ceb01bd) · [#2709](https://github.com/community-shaders/skyrim-community-shaders/pull/2709) | PBR grass reintroduction                      | Required. Construct complete materials before interning, custom grass technique/material setup, PBR shader, material-keyed buckets.                                                                               |
| UP     | [`035ad6d`](https://github.com/community-shaders/skyrim-community-shaders/commit/035ad6d) · [#2705](https://github.com/community-shaders/skyrim-community-shaders/pull/2705)                                     | Remove legacy grass shadow clamp              | Preserve final no-extra-clamp lighting semantics. This abbreviated commit is also explicitly referenced by upstream #2716. Preserve constant-buffer layout even when a field is unused.                           |
| UP     | [`aebf01c2ef`](https://github.com/community-shaders/skyrim-community-shaders/commit/aebf01c2efa0a1926d676dc66d2623aaf58cc866) · [#2716](https://github.com/community-shaders/skyrim-community-shaders/pull/2716) | Remove PBR grass shadow clamp                 | Extract grass shader hunk. Do not import the unrelated DLL incompatibility policy merely for this fix.                                                                                                            |
| UP     | [`e66deb7ab3`](https://github.com/community-shaders/skyrim-community-shaders/commit/e66deb7ab31134b782147cf32865ea052bcddbbd) · [#2781](https://github.com/community-shaders/skyrim-community-shaders/pull/2781) | Latch failed compute compiles                 | Required hardening. Extends OS fixes 72ce6b2dc and 5232d55d3 to complex detection and Hi-Z base/SPD, not just the culler.                                                                                         |
| OS     | [`f1d73b0617`](https://github.com/alandtse/open-shaders/commit/f1d73b0617130be0b3840634db18a9ef1697b817) · [#630](https://github.com/alandtse/open-shaders/pull/630)                                             | Initial VR Grass Optimizations                | Lineage/reference. Correct VR hook offsets, per-eye storage and indirect drawing. Do NOT reproduce its superseded projection, normal orientation or duplicate per-eye dispatch.                                   |
| OS     | [`3ee0ddf754`](https://github.com/alandtse/open-shaders/commit/3ee0ddf7547fb0a810be264a7cb35ad9faf5fb51) · [#648](https://github.com/alandtse/open-shaders/pull/648)                                             | Correct VR instance culling                   | Required final behaviour: one thread decodes an instance and evaluates both eyes; actual renderer matrices for GPU planes.                                                                                        |
| OS     | [`a1915fc73b`](https://github.com/alandtse/open-shaders/commit/a1915fc73b240abf802b7d2a7041b293d0427b2b) · [#663](https://github.com/alandtse/open-shaders/pull/663)                                             | Re-port VR after upstream PBR/UAV rewrite     | Required grass integration hunks. Preserve material keys, VR capacity bound and descriptor-specific input layouts; remove invalid non-PBR Reflectance write. This is a broad merge, NOT an automatic cherry-pick. |
| OS     | [`58fd866d26`](https://github.com/alandtse/open-shaders/commit/58fd866d268736eaf6c60a347e802471c37d6d24) · [#677](https://github.com/alandtse/open-shaders/pull/677)                                             | Foliage scattering at shadow limits           | Compare against CSX Foliage Lighting and shadow ownership; carry necessary grass semantics without replacing the shared CSX lighting system.                                                                      |
| OS     | [`5fb80fe627`](https://github.com/alandtse/open-shaders/commit/5fb80fe6275ebc3a4463641c2100bc6fbac75139) · [#680](https://github.com/alandtse/open-shaders/pull/680)                                             | Remove vanilla lighting dimming               | Retain no FogNearColor.w output multiplier in both new PBR and legacy paths. Avoid changing current CSX brightness.                                                                                               |
| OS     | [`2f1dc9af5a`](https://github.com/alandtse/open-shaders/commit/2f1dc9af5aff20ba2bb7ff6b5422f0683a10b020) · [#686](https://github.com/alandtse/open-shaders/pull/686)                                             | Stable backface lighting                      | Retain SV_IsFrontFace-based orientation and sphere-normal exception; supersedes view-dependent normal flipping in the initial VR port.                                                                            |
| OS     | [`6150e8d39e`](https://github.com/alandtse/open-shaders/commit/6150e8d39ea9b096d35c221d9f1aa19f80608844) · [#726](https://github.com/alandtse/open-shaders/pull/726)                                             | Preserve shared compute buffers               | Required grass-side state-restoration semantics even when the new Wind feature is not adopted.                                                                                                                    |
| OS     | [`01b4a5bd5d`](https://github.com/alandtse/open-shaders/commit/01b4a5bd5d65c58a113c25feae1c94dc6ca90f4b) · [#752](https://github.com/alandtse/open-shaders/pull/752)                                             | Projection, Hi-Z scaling and capacity limits  | Required together with #817. Separate Hi-Z sizing from density/LOD pixel metrics; calculate capacities from stride and eye count.                                                                                 |
| OS     | [`c9e628dc5c`](https://github.com/alandtse/open-shaders/commit/c9e628dc5c96199c71d7a2a12fb7002ac69807a9) · [#810](https://github.com/alandtse/open-shaders/pull/810)                                             | Restore PBR directional shadows               | Required semantics, adapted to CSX shadow helpers and eye coordinates, not a literal replacement with the OS helper.                                                                                              |
| OS     | [`404024aea2`](https://github.com/alandtse/open-shaders/commit/404024aea2c2376343febf2fcc1084d837d2f98b) · [#815](https://github.com/alandtse/open-shaders/pull/815)                                             | Remove destroyed shapes from pending captures | Required together with #822. Removal must invalidate queued captures, not allow a destroyed shape to be re-added.                                                                                                 |
| OS     | [`8444f3c628`](https://github.com/alandtse/open-shaders/commit/8444f3c628feaa51e3de0415f83c29269ca08e04) · [#817](https://github.com/alandtse/open-shaders/pull/817)                                             | Correct optimized VR grass placement          | Required. CameraViewProj[eye] times eye-relative absolute instance position; avoid the representative-shape translation being applied twice.                                                                      |
| OS     | [`b0f16fd4c2`](https://github.com/alandtse/open-shaders/commit/b0f16fd4c292704c4f09591241eeaf5d687f8c7f) · [#816](https://github.com/alandtse/open-shaders/pull/816)                                             | Real runtime Enabled transition               | Required baseline. Both engine patches, applied state, grass permutations and lifecycle must switch together. Extend for CSX generation/readiness/fallback handling.                                              |
| OS     | [`f938b5344e`](https://github.com/alandtse/open-shaders/commit/f938b5344e2a321f74ad1d470070cbd5c198f77a) · [#822](https://github.com/alandtse/open-shaders/pull/822)                                             | Snapshot capture state at staging             | Required. Bounds, triangle count and wave period are snapshots; diffuse texture is retained; deferred consumption does not dereference a freed shape.                                                             |
| OS     | [`2eedd983b3`](https://github.com/alandtse/open-shaders/commit/2eedd983b3d315ea95fa08a9d89d00634f521c50) · [#804](https://github.com/alandtse/open-shaders/pull/804)                                             | Grass profiler zone lifetime fix              | Conditional: required if importing the spanning Grass::Draw profiler zone. Prefer existing correctly scoped CSX instrumentation where possible.                                                                   |
| OS     | [`7acc8d70ad`](https://github.com/alandtse/open-shaders/commit/7acc8d70ad1dd60fb3ec46319a1f836f5d4065ca) · [#634](https://github.com/alandtse/open-shaders/pull/634)                                             | Shared wind and replacement grass collision   | Related, deliberately separate scope. Current OS culling payload depends on this. Adapt the payload to existing CSX deformation rather than silently importing 96 files.                                          |
| OS     | [`f74c58d42d`](https://github.com/alandtse/open-shaders/commit/f74c58d42d4ff283409a847f3a780b99e870c3f0) · [#757](https://github.com/alandtse/open-shaders/pull/757)                                             | Revert improved grass transparency            | Preserve the revert of #754. Do not reintroduce the discarded alpha scheme while porting historical grass commits.                                                                                                |

### 3.1 Proposals that are not merged donor fixes

| Source / head commit                                                                                                                 | Verified status on 1 October 2026 | Treatment                                                                                                                                                                                          |
| ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| OS [#653](https://github.com/alandtse/open-shaders/pull/653), `918864d3fc052280172872fff0200e9ade21a298`                             | Closed, **not merged**            | Useful diagnosis of CPU/GPU frustum mismatch. Reimplement and validate the invariant; do not treat this abandoned draft as a proven patch. Its own notes flag insufficient off-centre mesh bounds. |
| OS [#812](https://github.com/alandtse/open-shaders/pull/812), `b1642822464ac82b0474809245462e1dd6079a2a`                             | Open draft, **not merged**        | Useful depth-ownership investigation. Changes the fallback source, not proof that the live depth source is correct for CSX.                                                                        |
| UP [#2804](https://github.com/community-shaders/skyrim-community-shaders/pull/2804), head `2be77692821a05e3d8ee24b5e63ea80929ae11b2` | Closed, **not merged**            | Repackages OS #815 + #822. Take the OS fixes once; do not wait for or additionally port this duplicate.                                                                                            |
| UP [#2810](https://github.com/community-shaders/skyrim-community-shaders/pull/2810), head `9b075469d5982ec9aa1442f3b74120876a0fb065` | Closed, **not merged**            | Repackages OS #816 but strips VR details. Its `_Vanilla` disk-cache suffix is not a substitute for CSX compile-state/ABI/generation identity.                                                      |

### 3.2 Related history to review without expanding the first port

-   OS #629 imports the original upstream Grass Optimizations feature; OS #632 contains the earlier culler failure-latch / narrowing fixes (`72ce6b2dc`, `5232d55d3`). The latter are covered, and extended, by UP #2781.
-   Shader-cache lineage: OS #613 (`537795846`, grass depth/stencil descriptor alias), #616 (per-descriptor define digest), #764 (`800af745`, unreadable timestamps), and UP #2780 (their upstream port plus a timestamp race). Compare against CSX's existing cache implementation; port missing semantics only.
-   OS #634's later wind/collision changes include #760, #767 and #794. They are relevant dependencies of current OS files, not mandatory features for this CSX integration.
-   UP #2614 is the earlier Grass Optimizations development PR; use landed #2688 as the foundation. UP #2697 (new transmission/SSS work) and #2766 (tree LOD billboard reuse) are not prerequisites for this first grass integration. Do not import them by taking an unbounded newer branch.
-   OS #796 release/Beta metadata is not evidence that CSX has passed qualification. Keep CSX's new path opt-in until the gates below pass.

## 4. Current-code findings and required CSX adaptations

### 4.1 New PBR grass is not just a shader switch

CSX's reviewed `TruePBR.h` has no `SetupGrassMaterial` entry, its `ShaderConstants` section lacks the added `GrassPS` mapping, and the reviewed `RunGrass` still implements basic/complex grass rather than the upstream reintroduced PBR branch. The standard `GrassOptimizations.h` path is absent at this target. Treat this as an actual feature integration, not merely enabling a dormant checkbox.

UP #2709 changes `TruePBR.cpp/.h`, `Hooks.cpp`, `ShaderCache.cpp/.h`, `Common/PBR.hlsli`, `RunGrass.hlsl`, and the optimizer/bucket files. Its material construction order is important: copy/populate all PBR members, preserve the diffuse chosen by the grass form, supply valid texture defaults, then intern/attach the material. Attaching before the RMAOS/normal fields are populated can deduplicate different materials into the same material.

Port the custom grass render-pass selection, SetupTechnique, SetupMaterial, descriptor/define generation and constant mappings as a single contract. Verify every touched relocation and vtable index for Skyrim VR; an upstream SE/AE address pair alone is not proof of the VR byte offset.

The PBR shader resource contract introduced upstream is PS `b1` for PBR material constants, normal/RMAOS/subsurface textures and samplers at `t2–t4`/`s2–s4`, and PBR-only `SV_Target5` reflectance. Retain CSX's shadow mask and other binding ownership. Bind safe defaults or explicitly clear unused optional slots; do not depend on a previous grass material having populated them.

**Tests:** two grass forms using the same diffuse but different RMAOS/normal/subsurface settings must remain distinct with GO both off and on. Repeat with changed load order and alternating draw order. Legacy complex-grass half-texture detection must not run on explicitly authored PBR textures.

### 4.2 Preserve CSX lighting, not merely the names of the toggles

CSX currently has a shared `RenderBasicGrass` path for Grass Lighting disabled at boot or at runtime, `ApplyGrassWetDarkening`, optional Foliage Lighting scattering, wrapped lighting limited to non-complex grass, semantic `Color::*` calls, and `Color::ApplyAmbientBalance`.

Its current enhanced grass path samples `Skylighting::SampleWithShadow`, rebases the VR lookup into eye-0 coordinates, and combines available skylight visibility with existing soft shadow using `min`. Do not replace this with the original upstream PBR code's unconditional shadow assignments or old OS-only helper APIs.

Preserve these legacy branches and insert a separate PBR evaluation path. Define how the PBR path responds to both `TruePBR.Enabled` and `GrassLighting.Enabled`, including already-created PBR materials when a setting is turned off. Disabled must select valid basic shading, not the wrong shader layout or stale outputs. Keep global PBR, authored PBR grass, and GO logically independent.

Avoid double-counting PBR subsurface transmission and CSX's optional foliage scattering. Decide ownership explicitly; do not add both lobes merely because both settings exist. Carry Wetterness using the existing shared wetness/drying state; do not introduce a second rain accumulator. The PBR dry baseline should remain its authored material, not be silently forced to legacy complex-grass glossiness.

### 4.3 Preserve the actual constant-buffer and vertex contracts

CSX currently uses `PerGeometry` VS `b2` / PS `b3`. VR arrays occupy c0/c8/c16/c24; fog starts c32, scale is c37, and the final lane is `ShadowClampValue` at c37.w. Do not copy a flat pixel b2 declaration into this file without updating and verifying its CPU producer. Removing use of a legacy lighting clamp does not authorize shrinking/repacking the buffer.

Legacy grass uses VS `b7`/`b8` for fade data. OS optimized grass reuses `b7` for eye index and eye-slot base only because those legacy declarations are excluded under the optimized permutation. Preserve that exclusivity.

The optimized input layout must be keyed by the actual mesh vertex descriptor. The second eye's vertex stream uses `StartInstanceLocation`; the extras SRV uses an explicit eye-slot base. The final OS contract deliberately keeps these in agreement. Test a nonzero second-eye base and unequal survivor counts.

### 4.4 Optimized placement and legacy collision need separate adapters

For optimized vertices, decode an absolute world position, subtract the relevant `CameraPosAdjust[eye]`, then project with `CameraViewProj[eye]`. Preserve VR output packing. Do not multiply absolute positions by the representative shape's `WorldViewProj` again. Use the matching previous position/previous camera conventions for motion vectors.

This is not permission to change the legacy path's existing `WorldViewProj` use: its inputs are in a different coordinate space.

A second concrete trap exists in CSX's existing `GrassCollision::GetDisplacedPosition`: it internally multiplies both the vertex and instance root by `World[0]`. Passing already-world-space optimized positions to that wrapper would repeat the same class of translation mistake.

Recommended minimal adapter: extract the collision evaluation after the transforms into a helper that accepts the already-transformed vertex and root in the exact existing eye-0/collision-field coordinate system. Keep the old wrapper performing `World[0]` for legacy grass. The optimized wrapper supplies correctly rebased vertex/root coordinates once. Preserve the same current/previous height samples, displacement scaling and vertex-alpha behaviour. Use a no-op DXBC comparison for legacy permutations where feasible.

The existing wind calculation depends on per-vertex colour/height weighting. First keep it in the VS, using the same instance-local phase inputs as before. Only precompute mathematically shared components in a later measured change. Do not assume the new OS spring-field samples can replace the old CSX height-field collision without a behaviour change.

### 4.5 Current OS culling still has a consistency gap

In reviewed `GrassOptimizations::UpdateGrass`, CPU bucket/slice rejection uses `ComputeFrustumPlanes` with `NiFrustum` and substituted eye translations. VR GPU rejection uses planes extracted from cached per-eye CameraViewProj. A shape rejected by the first stage cannot be rescued by the second. OS #653 diagnoses this but was closed without merge.

CSX must use a shared, explicitly defined culling coordinate system for both stages. For a CPU test in a common origin frame, transform each eye's plane constant into that common frame; do not subtract one midpoint camera from the AABB and then reuse unrelated per-eye-relative planes. A CPU bucket may be rejected only if its conservatively padded bound is outside BOTH eye frusta.

Use actual renderer projection data, not desktop preview dimensions or assumptions of symmetric fTop/fBottom. If using unjittered planes, include the full raster-jitter envelope and intended safety margin. If no reliable projection snapshot is available, skip rejection rather than hide grass.

Bounds must include instance scaling, nonzero model-bound centre, supported wind/collision displacement and all selected LOD meshes. A root-centred CPU radius of merely modelRadius + 64 is not a general proof that it encloses a GPU bound expanded by BoundCenter.

### 4.5a Stereo-consistent quality decisions

The donor calls `CullEye` separately for both eyes. Although it shares the random seed, projected size, fade and LOD thresholds are evaluated using each eye's distance. Shared randomness alone does not guarantee equal density or LOD between eyes. For the later quality stage, prefer an eye-consistent per-instance density/LOD decision based on a conservative binocular projected-size metric (for example, the larger valid projected size), then retain independent per-eye frustum/occlusion decisions. Derive size from the actual projection and selected quality-resolution policy rather than assuming a symmetric frustum. Validate this proposed policy visually and measure its cost; do not describe it as an already-implemented OS fix. Keep all quality removal off in the initial parity mode.

### 4.6 Hi-Z requires a CSX depth contract, not an arbitrary depth SRV

**Version 2 amendment:** first evaluate/reuse the existing Hybrid maximum-depth builder and tests described in Section 0. Its native producer/history contract is not the grass consumer contract. The remaining requirements below still apply; implement missing pieces around the existing foundation, not a second independent hierarchy by default.

The donor's `HiZPyramid` first tries live kMAIN and falls back to a prepass copy/Terrain Blending backup. Its comments disagree about frame ordering, and OS #812 explicitly documents that the old rationale was wrong in a captured frame. That proposal also leaves live-depth redirection unresolved. Do not port a comment as proof of freshness.

CSX Terrain Blending has its own VR depth selection, shadowmask overrides, culling policy and render-scale integration. Add a small typed view/snapshot using the existing CSX resource ownership where possible. Proposed fields: SRV with retained lifetime; actual view format; depth convention; producing frame and camera/view identity; resource generation; full allocation extent; active render rectangles per eye; and whether a foveated/nonlinear mapping is in effect. These are proposed requirements, not an assertion that an identically named CSX API already exists.

Build Hi-Z only from a source proven to contain the current frame's relevant opaque occluders for the player view. If provenance, generation, layout or format is untrusted, disable Hi-Z for that frame and keep frustum/distance drawing. A previous frame's depth is not a safe drop-in fallback without conservative reprojection/disocclusion handling.

Use a max-depth pyramid for the currently assumed conventional depth path, conservative far-depth padding and complete footprint coverage. A future reverse-Z port must change the complete reduction/comparison contract, not one comparison. Keep depth layers/rectangles per eye from contaminating each other; clamping final sample coordinates alone does not prove coarse mip texels were reduced independently.

Separate allocation dimensions, active internal rendering dimensions and the metric used for density/LOD thresholds. During DLSS/FSR/native or render-scale changes, use the accepted CSX frame/resource generation. Do not create another per-frame resolution polling controller. If CSX's foveated mapping makes the depth layout nonuniform, either use the correct map in culling or disable Hi-Z for that path until validated.

Do not attempt to obtain a cheap current depth copy by changing unrelated Terrain Blending replay/depth-test semantics.

### 4.7 Donor failure paths are not sufficient for a robust CSX port

Observed examples in the pinned OS code:

-   `UpdateGrass` resets cull state and discards pending captures if the culler/context/resources are unavailable; its comment explicitly skips drawing rather than restoring vanilla.
-   `UploadEyeIndexCB` silently returns when Map fails, and the caller still issues indirect draws.
-   `DrawGrassIndirect` continues after `GetOptimizedInputLayout` returns null, leaving a previously bound layout usable by accident.
-   Hi-Z base/SPD use a raw null-pointer retry pattern; upstream #2781 supplies the broader compile-failure latch.
-   Some paths call `VanillaDrawInstanceTriShape`, but vanilla geometry is not compatible with an optimized VS and the vanilla fade-buffer skip patch. That is not automatically a valid fallback.

Use a feature-level safe transition first; per-bucket fallback is optional only after its whole draw-state contract is proved. Preflight the resources needed by all upcoming draws, or move necessary per-eye constant uploads before committing to optimized submission. Failed resource preparation must not leave stale eye state, stale indirect counts, an untracked partial frame, or a permanent missing-grass state.

Track requested, prepared and applied modes separately. Before enabling, have validated resources, hooks, shader permutations and generation ready. At a safe engine boundary, switch the draw representation, both engine patches, define snapshot and live shader generation as one transaction. On disable/rollback, restore BOTH patches including the fade-buffer path and the matching vanilla shaders. Preserve capture lifecycle information so re-enable or recovery can rebuild it. A restart-only opt-in is preferable to advertising a runtime transition that is not yet safe.

A compile failure should latch until explicit reload/cache invalidation. Depth-only/Hi-Z failure should normally disable that optional optimization; failure of the basic optimized representation should select the full vanilla representation. Log once per failure/generation rather than per instance or frame.

### 4.8 Capture/material lifetimes and capacity are correctness requirements

Take BOTH OS #815 and #822. Keep deferred capture consumption free of shape dereferences. Snapshot all required immutable state while the shape is valid, retain texture resources, serialize queue ownership, and make destruction invalidate pending work.

Current donor `BucketKey`/`PendingCapture` still carry raw material pointers. This observation does not establish a reproduced use-after-free, but it does leave an identity/lifetime invariant to prove. Verify material interning lifetime, in-place changes and address reuse. Use retained immutable material identity or a generation-aware key if lifetime is not guaranteed. Test destruction after the queue swap, pointer reuse and material reload rather than only normal cell unload.

Use checked arithmetic for raw instance bytes, extra records, both eyes, all active LOD bins, indirect argument offsets and slotted constant buffers. Split/chunk oversized buckets or take the valid fallback; never silently clamp an instance count and lose the tail. Reset the count fields for every eye/tier while preserving CPU-set indirect fields.

The current OS payload has 32 compacted bytes plus six float4 extras = 128 output bytes per capacity slot per eye. With two eyes, the main bin alone is 256 bytes per instance of capacity; provisioned main/middle/far bins could total 768 bytes before source/origin buffers and other overhead. This is arithmetic for fully provisioned donor buffers, not measured CSX memory use. Removing the new wind payload should allow a smaller initial ABI, but size it from the actual implementation and verify every consumer.

Allocate on resource changes/capacity growth, batch dirty uploads, and release empty buckets. Avoid a new per-frame GPU readback. Reuse existing CSX timing and render-scale snapshots; do not rebuild camera planes or inspect D3D resources separately per grass shape.

## 5. Dependency-ordered implementation tasks

Each task should be its own reviewable commit or small commit group. Add source attribution and a port-ledger entry. Build intermediate states; do not accumulate all tasks into one untestable patch.

| Task                                      | Work                                                                                                                                                                                                                                                    | Required exit gate                                                                                                                                                     |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0 — Baseline and ledger                   | New branch from actual main-VR; record pinned donor refs, local source/build identity and existing settings. Read AGENTS/CLAUDE and relevant render-scale qualification docs. Inventory hooks, material flags and grass permutations.                   | Clean baseline build and repeatable captures/timings; no uncommitted user work overwritten.                                                                            |
| 1 — PBR materials without GO              | Port material construction, hook/technique chain, descriptors, GrassPS constants, resource defaults and PBR GBuffer output. Integrate existing CSX colour/shadow/wetness APIs.                                                                          | Authored PBR works with optimizer absent; two different RMAOS materials sharing a diffuse remain distinct; basic/complex and runtime-off paths remain valid.           |
| 2 — Shared geometry/deformation contract  | Add the optimized VS contract behind a disabled flag. Extract the collision helper with legacy wrapper preserved; retain existing wind. Document absolute, eye-relative, previous and clip spaces.                                                      | Legacy shader no-op checks where applicable; reference CPU/VS projection and collision fixtures agree.                                                                 |
| 3 — Capture/bucket infrastructure         | Stage immutable captures and removals, material-safe bucket keys, mesh identity, current #815/#822 lifetime fixes, checked capacity allocation and resource naming. Keep engine rendering vanilla.                                                      | Deterministic queue-swap/destruction and same-texture/different-material tests; failure injection preserves data and renderer.                                         |
| 4 — Conservative optimized stereo drawing | Adopt current descriptor layouts, per-eye halves/args, explicit eye-slot base, one decode/two eye evaluation and final #817 projection. Disable Hi-Z, mesh LOD, thinning and simplified shading initially; preserve original fade/distance semantics.   | Both eyes/colour/depth place grass correctly away from origin; GO on/off comparison at matched density, materials and deformation.                                     |
| 5 — Unified conservative culling          | Shared per-eye matrix-derived planes and coordinate conversion for CPU/GPU; conservative off-centre/deformed/LOD bounds. Separate quality rejection from pure visibility rejection.                                                                     | Property tests: CPU reject implies both-eye reject for the represented geometry; actual asymmetric HMD outer-edge, near-plane and cell-boundary tests pass.            |
| 6 — Safe activation and failure recovery  | Applied/prepared/requested state, shader-generation readiness, both reversible engine patches, capture retention/rebuild, cold boot off, runtime off/on, shader failure latches, resource preflight and checked Map/layout creation.                    | Every injected failure gives a known valid mode without crash, stale-eye draw, silent permanent grass loss or retry storm.                                             |
| 7a — Qualify existing Hybrid foundation   | Reconcile f17b833b56 onto current main-VR in an isolated candidate; rebuild and rerun exact-source tests; qualify native execution with GO off. Capture native/grass depth event order, actual source contents and camera/rectangle attribution.        | Native fallback/history/state restoration are runtime-tested separately; builder/source evidence is attributed to exact code; Advanced stays default.                  |
| 7b — Narrow shared Hi-Z provider          | Extract the existing conservative builder/view only as needed. Publish source/frame/phase/generation/eye-layout provenance; reuse one build only for equivalent admissible snapshots. Keep native routing and result history private to native culling. | Shared builder retains WARP coverage; native/grass demands work independently; no stale or wrong-phase reuse, new readback, or duplicated build for the same snapshot. |
| 7c — Grass consumer integration           | Adapt 2D SBS sampling to the two-layer eye-local view; retain grass-owned frustum/instance/LOD/counter logic and validated deformed bounds. Preserve eight-UAV budget and valid draw fallback.                                                          | Combined Advanced/Hybrid/disabled-native × GO/grass-Hi-Z matrix passes; TB/layout changes fail open; performance includes actual builder/consumer cost.                |
| 8 — Explicit quality controls             | Density thinning, min size, mesh-cost bias, mid/far mesh swap and simplified shading. Stable original-instance identity for stochastic choices; compatible PBR/material handling for LOD assets.                                                        | No stereo rivalry or temporal reassignment from compaction; missing LOD asset preserves full mesh. Benchmarks report the quality change separately.                    |
| 9 — CSX UI/cache/profiling integration    | Existing Essentials/Advanced/Performance model, validated JSON/presets, DevBench action/schema, performance capture/restore, feature ABI and compile-generation identity. Keep shared compute state and correctly scoped profiling.                     | Settings round-trip/reset/malformed input; cold/warm/managed caches for all modes; DevBench A/B proves the full optimizer is actually off.                             |
| 10 — Qualification and release            | Full shader/configuration matrix, real-HMD + SE/AE regression runs, load/fast-travel soak, CPU/GPU/VRAM reporting and final fork-loss audit.                                                                                                            | Evidence meets agreed budgets; user-visible defaults conservative; rollback confirmed; update main-VR only through reviewed integration.                               |

Tasks 6 and 9 have early prerequisites: define the state/cache contracts before Task 4, even though their full failure and UI qualification occurs later. Never enable an intermediate build without a valid disable/recovery path. Tasks 7a–7c and 8 must not block delivery of a correct opt-in batching/frustum-only path. Hybrid qualification can proceed separately with GO off; do not make PBR materials depend on selecting Hybrid.

## 6. Proposed file ownership / minimal patch map

| Area                       | Files / integration points                                                                                                                                        | Rule                                                                                                                                                                                                 |
| -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PBR material setup         | `src/TruePBR.cpp`, `.h`; relevant `src/Hooks.cpp` grass creation hook                                                                                             | Preserve CSX runtime PBR behaviour, validate VR call sites.                                                                                                                                          |
| Technique/cache ABI        | `src/ShaderCache.cpp`, `.h`; existing feature define snapshots and ABI metadata                                                                                   | Add GrassPS/technique support and exact define identity; do not replace CSX cache with upstream descriptor-only logic.                                                                               |
| Grass shader               | `package/Shaders/RunGrass.hlsl`                                                                                                                                   | Keep legacy/basic/complex branches. Add PBR and optimized representation with explicit interfaces.                                                                                                   |
| PBR evaluation             | `package/Shaders/Common/PBR.hlsli` and existing lighting helpers                                                                                                  | Add narrowly scoped grass evaluation; avoid changing unrelated material BRDFs.                                                                                                                       |
| Legacy deformation adapter | `features/Grass Collision/Shaders/GrassCollision/GrassCollision.hlsli`; grass VS helpers                                                                          | Shared transformed-coordinate evaluator with old wrapper intact. No new wind simulation in this port.                                                                                                |
| Optimizer infrastructure   | New `src/Features/GrassOptimizations.{h,cpp}` and `GrassOptimizations/GrassBucketStore.*`, `GrassMeshLibrary.*`; a narrow shared Hi-Z view adapter when qualified | Feature-owned buckets/draws; reuse the existing Hybrid reduction foundation rather than copying a second `HiZPyramid` implementation by default.                                                     |
| Feature shader/package     | New `features/Grass Optimizations/Shaders/GrassOptimizations/*`, metadata and packaging                                                                           | Match C++/HLSL ABI, preserve eight-UAV budget, include signature VS and required SPD assets only if used.                                                                                            |
| Registration/UI/settings   | Existing CSX feature list, globals, category, menu/performance and DevBench schema/action registration                                                            | No broad framework migration just to satisfy a donor API name.                                                                                                                                       |
| Depth contract             | Existing `VRHybridCulling` builder/policy/tests plus CSX State/VR/Upscaling ownership as appropriate                                                              | Qualified read-only snapshot; native and grass consumers independent; reuse a built texture only when source/frame/phase/eyes match. No second resolution controller or transfer of TB policy to GO. |
| Tests/build                | Existing shader matrices, controller/unit tests, feature-version and provenance checks                                                                            | Add relevant combinations and evidence, not just default permutations.                                                                                                                               |

## 7. Qualification matrix and measurements

### 7.1 Shader and material coverage

Compile valid combinations across VR and supported flat runtimes: GO on/off; Grass Lighting compiled on/off and runtime Enabled on/off; global PBR on/off; legacy basic/complex/authored PBR material; colour/depth/depth-stencil/alpha-test passes; collision on/off; relevant Light Limit Fix, Skylighting, IBL, shadow and Wetterness variants. Include cached permutations, not only first compilation.

Assert reflection/layout facts: no undeclared Reflectance write in non-PBR; valid fallback writes for PBR GBuffer layout; CPU GrassPS material offsets match HLSL; VR PerGeometry size/slots unchanged; b7 exclusivity; correct extras stride and eye base; every referenced source/LOD descriptor has a compatible input layout.

Test true PBR authored assets rather than expecting a vanilla grass texture pack to acquire material maps automatically. Include optional-texture defaults, unusual but supported vertex descriptors, invalid/missing meshes and same diffuse/different PBR material pairs.

### 7.2 Image and geometry correctness

Use matched scenes at Guardian Stones/Riverwood and other world positions well away from zero. Include slopes, off-centre bounds, dense overlapping cards, rocks/walls, near-camera grass, eye outer edges and the stereo seam. Freeze camera/weather when comparing colour. Move both camera and colliders when comparing history/motion vectors.

Null-driver testing is useful for crashes, shader compilation and reproducible captures. It does not qualify asymmetric projection, stereo rivalry or headset motion. OS #817 explicitly reports that real asymmetric-HMD coverage was not included. Test the actual HMD separately.

Capture native, DLAA, DLSS and FSR where supported; change render scale and profiles; include CSX foveated paths and relevant NR modes without modifying their control plane. Check both eyes' colour, alpha/depth cutouts, world positions, normals, velocity and indirect counts. Test pause/resume, loading screen, door transition, fast travel and camera reset/history invalidation.

### 7.3 Lifecycle and injected failures

Exercise capture queued then destroyed; destruction after queue swap; remove/re-add/address reuse; material reload; repeated exterior/interior cell traversal; save load including an interrupted/failed load path; unload while feature disabled; re-enable without requiring a cell reload; empty buckets and buffer growth.

Inject failure of culler compile, complex-detection compile, Hi-Z base/SPD compile, signature VS, input layout creation, Map and buffer/texture creation. Verify retry latches reset only on explicit generation/reload actions and all stale counts/state are invalidated. Test a foreign modification at either patch site: do not partially patch or overwrite another plugin's bytes.

### 7.4 Performance methodology

Report these separate configurations with the SAME grass population and material pack unless the row explicitly changes quality:

A. Existing CSX baseline, optimizer off.
B. PBR material path enabled, optimizer off (isolates PBR shading cost).
C. Optimized batching/submission with thinning, Hi-Z and mesh LOD off.
D. C plus conservative frustum culling.
E. D plus Hi-Z, in both open and occluded scenes.
F. E plus explicitly documented density/LOD/simple-shading settings.

Version 2: repeat relevant grass rows across Advanced and experimental Hybrid object culling, plus native-culling-disabled grass-only source admission, as specified in Section 0.7. Separate shared-build work from consumer work and do not sum inclusive nested GPU scopes. Record the effective backend and fallback/rejection rates.
Record CPU main/render-thread and grass traversal/upload/submit times; GPU grass/cull/Hi-Z and total scene time; p50/p95/p99 frame times and transition spikes; draw/dispatch counts; captured and surviving instances per eye/tier; allocation/VRAM high-water marks; current build/source/settings/scale identities. Use existing asynchronous GPU timing, not synchronous readback in the normal rendering path.

Interpretation: fewer CPU submissions can help without reducing pixel/alpha overdraw. PBR may add GPU shading cost. Hi-Z can cost more than it saves in an open vista. Density/LOD reductions can provide worthwhile performance, but are not a quality-neutral implementation improvement. Do not extrapolate the original OS null-driver/OnVisible-only benchmarks into a CSX FPS claim.

## 8. Suggested initial defaults

During development: optimizer opt-in/off by default, Hi-Z off, mesh LOD off, simplified shading off, no mesh-cost/density/min-pixel removal in the parity mode, and original CSX distance/fade policy. Provide an explicit parity mode rather than relying on a slider combination that still secretly drops instances.

At release: only enable a path by default after its own qualification; the batching path and optional Hi-Z do not need the same default. Show requested/applied state and a concise reason when a capability falls back. Do not silently overwrite a saved quality preference merely because a transient resource failed.

## 9. Codex starting prompt

```text
Repository: ParticleTroned/skyrim-community-shaders
Target: main-VR (case sensitive)
Task: Implement PBR grass and Grass Optimizations in staged, minimal-churn commits using this handover.

First read AGENTS.md, applicable .claude/CLAUDE.md, and the existing VR render-scale qualification / build-provenance instructions. Check git status and HEAD. Do not reset, overwrite or stash user changes automatically. Create a separate feature branch from the current main-VR after reconciling any drift from the reviewed baseline dab1874a76fd39175dcefdc52110ba69d7284e12.

Reference OS dev d6bfd6b7b552e70d23eec00fce4e7f14cbece0af and upstream dev 97a7db04f50643efce30e8632a32082b8eebcd3f. Use corrected final grass semantics, not whole sync-merge cherry-picks. Keep a source-commit / file-hunk / CSX-adaptation ledger.

Also inspect origin/codex/vr-hybrid-hiz-culling at f17b833b56b5078527bb9a5b413e033455ad855d and its docs/development/vr-hybrid-culling*.md. Its published head has no post-rebase build/test evidence and is not tested in-game. Keep Advanced default. Do not merge Hybrid untested into main-VR or build a duplicate grass Hi-Z foundation. Qualify and extract only the reusable conservative builder/view when the optional grass Hi-Z stage is reached; native OBB results/history remain separate from grass instance compaction.

Begin with Task 0 and Task 1 only. Make PBR grass work on the existing renderer before activating GO. Preserve existing CSX basic/complex shading, runtime disabled path, Wetterness, Foliage Lighting, semantic colour/ambient balance, Skylighting, shadow ownership and cache generations. Do not import the new shared Wind system or tree billboard work.

Mandatory final fixes: material fully initialized before interning; material-distinct buckets; current VR input layouts and eye offsets; camera-relative optimized projection; both capture-lifetime fixes; eight-UAV culling; bounded capacities; shared compute state preservation; a real reversible draw/shader/fade-buffer transition; compile/resource failure recovery.

Do not call a vanilla draw helper while optimized shader/patch state is active. Do not feed absolute optimized positions to the legacy collision helper that applies World[0]. Do not use stale or layout-unknown depth for Hi-Z. A Hybrid array SRV is not compatible with OS grass's 2D SBS declaration, and a native-phase pyramid is not automatically fresh or available at grass-cull time. Do not claim asymmetric HMD validation from a null-driver test.

For each completed task, report changed files, source commits, preserved CSX differences, tests actually run, failures/untested cases and next dependency. Performance claims require matched population/material/scale measurements. Do not push or merge until explicitly requested.
```

## 10. Local history verification (not executed during this review)

This is a read-only audit apart from fetching remotes. Use an existing checkout. Choose unused remote names; never overwrite the user's existing remote configuration. The commands below do not cherry-pick or modify tracked files.

```powershell
# From the user's existing CSX checkout, after reviewing AGENTS.md.
git status --short
git branch --show-current
git remote -v

# Add these names only if they do not already exist; verify URLs if they do.
git remote add grass-audit-os https://github.com/alandtse/open-shaders.git
git remote add grass-audit-upstream https://github.com/community-shaders/skyrim-community-shaders.git
git fetch grass-audit-os dev
git fetch grass-audit-upstream dev

$os = 'd6bfd6b7b552e70d23eec00fce4e7f14cbece0af'
$up = '97a7db04f50643efce30e8632a32082b8eebcd3f'
$paths = @(
  'src/Features/GrassOptimizations.cpp',
  'src/Features/GrassOptimizations.h',
  'src/Features/GrassOptimizations',
  'features/Grass Optimizations',
  'package/Shaders/RunGrass.hlsl',
  'src/Features/GrassLighting.cpp',
  'src/Features/GrassLighting.h',
  'features/Grass Lighting',
  'features/Grass Collision',
  'src/TruePBR.cpp',
  'src/TruePBR.h',
  'src/ShaderCache.cpp',
  'src/ShaderCache.h',
  'src/Hooks.cpp',
  'package/Shaders/Common/PBR.hlsli'
)
# A deliberately broad superset, including shared-file commits that need classification.
git log --full-history --date=iso-strict '--format=%H%x09%aI%x09%s' --since=2026-08-01 $os -- $paths
git log --full-history --date=iso-strict '--format=%H%x09%aI%x09%s' --since=2026-08-01 $up -- $paths
# Title search catches cross-cutting fixes that did not touch the original feature directory.
git log --date=iso-strict '--format=%H%x09%aI%x09%s' --since=2026-08-01 --regexp-ignore-case --grep='grass' $os
git log --date=iso-strict '--format=%H%x09%aI%x09%s' --since=2026-08-01 --regexp-ignore-case --grep='grass' $up
```

Use the resulting history to resolve individual commits inside broad merges such as #663 and to update the ledger for newer donor changes. Review patch content, not just titles. For non-merge patch-equivalence checks, use stable patch IDs; manually inspect conflicted/adapted ports. An ancestry test cannot establish that CSX retained a feature that was deliberately dropped during a sync.

## 11. Source map and audit limits

Primary source links are embedded in the inventory. The following pinned files support the code-specific findings:

-   [CSX: AGENTS.md](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/AGENTS.md)
-   [CSX: src/TruePBR.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/src/TruePBR.h)
-   [CSX: src/ShaderCache.h](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/src/ShaderCache.h)
-   [CSX: src/ShaderCache.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/src/ShaderCache.cpp)
-   [CSX: src/Features/GrassLighting.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/src/Features/GrassLighting.cpp)
-   [CSX: package/Shaders/RunGrass.hlsl](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/package/Shaders/RunGrass.hlsl)
-   [CSX: features/Grass Collision/Shaders/GrassCollision/GrassCollision.hlsli](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/features/Grass%20Collision/Shaders/GrassCollision/GrassCollision.hlsli)
-   [CSX: src/Features/TerrainBlending.cpp](https://github.com/ParticleTroned/skyrim-community-shaders/blob/dab1874a76fd39175dcefdc52110ba69d7284e12/src/Features/TerrainBlending.cpp)
-   [OS: AGENTS.md](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/AGENTS.md)
-   [OS: src/Features/GrassOptimizations.h](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/src/Features/GrassOptimizations.h)
-   [OS: src/Features/GrassOptimizations.cpp](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/src/Features/GrassOptimizations.cpp)
-   [OS: src/Features/GrassOptimizations/GrassBucketStore.h](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/src/Features/GrassOptimizations/GrassBucketStore.h)
-   [OS: src/Features/GrassOptimizations/HiZPyramid.cpp](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/src/Features/GrassOptimizations/HiZPyramid.cpp)
-   [OS: features/Grass Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl](https://github.com/alandtse/open-shaders/blob/d6bfd6b7b552e70d23eec00fce4e7f14cbece0af/features/Grass%20Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl)

The review used live GitHub branch metadata, source-file inspection, commit diffs and PR histories. It did not clone the complete commit graph, verify every local patch ID, disassemble the current CSX runtime, or compile/run the proposed port. The inventory consolidates relevant landed change sets rather than asserting an exhaustive list of every intermediate PR commit. Candidate CSX failure modes are identified from the shown control flow; their runtime frequency was not measured. Final validation must use the actual local source, deployed DLL/shader package, assets and headset.

<!-- END VERBATIM SOURCE B -->
