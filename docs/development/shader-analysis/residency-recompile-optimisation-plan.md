# CSX shader residency and recompile optimisation action plan

Date: 2026-08-20

## Outcome

Reduce shader compilation, cache churn, restart requirements, and first-use hitches without degrading rendering quality or making runtime feature changes unsafe.

The principal architectural change is to distinguish three feature states:

- **Installed** — code and assets are present.
- **Resident** — required shaders and resources are compiled/loaded and may be used without a restart.
- **Active** — the feature currently contributes work or shading.

Ordinary behavioural choices should move from compile-time defines to runtime data where the GPU and resource cost is acceptable. Compile-time variants remain appropriate for structural changes such as shader stage, input/output signature, resource layout, render-target format, topology, vendor implementation, or materially different algorithms.

## Current implementation baseline

- 39 feature INI declarations.
- 174 HLSL/HLSLI source files.
- `Feature::loaded` currently combines residency and activation concepts.
- `ShaderCache` generates macros from each loaded feature's `HasShaderDefine(shaderType)` result; those macros form part of compilation and cache identity.
- Existing feature scoping by shader family is useful, but some scopes need auditing.
- Several features already have runtime enable data inside resident shaders. Skylighting is the clearest reference implementation; Hair Specular is a promising small integrated pilot.
- The cache comparison tooling presently identifies changed cache paths, but it does not yet attribute differences to semantic feature, pass, pipeline, resources, or invalidation cause.
- The CSX VR menu has distinct in-scene vertex/pixel and submit-hook compute routes and must be classified separately from ordinary effect shaders.

## Guardrails

1. No intentional image-quality reduction.
2. An `Active` change must not synchronously wait for shader compilation.
3. GPU state and executable shader swaps occur only at a known safe frame boundary.
4. `Disable at Boot` remains available as a hard residency choice for compatibility and memory control.
5. Generic resident shader cache keys depend on structural state and resident capability, not the current active mask.
6. Soft-off retains required shaders and resources unless a feature-specific policy explicitly permits safe release.
7. No universal mega-shader. Resident supersets are bounded by shader family and measured register/resource pressure.
8. VR versus flat, shader stage, signatures, resource layouts, target formats, vendor paths, and topology remain structural until proven otherwise.
9. Every migration is independently reversible.
10. A feature does not graduate from pilot status until output, stability, CPU cost, GPU cost, cache behaviour, and cold/warm startup are measured.

## Target model

### Feature states

| State | Meaning | May compile shaders? | May schedule work? |
|---|---|---:|---:|
| Not installed | Code/assets unavailable | No | No |
| Installed, not resident | Available next launch or on explicit load | Only by an explicit residency operation | No |
| Resident, inactive | Immediately activatable | Already compiled/loaded | No independent work; integrated shader branch is inactive |
| Resident, active | Fully enabled | No compile required for ordinary activation | Yes |

During migration, legacy `loaded` behaviour should map to `Resident`, with explicit `Active` state added feature by feature.

### Composition choices

| Feature shape | Preferred mechanism | Examples/candidates |
|---|---|---|
| Independent pass | Retain pipeline/resources; omit dispatch/draw while inactive | Screen-space or post-process passes after dependency audit |
| Integrated behavioural term | Bounded resident superset plus runtime active flag | Hair Specular; later Wetness |
| One-of-N substantial algorithm | Generic fallback, then background specialisation; possibly a linker class | Mutually exclusive lighting/reconstruction models |
| Structural change | Compile-time variant | Stage, signature, resource layout, target format, VR/flat, vendor path |
| Linear or table-driven operation | Runtime data, LUT, or small restricted operation list | Colour transforms and simple coefficients |
| Independent overlay | Explicit overlay pipeline classification and scheduling | CSX menu in-scene and submit-hook routes |

## Milestone 0 — Preserve and extend the baseline

### Actions

- Record the source revision, working-tree patch, deployed DLL hash, settings/preset, driver, HMD route, resolution, and active shader cache for every comparison.
- Retain representative scenes, including the existing repeatable Riften and interior/Breezehome routes.
- Capture cold-cache startup, warm-cache startup, total compile count/time, cache files/bytes, main-thread hitch distribution, first-use cost, CPU feature-hook time, and GPU pass/family time.
- Add compile lifecycle telemetry around:
  - preprocessing;
  - `D3DCompileFromFile`;
  - shader-object creation;
  - first bind/use or explicit warm-up;
  - cache hit/miss;
  - invalidation cause;
  - requesting feature, family, stage, entry point, and structural key.
