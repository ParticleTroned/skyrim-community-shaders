# main-vr-nr progress and continuation record

## September 19 main-VR integration

[Integration record](main-vr-nr-sync-20260919.md): merge local `main-VR`
through `63e40d610`, including CommonLib 8.3.0, while retaining NR routes,
colour/capture contracts and the Render Scale guard. The September 20
adversarial follow-up adds the subsequent `09eb8523f` preset-default commit
and verifies that existing settings are unchanged. Fourteen source contracts
and preset checks pass. The existing AIO predates these merges; compilation
is still pending.

## Renderscale NR dependency (2026-09-19)

[Dependency guard and validation](nr-renderscale-dependency-20260919.md):
enabled VR Renderscale NR protects Render Scale in UI/profile controls and
waits for physically active scaled targets at runtime. Full/Foveated NR
and flat behavior stay independent. All 12 focused C++ host tests pass;
in-game validation is pending.

## Persistent branch directive

User direction dated **2026-09-18 (Europe/London)** applies to this and
subsequent tasks: **`main-vr-nr` is the integration target**. Work on its
current integrated implementation. Earlier handover references to moving
the work into `main-VR`/`main-vr` are superseded; no migration is required.
The old Face-of-gogh branch is historical source, not the working target.

At each continuation, read `AGENTS.md`, applicable scoped instructions and
`.claude/CLAUDE.md`; resolve the actual local/remote heads and worktree
status again. Preserve newer commits, uncommitted work, existing worktrees,
build outputs and caches. Never reset to the reviewed reference or forcibly
reconcile differences. If isolation is needed, base it on the verified
current `main-vr-nr` HEAD and retain this integration target. Do not switch
to or merge `main-VR`/`main-vr` to satisfy an earlier handover.

Subsequent implementation tasks need separate reviewable changes,
appropriate tests, truthful evidence and Conventional Commit bodies with
`Rationale:` and `Implementation:`. Revalidate findings against current
source before editing. Do not automatically push, deploy or alter a live
game. Do not edit agent policy or unrelated rendering. This document
records the user's task directive without changing repository policy.

## NR master control correction, 2026-09-19

The [control correction](nr-master-toggle-fix-20260919.md) makes Enabled
independent of missing FOV prerequisites and keeps selected restrictions
removable. Runtime readiness still gates execution. DevBench now accepts
valid pending FOV-dependent settings and reports their readiness separately.
Source checks passed as recorded; compiled/live validation remains pending
under the user's no-build instruction. The flat character NR extension was
paused at the user's request to prioritize this correction.

The [FOV menu follow-up](nr-fov-menu-selection-20260919.md) keeps configured
VR Foveated choices editable while startup or transition handling pauses
runtime upscaling. Execution still uses the effective runtime method, and
the UI now distinguishes configured-but-waiting FOV from inactive FOV.

## Flat character NR extension, 2026-09-19

Resumed after master-toggle correction `153af7cbc`. The
[flat support record](nr-flat-character-support-20260919.md) describes shared
category authoring, mono character selection and ROI/Multi-ROI, compatibility
limits and validation. The previous production AIO still contains only the
master-toggle correction; no replacement archive was requested for this work.

## Flat reduced-resolution NR extension, 2026-09-19

SE/AE now supports A/full resolution and C/reduced resolution, both sharing
face/skin/hair selection, strengths and ROI/Multi-ROI. The
[flat reduced-resolution record](nr-flat-reduced-resolution-20260919.md)
describes the mono adapter: C processes the active render-resolution image,
then supplies its complete private candidate to ordinary DLSS. Preparation
or inference failure retains the original DLSS input. The shared exact mask,
private outputs, colour/Lighting preservation and stateless NR policy are
unchanged; DLSS owns temporal reconstruction. B/foveated and FOV restriction
remain VR-only. Source implementation does not establish flat visual quality
or performance; those require runtime evidence.

## Optional renderscale NR FOV, 2026-09-19

