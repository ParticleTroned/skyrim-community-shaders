# Selective Open Shaders sync: adversarial review

Review baseline: `e5fa9a52468e01554163ebe645d9d98e38490806` on
`codex/pr730-open-shaders-dev-sync`. The scope includes the approved
runtime/build ports, their integration corrections, and interaction with
the merged Adaptive Balance Color base. Primary `main-VR` remains separate.
Original port commits are retained; each affected port receives a new
correction commit. Source review and combined build validation are complete.
Two actionable findings were corrected; no additional confirmed defect
remains from this review.

## Commit coverage

Each row was checked against its original diff and final call sites for
accepted scope, runtime divergence, failure handling, ownership, and
existing utilities. "Retained" means no additional confirmed defect was
found in that reviewed change; it is not an in-game qualification result.
Documentation-only decision commits were checked for agreement with the
accepted ports and exclusions.

The coverage audit in `adversarial-coverage.json` accounts for all 22
code-bearing first-parent commits after the chosen `202777f6f` base.
The base's #738 port is included below. Its preceding `4730e3029` test
compatibility change was also reviewed: `_sopen_s` reports its returned
error and retains the fixture's descriptor ownership; no change is needed.

| Commit      | Port / integration            | Review result                                                                                                                                                            |
| ----------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `202777f6f` | #738 sky composition          | Retained authored-color composition, legacy texture path and VR alpha behavior. This port was already in the chosen branch base.                                         |
| `35743a486` | #730 shared shader bytecode   | Retained descriptor/stage/type identity, per-bytecode clearing, pending/deferred ownership guards and queue deduplication.                                               |
| `a5e5ff76e` | Adaptive Balance Color base   | Retained merged feature ownership, final-scene grading and buffer integration.                                                                                           |
| `c0dce9cc4` | #741 atmosphere controls      | Retained independent cloud migration, layered clamps, neutral values, preview gating and GPU layout. Later sky-static brightness routing is reviewed under #787.         |
| `354a7026c` | Amended Color base            | Retained sign-aware grading and HDR cases; no overwrite of atmosphere controls.                                                                                          |
| `7f20a64d7` | #743 RTTI recovery            | Retained removal of terminating `noexcept` boundaries around recoverable RTTI paths.                                                                                     |
| `c0c7caf47` | Color preset compatibility    | Retained supported contract revision 5 and refreshed source fingerprints.                                                                                                |
| `6fb3bd203` | #746 stale scene lights       | Retained bounded batch reads, neutral recovery for the whole batch and bounded warning output.                                                                           |
| `27ab19bd9` | #755 frame-generation inputs  | Retained readiness after both input preparations, live provider gates, presentation consumption and resource-reset invalidation. VR remains excluded.                    |
| `36170c5e1` | #750 forced weather           | Retained shared force-weather routing, native trampoline and AE versus SE/VR model-handle release. The address-library requirement covers the added VR relocation.       |
| `36956805c` | #756 camera transforms        | Retained row-vector origin compensation, per-eye frozen history and local cropped-viewport correction.                                                                   |
| `6ca25554e` | #751 stale-job early exit     | Retained dispatch-slot RAII and later generation/publication checks; no upstream post-processing subsystem imported.                                                     |
| `611eda483` | #758 hair / True PBR          | Retained common `CS_HAIR_SHADING` gate through material, context and lighting consumers; generic hair semantics remain intact.                                           |
| `e1a06dfcc` | #766 weather scrubbing        | Retained deferred cloud-pass invalidation and finite time edits without deleting borrowed render passes.                                                                 |
| `d78bb69d4` | #772 FSR diagnostics          | Retained exact exception-code capture in existing protected dispatches and existing quarantine decisions.                                                                |
| `5197a2329` | #777 resource names           | Retained centralized D3D11 naming, transactional resource ownership and D3D12 alias names; imported source names are preserved.                                          |
| `d0e4428c9` | #770 DLSS budget warning      | Retained warning-success classification in output and telemetry, per-eye throttling and failure handling for other results.                                              |
| `315f1d7a1` | #776 runtime download policy  | Corrected configure-to-install payload drift in a separate commit; regression reproduced and passed after the correction.                                                |
| `658087a45` | #769 typed eye depth          | Retained existing guide encoder, source/view identity checks, typed per-eye output, post-encode resource identity and FSR UAV unbinding before copies.                   |
| `e41544b93` | #787 weather brightness       | Retained one-time scaling/restoration of live weather colors, neutral disable behavior and reported hook availability. No Scene Manager added.                           |
| `fe510ae91` | #780 RCAS naming subset       | Retained existing named constant-buffer wrapper; no unsupported scene-exposure normalization imported.                                                                   |
| `c047c6dde` | #778 optional FOV curve       | Reused the shared finite clamp in a separate commit. Retained opt-in/default-neutral math, mask geometry, shader layouts, history invalidation and saved value behavior. |
| `e5fa9a524` | End-of-sync integration fixes | Retained 80-byte assertion, actual diagnostics API, fixture-local `NOMINMAX`, narrowly checked known shader warnings and handle-based long-path resolution.              |