- Keep loaded-active, resident-inactive, hard-unloaded, and relaunch-required results distinct.

### Exit gate

- Repeated measurements are stable enough to distinguish a change of approximately 5% in compile time and the agreed per-pass GPU thresholds.
- Cache snapshots can be tied unambiguously to configuration and source state.
- Every compilation event has a recorded reason.

## Milestone 1 — Build a machine-readable shader and pass manifest

This is the prerequisite for safe optimisation. The existing family classification is useful, but pipeline membership, resources, ordering, and ownership are not yet complete enough.

### Manifest fields

For every feature, shader, and pass record:

- installed/resident/active capability;
- owner feature or infrastructure component;
- shader family, stage, profile, entry point, and source/include closure;
- compile-time defines and whether each is structural or behavioural;
- pass/pipeline membership and invocation site;
- inputs, outputs, constant buffers, SRVs, UAVs, samplers, register slots, formats, and dimensions;
- read/write access and resource lifetime;
- ordering constraints and upstream/downstream dependencies;
- CPU hooks and per-frame resource updates;
- profiling timer and cache path;
- flat, VR, stereo, overlay, vendor, or compatibility route;
- current cache/invalidation scope;
- proposed composition mechanism and migration status.

### Actions

- Generate the first draft from the feature list, `HasShaderDefine`, shader compile calls, HLSL includes/register declarations, and profiler registrations.
- Add manual annotations for dynamic bindings and engine-owned resources that static scanning cannot prove.
- Treat the CSX menu's in-scene VS/PS and submit-hook compute shader as separate overlay pipelines.
- Add a read-only DevBench view/export for the runtime-active graph, resource bindings, structural key, resident set, and active set.
- Make CI fail when a production shader or pass is unowned or a manifest entry goes stale.

### Exit gate

- All 39 feature declarations and all 174 shader sources are classified.
- Every production pass has an owner, invocation point, ordered dependencies, and resource I/O description.
- No shader is labelled independent until its inputs, outputs, and ordering prove that it is independently schedulable.

## Milestone 2 — Take low-risk cache and recompile wins

These changes should precede broad shader architecture work.

### Actions

1. Audit `HasShaderDefine` precision by family and stage.
   - Start with Cloud Shadows, whose current scope appears substantially broader than its HLSL use warrants.
   - Record why each remaining broad define must affect each family/stage.
2. Ensure an irrelevant installed or active feature cannot change a shader family's define fingerprint.
3. Hash preprocessed source, compiler/profile/flags, structural defines, and include closure to detect semantically identical variants.
4. Hash compiled DXBC to identify duplicate outputs produced under different nominal keys.
5. Attribute invalidation to the smallest source, include, ABI, feature-residency, or compiler change.
6. Preserve caches across active-only changes.
7. Manage active and previous-known-good cache generations transactionally so interrupted recompilation cannot destroy the usable set.
8. Extend the cache comparator to report feature/family/stage/entry point, bytecode identity, compile cause, and dependency closure rather than only directory differences.

### Exit gate

- Compile count, compile time, and cache size fall on at least one real preset transition.
- Byte-identical shaders are reused.
- Active-only toggles cause zero shader invalidations.
- Render output and stability remain unchanged.

## Milestone 3 — Introduce explicit residency and activation

### Actions

- Add explicit `Resident` and `Active` state while retaining a compatibility mapping for `loaded` during migration.
- Split iteration and hooks into concepts such as `ForEachResidentFeature` and `ForEachActiveFeature`.
- Generate shader capability defines and generic cache identity from `Resident` state.
- Drive runtime shader flags, pass scheduling, and avoidable CPU hooks from `Active` state.
- Define a safe-frame activation queue; coalesce multiple changes before applying them.
- Make active changes incapable of clearing the shader cache.
- Define resource policies per feature:
  - always resident;
  - lazy resident, retained while inactive;
  - releasable only on explicit hard unload/restart.
- Present two clear UI operations where applicable:
  - immediate enable/disable;
  - hard unload at next launch.
- Update disk-cache metadata and diagnostics to show installed, resident, active, structural key, and pending state independently.
- Add unit/integration tests for every legal state transition, cache-key stability, restart persistence, and failure rollback.

### Exit gate

- State transitions are deterministic and covered by tests.
- A resident feature can be toggled without compile, cache invalidation, or unsafe mid-frame mutation.
- Existing presets and `Disable at Boot` preserve their intended behaviour.