**Renderscale NR before DLSS** defaults to full-eye NR then DLSS again.
The independent **Use FOV mask for Renderscale NR** switch opts into the
cropped behavior introduced by `20717c138`, enabling an in-game comparison.
Full resolution keeps its separate restriction; Foveated keeps automatic
masking. SE/AE remains mono without FOV. The
[implementation record](nr-renderscale-fov-20260919.md) covers settings,
crop/blend policy, readiness and focused CPU validation. This change has
not been compiled into a production archive or measured in game. Review
also separated shared-mask availability from the full-eye adapter, removed
redundant full-coverage baseline copies and tightened capture identity
checks; the implementation record retains validation and scope details.

## Foveated final-scene placement and hair defaults, 2026-09-19

The [placement/default correction](nr-final-scene-placement-20260919.md)
makes Foveated use Final LDR before UI, including legacy early insertion
settings, following the tester's successful fire/light workaround. Full
resolution and Renderscale NR retain their respective late and pre-DLSS
placements. Hair now defaults enabled at the existing 0.65 strength;
explicit saved hair selections remain intact. Source checks passed;
compiled and live verification remain pending under the no-build instruction.
The same update defaults Renderscale NR FOV on when loading/resetting NR
with VR FOV enabled, preserving explicit saved choices. Valid old placement
values migrate without rejecting the rest of the saved profile.

## Task 0 verified checkpoint

-   Existing worktree: `C:/src/skyrim-community-shaders/build/worktrees/main-vr-nr`.
-   Reviewed reference and actual implementation HEAD:
    `8551db2a31f4fcfd32015a6377da5e5735c1aabf`.
-   `origin`: `git@github.com:ParticleTroned/skyrim-community-shaders.git`.
-   A targeted fetch and `ls-remote` verified `origin/main-vr-nr` at the same
    commit on `2026-09-17T23:04:27Z` (2026-09-18 in Europe/London).
-   Before this documentation change: clean tracked/untracked status,
    **0 ahead / 0 behind**, no commits or file differences since the review
    reference. No newer local or remote commits needed reconciliation.
-   The commit containing this record adds documentation only on that HEAD;
    it is intentionally not pushed. Existing worktrees remain in place.

## Implementation boundaries to preserve

-   A/full resolution, B/foveated and C/render-resolution NR before DLSS;
    character selection remains orthogonal. Keep FOV prerequisites, shared
    mask geometry/feathering and normal DLSS fallback.
-   Preserve exact character texel loads, produced-ROI bounds, source-frame
    identity and complete mono/stereo publication. SE/AE supports A and C,
    including shared character selection and ROI/Multi-ROI; B/foveated and
    FOV restriction remain VR-only.
-   Native NR remains D3D12 with the existing interop. Do not modify NVIDIA
    binaries or admission and do not reopen native-D3D11 NR experiments.
-   Keep the independent `NeuralRendering` feature and saved
    `Neural Rendering` envelope (`schemaVersion`, `rendering`, `colour`),
    existing DevBench services and `CS_GPU_PASS` instrumentation.
-   Preserve lighting preservation's 0–1 range, default 1, saved preferences
    and per-transaction snapshots. Its output-only changes do not reset
    inference input history. C retains its separate stateless-NR policy.
    NR defaults off; colour mode defaults to Original (`LegacyRaw`), so the
    100% slider default does not itself enable Preserve Source processing.

## Progress

| State        | Current position                                                                                                                                                                                                                                                                                                 |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Completed    | Integrated A/B/C and character controls, production/debug UI separation, FOV admission, exact character composition, independent persistence and DevBench exposure are present. Lighting preservation shares the colour pipeline across routes. Preset contract 6 and signed-zero evidence fixes are integrated. |
| Experimental | Multi-ROI/savings-gate controls and colour exposure/domain/transport/A–B diagnostics remain experiments. OpenNR's smaller spatial proxy and residual reconstruction are reference ideas, not an extra implemented C downsample. Dedicated Vincent/C colour calibration remains deferred on this target.          |
| Blocked      | No repository blocker was established in this checkpoint. Live provider readiness and compatibility were not checked; absence of runtime evidence is not a diagnosed admission failure.                                                                                                                          |
| Unmeasured   | Live Feature 18 inference on this integration, in-game UI layout, headset image quality, moving-scene stability and comparative CPU/GPU performance remain unqualified. CPU/WARP results do not establish those outcomes.                                                                                        |

