# main-vr-nr progress and continuation record

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
    identity and complete stereo publication. SE/AE keeps supported A;
    unsupported flat B/C/character routes remain disabled.
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