## Milestone 4 — Prove the independent-pass route

### Candidates

- Use Skylighting as the resident/inactive control because it already documents and implements runtime-safe soft disable.
- Select the first truly independent effect only after the manifest proves its scheduling and resource dependencies. Screen Space Shadows is a likely candidate but should not be assumed independent solely from its compute shader files.

### Actions

- Keep shaders and required resources resident.
- When inactive, skip dispatch/draw submission, avoid per-frame CPU updates, and make its profiler timer absent or explicitly zero-work.
- Compare active, resident-inactive, and hard-unloaded states using the same scene and cache generation.
- Verify reactivation at a safe frame boundary without compilation or first-use hitch.

### Exit gate

- Activation takes no longer than one safe-frame transition.
- No compile or cache invalidation occurs.
- Inactive CPU/GPU overhead is below measurement noise or an agreed ceiling of roughly 0.02 ms for the independent pass.
- Reactivated output matches the current enabled path.

## Milestone 5 — Prove a bounded integrated resident superset

### Pilot 1: Hair Specular

Hair Specular is a good first integrated pilot because it is scoped to Lighting, already has runtime `Enabled` data, and is much smaller than Wetness.

### Actions

- Treat the existing compile define as `Resident` capability.
- Ensure the runtime flag alone controls `Active` contribution.
- Compare:
  - current specialised-off shader;
  - resident-inactive shader;
  - resident-active shader.
- Measure affected Lighting draw time, scene aggregate time, bytecode size, instruction count, temporary registers, resource slots, CPU update cost, compile count, and cache size.
- Test representative feature permutations rather than only the single-feature case.

### Pilot 2: Wetness

Proceed only after the Hair Specular pilot passes. Wetness spans Lighting and Water, has many compile-time sites, and is therefore the correct complex test of bounded family supersets.

### Stop/split conditions

- Split the resident set when register pressure, resource slots, occupancy, bytecode size, or inactive branches cause a material regression.
- Retain compile-time structural variants when a resource or signature cannot be safely bound in the generic family.

### Exit gate

- Inactive integrated overhead is below 1–2% for affected draws and below approximately 0.05 ms in the representative scene aggregate, unless a stricter measured threshold is adopted.
- Enabled and disabled outputs match current specialised paths.
- The number of resident variants remains bounded and explainable.

## Milestone 6 — Add asynchronous specialisation behind a valid fallback

Use this only where the generic resident superset is correct and acceptably fast, but a specialised shader still provides worthwhile steady-state performance.

### Actions

- Keep a valid generic resident shader available for every supported active combination.
- Compile specialised active configurations in the background.
- Use a content-addressed key containing:
  - base and include hashes;
  - compiler version, flags, and profile;
  - structural variant key;
  - ordered resident modules and ABI versions.
- Do not put the ordinary active mask in the generic resident key.
- Create the D3D11 shader object and perform a controlled warm-up before publishing it.
- Atomically swap only at a safe frame boundary.
- On compile, creation, warm-up, or validation failure, retain the generic shader and log the failure without blocking gameplay.
- Add bounded queueing, cancellation/coalescing, and compile-rate controls so rapid UI changes do not produce obsolete work.

### Exit gate

- Toggle response is immediate through the generic fallback.
- No first-use hitch occurs when the specialised shader becomes active.
- Specialisation equals or improves the current steady-state path and never reduces correctness.

## Milestone 7 — Isolated D3D11 function-linking spike

Function linking is an optional optimisation experiment, not a dependency of the plan.

### Scope

- One Lighting pixel-shader family.
- One small behavioural module and a fixed ABI such as `SurfaceState`/`FeatureContext`.
- AMD and NVIDIA validation, plus current VR and flat paths where applicable.

### Measure

- full compilation versus library compilation/link time;
- link time;
- shader-object creation and warm-up;
- bytecode size, instructions, registers, and resource slots;
- steady-state GPU time;
- driver compatibility, cache persistence, and failure recovery.

### Decision gate

- Adopt only if end-to-end latency and generated shader quality are materially better and driver behaviour is reliable.
- Abandon or confine it to a narrow family if linking merely relocates cost, produces inferior code, or introduces compatibility risk.

## Milestone 8 — Restricted data-driven composition

Consider a compact operation list, LUT, or coefficients only for domains that are naturally linear and bounded. Do not build a general shader interpreter.

Suitable examples include colour transforms, blend terms, and simple material coefficients. Unsuitable examples include resource-heavy effects, divergent algorithms, geometry/topology changes, and vendor implementations.