## Source and evidence revalidation

The checkpoint reread the [implementation](main-vr-nr-implementation.md),
[adversarial](main-vr-nr-adversarial-review.md),
[OpenNR](main-vr-nr-opennr-review.md) and
[lighting-preservation](main-vr-nr-lighting-preservation-sync.md) reports.
Historical source builds and OpenNR's pinned revision retain their original
scope. Their evidence is not a new live measurement of this checkpoint.

Current source anchors checked include
[`PipelinePolicy.h`](../../src/Features/Upscaling/NeuralRendering/PipelinePolicy.h)
for routes/runtime boundaries,
[`CharacterRegionPolicy.h`](../../src/Features/Upscaling/NeuralRendering/CharacterRegionPolicy.h)
and [`FoveatedCenterBlendCS.hlsl`](../../features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl)
for ROI and exact output selection,
[`DLSS5CharacterMaskCS.hlsl`](../../package/Shaders/DLSS5CharacterMaskCS.hlsl)
for exact authored-category texels,
[`Renderer.cpp`](../../src/Features/Upscaling/NeuralRendering/Renderer.cpp)
for D3D12 work and transaction capture, and
[`NeuralRenderingFeature.cpp`](../../src/Features/NeuralRenderingFeature.cpp),
[`ColorPolicy.h`](../../src/Features/Upscaling/NeuralRendering/ColorPolicy.h)
and [`ColorPipeline.cpp`](../../src/Features/Upscaling/NeuralRendering/ColorPipeline.cpp)
for persistence, defaults and frozen reconstruction settings. No rendering
or settings implementation changed in this checkpoint.

The retained `build/validation/nr-lighting-adversarial-final-20260917/summary.json`
was reread: **passed**, 154/154 tests, none missing/disabled/skipped, plus
preset and DLL/manifest checks. Its compiled source is `feb34598c`; only
the lighting report differs from that source at reviewed HEAD `8551db2a3`.
The lighting report separately records 20/20 colour tests. These are
existing results, not reruns for this documentation change. This change
uses scoped documentation hooks and link/diff checks; it does not require
a new renderer build or live-game run.

## Task 1: transaction telemetry

Work continued on the existing `main-vr-nr` worktree from Task 0 commit
`c23499f9efbc9f193fd9e8b7ccfa7a9a95bcbf33`. A fresh targeted fetch found
`origin/main-vr-nr` still at `8551db2a31f4fcfd32015a6377da5e5735c1aabf`.
The worktree was clean; the only intervening local commit was Task 0's
documentation checkpoint. No history was reset or reconciled. Local
commit `3d0979188` adds retained profiler evidence; NR integration and its
tests are separate commit `27f46d09d44208cca118c15c471223c8f34f0c15`.
At final verification, this implementation HEAD was clean, three ahead and
zero behind; `ls-remote` confirmed origin still at the reviewed reference.
The following documentation update records validation only. The branch
directive above remains in force.

| State        | Task 1 position                                                                                                                                                                                                                                                                                                                            |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Completed    | Inspected the newest local NR run and matching producer manifest before adding telemetry. Added immutable source/publication/region identities, delayed per-call GPU samples, CPU stages and waits, physical work/capacity and byte accounting, explicit outcomes and source-boundary evidence through existing DevBench/capture services. |
| Experimental | Detailed telemetry is opt-in. D3D11 samples require the existing active profiler capture; mask support requires its existing coverage readback. Pending or disabled observations remain unavailable. No new capture/control system is introduced.                                                                                          |
| Blocked      | No source/build blocker remains established. Live acceptance needs a separately authorized producer deployment and run; none was attempted and provider readiness was not inferred.                                                                                                                                                        |
| Unmeasured   | No complete live A/B/C × character/FOV transaction matrix, native Feature 18 stage performance, telemetry overhead, headset quality or real-scene stability has been measured for Task 1. Serializer/CPU/WARP fixtures do not substitute for these observations.                                                                           |

The [transaction report](main-vr-nr-transaction-telemetry.md) records the
historical run identity, missing evidence, implementation contract,
adversarial corrections and exact validation scope. No pushes, deployment
or live-game mutation are part of this task.