The ports do not add excluded E11/EHF/SLF subsystems, upstream UI,
translations, the shared wind system, Grass Optimizations or Scene Manager.
The primary checkout and unrelated user changes remain outside the review
worktree. No shared-branch history is rewritten.

## Runtime payload installation: P2 corrected

Affected port: `315f1d7a1` (#776). The install guard captured missing-file
state only at configuration. Removing or changing a previously verified
runtime DLL afterward allowed installation to enter its destructive AIO
staging reset before the missing file failed, or package changed bytes.

The guard now records configured payload SHA-256 values and checks the
selected components' current files before any install action. One shared
check handles FidelityFX and Streamline; SKSE-only installation remains
independent. Runtime downloads, cache ownership and deployment are unchanged.

The extended script fixture reproduced the old guard accepting a changed
FidelityFX DLL. It now passes changed/missing payload cases for both
providers, verifies staging preservation, permits SKSE-only installation,
and accepts restored valid files. The existing download, cleanup and
deployment-ownership cases also pass. Evidence under
`build/analysis/open-shaders-dev-review-20260926`:

-   `review-runtime-before-repro-final.log`: expected rejection missing.
-   `review-runtime-after.log`: complete policy fixture passed.
-   `run-runtime-review.ps1`: exact argument-array runner.

Three earlier command invocations failed at argument parsing, before the
regression ran. `review-dev-doctor.log` reports zero failures and one
existing public-HTTPS remote warning. No game deployment occurred.

## FOV falloff validation: P3 reuse corrected

Affected port: `c047c6dde` (#778). Its local finite-value clamp duplicated
`Util::ClampFinite`. The policy now calls that shared utility with the
same 0.5–2 bounds and neutral fallback of 1. Existing FOV fixture coverage
includes both bounds, nonfinite values, disabled-value preservation,
SE/AE neutrality, and strict DevBench input validation. The shader curve,
mask geometry, history policy and settings format are unchanged.

## Combined validation: passed

Commands from the sync worktree:

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -D DEVBENCH_BRIDGE=ON
pwsh ./tools/validate-local.ps1 -OutputDirectory ../../analysis/open-shaders-dev-review-20260926/adversarial-validation-ec58827f1
```

-   Compiled source: `ec58827f1d5577b78b01e8883bb278ceb72686c9`, clean
    before and after validation; submodules matched their pins and were clean.
    Later documentation folding preserves this producer identity and does
    not claim a rebuilt DLL.
-   Universal SE/AE/VR Release DLL with DevBench enabled and Tracy disabled:
    passed. This compiles the actual registered menu/weather controls and
    optional diagnostic paths in addition to their fixture coverage.
-   Build ID:
    `ad7c6d6f045c401d0208cb057791b04deb03e6f6590b4aa7ac95701e4edd6095`.
-   DLL SHA-256:
    `52a9d123612adc39b7e8a8c32056c0345c29b1e69cd901ed53857cb6b052468c`;
    size 29,112,320 bytes. Physical build artifact and manifest matched.
-   All 160 registered tests executed and passed, with no failures, skips,
    disabled tests, missing tests or unbuilt tests. This includes the new
    runtime-payload regression and existing FOV/controller/WARP fixtures.
-   Standalone preset regression, generated preset check, diff check,
    manifest verification and unchanged final source snapshot: passed.
-   Total runner time: 455.93 seconds. Configure 23.72 s, DLL build 232.86 s,
    test build 100.42 s, CTest 66.28 s and preset regression 23.02 s.
-   The actual configured runtime guard also passed independently without
    installation (`review-real-runtime-guard.log`). Scoped hooks and CMake
    formatting passed; gersemi emitted custom-command recognition warnings.

The directory named in the validation command contains the complete
inventory, stage commands/timings, logs, JUnit results, source snapshots,
manifest and `summary.json`. Existing FidelityFX CMP0116 and narrowly
accepted periphery-cache shader diagnostics remain documented in the
prior validation record. No production HLSL changed during this review,
so the prior 64-variant hair compilation result was not rerun.

## Validation boundary

The prior end-of-sync [validation](open-shaders-dev-validation.md) remains
evidence for its exact producer. New in-game SE/AE/VR execution, deployment,
physical-HMD qualification and performance measurement are outside this
review run. No visual, stability or performance pass is inferred from
source review or compiled tests.