## Milestone 9 — Controlled rollout

- Roll out by shader family and then by feature, with a capability table showing structural/resident/active support.
- Keep the existing specialised implementation as the default until each pilot passes.
- Retain a per-feature rollback switch.
- Structure commits/PRs so that instrumentation, manifest, cache-key changes, state model, each pilot, asynchronous specialisation, and linker experiments are separable.
- Re-run cold/warm cache, flat/VR, main-menu/in-game, save/load, preset transition, device reset, and shutdown tests for each graduated family.

## Initial candidate ordering

| Priority | Candidate | Purpose | Initial recommendation |
|---:|---|---|---|
| 0 | Compile/cache instrumentation | Makes every later decision measurable | Start immediately |
| 0 | Machine-readable manifest | Establishes complete families, passes, I/O, and dependencies | Start immediately |
| 0 | Cloud Shadows define-scope audit | Likely quick reduction in irrelevant variants | Audit before architectural changes |
| 1 | Skylighting | Reference resident/inactive implementation | Formalise and measure as control |
| 1 | Proven independent pass | Validate zero-work inactive scheduling | Select from completed manifest |
| 2 | Hair Specular | Small integrated resident-superset pilot | First behavioural shader migration |
| 3 | Wetness | Complex cross-family integrated pilot | Attempt only after Hair Specular passes |
| 3 | Background specialisation | Remove residual generic-superset cost/hitches | Add only where measurements justify it |
| 4 | D3D11 function linking | Test low-latency modular specialisation | Isolated spike only |
| Defer | Upscaling/vendor reconstruction | Structural and compatibility-sensitive | Do not use as an early pilot |
| Defer | VR/flat pipeline unification | Structural and high-risk | Preserve explicit variants |

## First implementation sprint

1. Define the manifest schema and structural-versus-behavioural vocabulary.
2. Generate the first manifest from feature definitions, compile sites, includes, bindings, and profiler registrations.
3. Manually close the gaps for dynamic/engine-owned resources and all overlay routes.
4. Add compile lifecycle and invalidation-cause telemetry.
5. Extend cache comparison to semantic ownership and bytecode identity.
6. Capture versioned cold/warm baseline evidence for the representative scenes and presets.
7. Audit and, if safe, narrow Cloud Shadows' shader-family/stage define scope.
8. Write the explicit residency/activation state-transition specification and tests before changing production behaviour.
9. Audit Skylighting against that specification and use it as the reference result.
10. Hold a measured go/no-go review before selecting the independent-pass and Hair Specular production pilots.

## Success metrics

- Cold and warm compile count and wall time.
- Cache hit rate, file count, bytes, duplicate DXBC count, and invalidation scope.
- Active-toggle latency and number of active-only recompilations (target: zero).
- Main-thread hitch p50/p95/p99 and worst frame during startup, preset changes, and toggles.
- Preprocess, compile, object creation, warm-up, and publication time separately.
- Per-feature CPU hook/update cost.
- Per-pass and scene-aggregate GPU cost for active, resident-inactive, and hard-unloaded states.
- Bytecode size, instruction count, temporary/register pressure, and bound resource count.
- Flat/VR stereo correctness and overlay correctness.
- Driver/device compatibility, shader-creation failures, CTDs, and successful fallback rate.

## Explicit non-goals and traps

- Do not introduce one global active mask into every shader family in one change.
- Do not build a general-purpose shader VM.
- Do not use an unoptimised visible fallback as the normal toggle path.
- Do not include ordinary active flags in generic resident cache identity.
- Do not release soft-disabled resources if doing so makes reactivation compile, allocate, or hitch.
- Do not infer pass independence from filenames alone.
- Do not start with Upscaling, vendor paths, or VR/flat structural variants.
- Do not treat shorter compilation as a win if shader-object creation, driver warm-up, GPU time, cache size, or stability regresses.

## Decision point after the first sprint

Proceed to production pilots only when the manifest is complete enough to prove dependencies and the telemetry can separate preprocessing, compilation, object creation, warm-up, and steady-state cost. At that point the expected path is:

1. narrow irrelevant define scope and invalidation;
2. formalise installed/resident/active state;
3. prove an independent-pass soft-off;
4. prove Hair Specular as a bounded integrated resident superset;
5. attempt Wetness and background specialisation only if the measured trade-off remains favourable;
6. evaluate function linking independently, with a clear stop condition.