Clean-source `validate-local.ps1` passed **157/157**, with zero missing,
disabled or skipped tests, plus preset and DLL/manifest checks. Build ID:
`172e18c4dc07de4b326e935321e8d6630ebdaf4c94cbc837d2f897ca382f4576`.
The supplementary colour suite passed 21/21 CTest entries, including 26/26
transaction joins; its nested Python image tests retain 26 dependency
skips. Live matrix acceptance and telemetry overhead remain unmeasured.

### Existing HMD evidence reconciled

The user's follow-up identified latest perf-branch commit
`60179f5b5289eaf8d42ffe030425f063edf3c6eb`. It already belongs to this
branch's ancestry; its [HMD material/CPU/DLSS comparison](material-comparison-20260917/README.md)
and ledgers 0006/0007 are unchanged here. These retain measured performance
and complete portable receipts, even though external raw trace paths are
unavailable on this machine. The earlier raw-NR inventory was too narrow
to serve as an index of all prior HMD evidence.

The portable audit passed 1,314 receipt reconstructions, 23 candidate
windows and 69 late profile checks; it preserves one scheduler coverage
flag and the missing interrupted Save 13. These historical whole-frame and
WPR measurements inform future comparisons. Task 1's new NR per-region GPU
timings and overhead remain unmeasured. The [transaction report](main-vr-nr-transaction-telemetry.md#retained-hmd-cpudlss-analysis)
records exact producer identity, findings and scope. No new measurements,
settings changes, branch migration, build, deployment or push occurred in
this evidence review.

## Task 2: native replay tooling

The bounded developer capture, standalone admitted-runtime replay and
cost-report tooling are implemented. See the [native replay checkpoint](main-vr-nr-native-replay.md)
for contracts, actual SDK/runtime inspection, validation and explicit
measurement gaps. The test AIO enables the DevBench bridge; deployment
and push remain manual. Numeric A/B/C cost tables require new native input
bundles. Earlier screenshots cannot provide depth/motion inputs. Task 3
ROI descriptor changes remain separate and have not been started here.

## Local main-VR integration: 2026-09-19

Integrated the 29 local main-VR commits through
`5dcbf163ee08c0bbd652bd47eedd3dbbbec2d25e` into NR checkpoint
`9fde1e74bfce2953eda91dcc40012a801863f869` by merge, preserving both
histories. The companion main-VR commit
`d63de104b4fe43ca4463eef727a641a2e48c1813` carries the NR Subsurface
Scattering installation guard back to the shared renderer baseline. Its
ancestry is incorporated separately after this integration commit.

The user selected main-VR profiler capture behaviour with NR query checks,
and the NR Subsurface Scattering implementation. The integration retains:

-   Main-VR CPU/GPU/Both capture modes, immediate CPU publication and CPU
    fallback when GPU queries are unavailable. Its profiler API and service
    match main-VR, with NR per-invocation and detail evidence retained.
-   Checked NR query acquisition and explicit unavailable/failure outcomes.
    Retained evidence respects capture modes, preserves source-frame identity
    and avoids CPU clock sampling during GPU-only capture.
-   The shared NR D3D draw hooks, including underwater-depth-of-field and
    exposure observation, together with main-VR hook failure handling.
    Character-category authoring and terrain mesh permutation updates both
    run; their descriptor fields remain separate.
-   The Subsurface Scattering `std::call_once` guard on both branches.
    The NR ROI/route and colour implementation directories are unchanged
    from the pre-merge checkpoint, including Lighting preservation.
-   NR preset policy revision 6 and settings. Generated source fingerprints
    were refreshed for the merged implementation; settings did not change.
    Later NR historical reports and numbered measurement ledgers are retained.

A diagnostics-disabled build exposed an integration defect: required NR
submit/publication methods and route labels were inside the DevBench
conditional block. Their declarations and definitions now remain available
in production. The viewport-crop formatter also remains available to
production error logging. DevBench controls retain their existing guard.

### Validation and limitations

`pwsh ./tools/validate-local.ps1 -OutputDirectory
build/validation/main-vr-merge-final-20260919` passed on the merged working
tree: **165/165 tests**, none missing, disabled, skipped or failed, plus
preset-generator tests, generated-preset checks, diff checks and DLL
manifest verification. The universal SE/AE/VR build used
`DEVBENCH_BRIDGE=ON`, `TRACY_SUPPORT=OFF`; deployment and archive targets
were disabled. Full workflow time was 308.224 seconds; CTest took 77.220
seconds. Build ID:
`625b46fc478bba9ea3119f152b495668f402ac4535bed8b6cf3197ef85fe9427`.
The producer records pre-commit HEAD `9fde1e74b` with dirty digest
`3c44097da61d081803b50fbad2c2d1fc17adc93e799efbd5c18d0b112713f13e`.
This documentation was added after that source-stability check.

The same universal DLL also built with `DEVBENCH_BRIDGE=OFF`; its manifest
verified. Evidence and preserved DLL/manifest are under
`build/validation/main-vr-merge-production-20260919/`, including
`build-corrected.log` and `bridge-off-artifacts/`. Build ID:
`294aa98a893576e41819ae149672b6f9fac05828bb58aff1c85edd7fe130cfea`.
The final full suite used the bridge-enabled configuration. The main-VR
SSS commit passed its scoped hooks and preset-generator tests; its identical
hook header compiled in these merged universal builds.

Earlier validation directories retain the interrupted build, compiler-PDB
failure, stale preset-fingerprint failure and initial diagnostics-disabled
compile failure. The interrupted PDB was preserved before regeneration;
all were resolved for the final successful checks. Existing dependency and
assertion-build warnings remain warnings, not a warning-free result.

No live-game, HMD quality or performance measurement was made for this
integration. No production NR defaults changed. Task 3 descriptor work and
Task 2 native replay measurement gaps remain separate. These are local
commits; no push, game deployment or replacement AIO is part of this update.

## NR master toggle and FOV compatibility (2026-09-19)

The reported stuck master checkbox has a reproducible rollback path:
backend retirement failure previously rejected the disabled configuration,
so menu/configuration callers restored the prior enabled setting. Off now
remains accepted after a failed reset. Unsafe resources remain owned by
the renderer; re-enabling still requires successful retirement. History,
frame-scoped resource checks and pending NR presentation are invalidated.
DevBench reports configuration acceptance and retirement independently;
`transitionSucceeded=true` can accompany `resetSucceeded=false` for Off.

Enabled NR selects the saved centre-only FOV profile. The common settings
sanitizer handles saved loads and restores, both menu layouts disable the
FOV + TAA checkbox, and the runtime dispatch guard rejects that combination.
Neither saved mask profile is overwritten. FOV-dependent NR readiness uses
the centre-only profile even before enabling NR. A red warning asks the
player to set both eye masks precisely to cover the visible headset view.

`nr_configure` reports `fovTaaDisabled`; explicit foveation requests to
enable TAA while NR is enabled are rejected. The fixed `prepare_coc` and
`prepare_tuning` TAA fixtures reject enabled NR before any mutation rather
than changing the assay's fixture. Their ordinary NR-off behavior remains.
No shader, colour reconstruction, ROI ownership or A/B/C route policy was
changed. Preset keys, defaults, schema revision 6 and tier values remain;
only their reviewed source fingerprint and generated hashes changed.

Validation compiled the universal SE/AE/VR Release DLL with DevBench on
and Tracy off using `tools/validate-local.ps1`. The initial workflows
exposed stale transition-source assertions and a packaging test that did
not recognize the optional local NR DLL. The corrected packaging check
requires the configured provider's exact source hash and retains the
complete SDK manifest check. A focused transition-contract rerun passed.
The final complete CTest rerun passed **166/166**, with zero failures or
skips (67.08 seconds); preset-generator tests, generated-preset checks,
diff checks and DLL manifest verification passed afterward. Evidence:
`build/validation/nr-menu-fov-checks-20260919/summary.json` and siblings.
Earlier failed workflow records remain under `nr-menu-fov-20260919` and
`nr-menu-fov-final-20260919`; the final rerun changed only test assertions,
not the compiled runtime code. Bridge-enabled producer Build ID:
d41415c32c41dc49cb2c1bf893b85b5f0cc9e1c0ddca25558c3a890779f55e0d.
Its source is `23c1e0f86` plus dirty digest
`dcee68c2710a561c14eace74385308cb69219394e63d5c34566594b27678ecf6`.
This handover was appended after validation. The user then instructed that
builds must only run when explicitly requested. No production rebuild or
replacement AIO was started. The existing production archive is unchanged;
these fixes need an explicitly requested production build before testing
that configuration in game.
No live-game control, HMD quality, performance or render-scale release
qualification was performed for these fixes. No push or deployment occurs.

The user additionally reports little visible NR effect with Preserve source.
This is unmeasured, not evidence of cancelled inference. Colour processing
Off selects Original/raw NR without disabling NR. Managed uses the selected
colour/exposure reconstruction profile; the default identity profile can
look similar to raw NR. Preserve source retains source colour and, at 100%
Lighting preservation, suppresses broad neural brightness changes. Its
appearance-mix endpoint of 1 returns the same candidate as Managed. A
same-scene raw/Managed/Preserve comparison with a known nonzero neural edit
is the next quality check before changing preservation strength or defaults.

## Managed colour mode visibility (2026-09-19)

The normal colour dropdown offers Original and Preserve source. Developer
Mode (Debug/Trace) additionally offers Managed (experimental). A saved or
API-selected Managed mode remains visibly labelled when Developer Mode is
off; passive redraw never replaces it, and either normal choice can exit
it. Help text identifies the session-only calibration and makes clear that
preservation sliders apply only to Preserve source. Saved enum values,
DevBench access, processing algorithms and defaults are unchanged.

The existing extracted UI regression covers normal/developer choices,
missing state, saved-mode retention, leaving Managed and balanced combo
scopes. It was updated but not compiled or run because the user explicitly
requires a request before any build. Source-only validation passed:
`python tests/neural_color/source_contract_test.py` (8 tests),
`pwsh ./tools/generate-unified-presets.ps1 -Check`, scoped pre-commit and
`git diff --check`. Preset revision 6 and tier settings are unchanged; the
reviewed source fingerprint and generated hashes were refreshed. No DLL,
shader, test executable or AIO build, push, deployment or live test occurred.

## Contextual FOV warning (2026-09-19)

The NR menu shows the red centre-only mask warning only for an enabled,
configured FOV-dependent selection after an accepted transition replaced
FOV + TAA during the current NR-enabled session. Passive redraw retains
that notice; disabling NR clears it, and rejected transitions cannot create
it. The Upscaling menu retains its warning whenever NR and FOV are enabled,
regardless of NR route or the previous TAA selection. Neither menu warns
when NR is off. The runtime TAA constraint and saved mask profiles are
unchanged; this notice is session-only and is not a persisted setting.

The extracted control regression now covers both menu contexts, prior TAA,
all three NR modes, FOV restriction and availability, non-VR, redraw,
disabling and rejected enable transitions. Source extraction, the NR
DevBench contract, preset generation/check and diff checks passed. The
settings fingerprint was refreshed without changing preset revision 6 or
its tier values. Compiled regression execution and in-game UI testing were
not run: the user requires explicit authorization before another build.
No DLL, shaders, test executable or AIO was built, pushed or deployed.

## Individual NR tooltips (2026-09-19)

Every NR toggle, slider, selector and action now has individual, wrapped
hover help, available even when disabled. The inventory covers 37 rendering
and character control/choice call sites and 19 colour call sites. Rendering
and colour dropdown choices also describe themselves. Existing long
experimental tooltips were shortened; controls do not share grouped help.
Full resolution with FOV restriction is identified as the final-scene route
limited to eye masks, while Foveated uses the DLSS-upscaled FOV region.
Character selection can further narrow either route. Runtime settings,
rendering, colour processing and the contextual warning rules are unchanged.

Validation: the source inventory found no control without individual help;
UI extraction, the NR DevBench source contract and all eight colour source
contract tests passed. Preset generation/check retains revision 6 and tier
values, with updated source fingerprints. No DLL, shader, test executable,
AIO build, in-game validation, deployment or push was performed.
