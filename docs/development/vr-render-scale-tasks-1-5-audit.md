# Render-scale Tasks 1–5 source-to-test audit

Audit baseline: `main-VR`,
`b46c79e1a29f37c357350b4dee0d084689987150`, initially clean, with the
pinned recursive submodules unchanged. This audit follows the Step 2
contract reconciliation and Step 3 build validation. It identifies one
demonstrated production defect: lifecycle reset discarded Streamline's
frame-ordering history. The correction retains that history while
invalidating the token.

`IMPLEMENTED` means the production path exists and was traced below.
`TESTED` means the listed production policy or extracted production code
is exercised by the focused tests. It does not mean that the complete
game, graphics driver or provider SDK executes inside those tests.
`RUNTIME_EVIDENCE_PENDING` applies to every task for the corrected build.
No new runtime measurement or numbered ledger accompanies this audit.

## Task matrix

Source locations in this document are repository-relative. Telemetry names
are relative to their controller, qualification, stress or trace object;
they are not invented additional schema fields.

| Task                                                 | Production owner                                                                                                  | Production service point                                                                                                                                                                            | Policy/header                                                                                                                                                                                    | Focused test                                                                                                                                                                                                                                                                   | Runtime telemetry field                                                                                                                                                                                                                      | Covered cases                                                                                                                                                                                                                | Uncovered cases                                                                                                                                                   | Code changed                                                                                                      | Final status                                   |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| 1. Request / operation / epoch / generation identity | Public API operation table; locked pending/deferred requests; physical queue and transition controller            | `UpscalingService::ExecuteOperation` / `UpdateOperationsLocked`; `QueueVRRenderScaleRequest`; `RecordVRRenderScaleTransitionRequested`; `BindVRRenderScaleRelatchEpoch`; applied/stable publication | `VRRenderScaleAuthorityPolicy.h`, `VRRenderScaleQualificationPolicy.h`, `VRRelatchReleasePolicy.h`, `VRSubmitTemporalSnapshot.h`, `VRSubmitStereoBatch.h`; existing API and replacement policies | `vr_render_scale_authority_policy_test.cpp`, `vr_render_scale_qualification_policy_test.cpp`, `vr_relatch_release_policy_test.cpp`, `vr_relatch_promotion_test.cpp`, `vr_submit_temporal_snapshot_test.cpp`, `vr_submit_stereo_batch_test.cpp`; API/replacement contract tests | `operationId`; profile `requestID`, `transitionEpoch`, `contractGeneration`; `desiredOwner`, `physicalOwner`, `presentationOwner`; per-eye generation, frame, cycle and dispatch evidence                                                    | Exact request/epoch/generation receipt binding; stale owner and generation rejection; deferred transfer; duplicate/missing/mismatched eyes; immutable batch resource and dispatch identity                                   | Complete API-to-game interleavings and actual HMD submission on this build; API operation bookkeeping is source-audited, not a whole-service executable fixture   | None                                                                                                              | IMPLEMENTED + TESTED; RUNTIME_EVIDENCE_PENDING |
| 2. Regression guards and milestones                  | Qualification transition/session baseline; controller presentation/fidelity state; stress episode state           | `BuildQualificationObservation`, `RecordQualificationMilestones`; `RecordVRRenderScalePresentation`; stress stop and gate generation                                                                | `VRRenderScaleQualificationPolicy.h`, `VRPresentationStretchTelemetryPolicy.h`, `VRRelatchReleasePolicy.h`                                                                                       | `vr_render_scale_qualification_policy_test.cpp`, `vr_presentation_stretch_telemetry_policy_test.cpp`, `vr_relatch_release_policy_test.cpp`; `vr_render_scale_devbench_contract_test.cmake`                                                                                     | `presentationStable`, `cleanupDrained`, `strictSatisfied`, `monotonicCounterRegressions`, `terminalDiagnosticReasons`; stretch raw gates and stop-tail fields                                                                                | Separate first milestones; current stability versus historical health; stale observation/counter rejection; terminal/provider failure; cleanup debt; complete/incomplete stereo and active stop tail; raw diagnostic stretch | Exact-build milestone timing and full-history runtime receipts; producer JSON wiring is source-checked rather than the full game bridge running in a unit fixture | None                                                                                                              | IMPLEMENTED + TESTED; RUNTIME_EVIDENCE_PENDING |
| 3. Owner acquisition, service, transfer and release  | The 25 owner classes catalogued below; provider drain tickets and owned release receipt                           | `ConfigureUpscaling`; completed native pair boundary; reset/resource service; recovery watchdogs; retirement/trim service; next-cycle release revalidation                                          | `VRRenderScaleAuthorityPolicy.h`, `VRRelatchReleasePolicy.h`; existing drain, frame-boundary and vendor-relatch policies                                                                         | `vr_render_scale_authority_policy_test.cpp`, `vr_relatch_release_policy_test.cpp`, `vr_relatch_drain_controller_test.cpp`, `vr_relatch_native_boundary_test.cpp`, `vr_relatch_promotion_test.cpp`                                                                              | `authorityLiveness.owners`, `serviceClasses`, `unmappedOwnerMask`, `inconsistencyMask`, `controllerRevisionStable`; lifecycle, retirement, trim and physical-mutation snapshots                                                              | All owner-to-service mappings; diagnostic hints cannot add/remove owners; orphaned compound owners remain visible; stale drain tickets; mixed retry/debt rejection; actual release receipt assembly and final revocation     | Real GPU fences, device loss, provider teardown and concurrent ownership transfers on this build                                                                  | None                                                                                                              | IMPLEMENTED + TESTED; RUNTIME_EVIDENCE_PENDING |
| 4. Streamline token/constants coherence              | `Streamline::frameTokenCoordinator`; immutable submit temporal snapshot and stereo batch                          | `AcquireFrameToken` from DLSS and Reflex; `CheckFrameConstants`; `slEvaluateFeature`; `CaptureSubmitTemporalSnapshot`; submit dispatch scope                                                        | `StreamlineFrameTokenPublication.h`, `VRSubmitTemporalSnapshot.h`, `VRSubmitStereoBatch.h`                                                                                                       | `streamline_frame_token_publication_test.cpp`, `vr_submit_temporal_snapshot_test.cpp`, `vr_submit_stereo_batch_test.cpp`                                                                                                                                                       | Trace `frame`, `frameToken`, `frameTokenAddress`, `compositorCycleToken`, `eyeIndex`, `viewportRole`, `constants`, `stage`, `resultCode`; per-eye dispatch frame/serial                                                                      | Concurrent publication; failed acquisition; dispatch-failure reset; lifecycle reset; frame wrap; immutable camera/jitter/depth/motion inputs; changed contract rejection; desktop Present between eyes                       | Actual SDK token lifetime, Reflex/render/reset interleavings, constants/evaluation trace and visual output for this build                                         | Preserve published frame across lifecycle reset; add failing-first stale-frame regression and reset/wrap coverage | IMPLEMENTED + TESTED; RUNTIME_EVIDENCE_PENDING |
| 5. Native-AA identity                                | Requested/configured profiles; effective/stable physical contract; actual provider dispatch and presentation eyes | Mode resolution in UI/API/queue; `NativePhysicalContractStable`; `PresentationEyesStable`; `NativeVendorPresentationStable`; successful DLSS/FSR evidence capture                                   | `VRRenderScaleModePolicy.h`, `VRRenderScaleQualificationPolicy.h`, `VRSubmitStereoBatch.h`                                                                                                       | `vr_render_scale_mode_policy_test.cpp`, `vr_render_scale_qualification_policy_test.cpp`, `vr_submit_stereo_batch_test.cpp`; `render_scale_link_contract_test.cmake`                                                                                                            | Requested/effective/stable method, quality, render-scale and FSR4 preference; physical `backend`; `nativeVendorExecution.required`, `sameFrameBothEyesValid`, `actualBackend`, `actualRuntimeFallbackObserved`, left/right dispatch evidence | DLAA; native FSR AA; native TAA/None without vendor execution; configured FSR4 versus physical FSR host/runtime/fallback; explicit false preference; missing/mixed/stale vendor evidence                                     | Actual native TAA/None image output and DLAA/FSR AA provider execution on this build; hardware/provider availability matrix                                       | None                                                                                                              | IMPLEMENTED + TESTED; RUNTIME_EVIDENCE_PENDING |

## Production-to-test trace

All eight requested tests include the corresponding production header
directly. Their fixtures supply inputs and expected results; they do not
maintain another implementation of the policy.

| Production header                                               | Production caller and evidence path                                                                                                                                                                                    | Executed test surface                                                                                                                                                                                                                       |
| --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/Features/Upscaling/StreamlineFrameTokenPublication.h`      | `Streamline.cpp`: `AcquireFrameToken` validates the SDK token value, then DLSS passes the returned token to both `CheckFrameConstants` and `slEvaluateFeature`; Reflex passes the same coordinator's snapshot to sleep | `streamline_frame_token_publication_test.cpp`: synchronized concurrent callers, acquisition failure/retry, stale frame, both reset scopes and wrap                                                                                          |
| `src/Features/Upscaling/VRRenderScaleAuthorityPolicy.h`         | `Upscaling.cpp`: `GetVRRenderScaleAuthorityDiagnosticSnapshot` gathers concrete owners, resolves the map and reports controller revision stability; bridge `AuthorityJson` marks it `diagnosticOnly`                   | `vr_render_scale_authority_policy_test.cpp`: all 25 owner classes, all service mappings, derived-only input, hint clearing, recovery/mutation transfers and inconsistent compound state                                                     |
| `src/Features/Upscaling/VRRenderScaleQualificationPolicy.h`     | Bridge `BuildQualificationObservation` builds facts from public API, physical contract, provider lifecycle, paired presentation and stress deltas; `EvaluateMilestones` and terminal-diagnostic policy evaluate them   | `vr_render_scale_qualification_policy_test.cpp`: target/profile matching, ownership, observation freshness, milestones, native execution, physical backend separation, stereo history, counters, shader requirements and timeout arithmetic |
| `src/Features/Upscaling/VRRenderScaleModePolicy.h`              | `Upscaling.cpp` UI, FPS Stabilizer profile and request paths; `Api/UpscalingService.cpp` profile normalization                                                                                                         | `vr_render_scale_mode_policy_test.cpp`: vendor/non-vendor matrix, native preference retention, explicit disabled/link-disabled profiles; link-contract test checks production wiring                                                        |
| `src/Features/Upscaling/VRSubmitTemporalSnapshot.h`             | `CaptureSubmitTemporalSnapshot` captures both cameras and retains depth/motion resources; dispatch scope exposes it to `Streamline::CheckFrameConstants` and FidelityFX temporal parameters                            | `vr_submit_temporal_snapshot_test.cpp`: immutable inputs, invalid values, exact contract, reset/invalidation, frame and cycle wrap, next-cycle freshness, desktop Present and pending recovery reset                                        |
| `src/Features/Upscaling/VRSubmitStereoBatch.h`                  | Successful FSR stereo dispatch records the full batch and immutable dispatch proof; later eye reuse checks the same source, resources, generation and compositor cycle                                                 | `vr_submit_stereo_batch_test.cpp`: every batch identity component, original proof after Present, failed/wrong-frame proof, frame-zero normalization and telemetry-disabled build                                                            |
| `src/Features/Upscaling/VRRelatchReleasePolicy.h`               | `IsVRRenderScaleOwnedDrainConsumable`, `CanUseVRRenderScaleOwnedRelease`, `RevalidateVRRenderScaleOwnedRelease` and final submit promotion                                                                             | `vr_relatch_release_policy_test.cpp`: exact owned retry history, intermediate suffix, engine targets, trim and complete obligations; extracted receipt/promotion tests exercise production assembly and revocation                          |
| `src/Features/Upscaling/VRPresentationStretchTelemetryPolicy.h` | `RecordVRRenderScalePresentation` observes complete stereo cycles; stress stop snapshots the active episode and incomplete cycle before closing; stress record generation classifies gates                             | `vr_presentation_stretch_telemetry_policy_test.cpp`: allowed/failure path separation, episodes, duplicate/cycle edges, active/incomplete stop, QPC validity, attribution, overflow and diagnostic threshold                                 |

`extract_vr_relatch_drain_controller.cmake`,
`extract_vr_relatch_native_boundary.cmake` and
`extract_vr_relatch_promotion.cmake` read current production source and
generate the code compiled by their integration fixtures. They do not
check in a second implementation. Their environment/provider mocks are
not substitutes for real device testing. `VRRenderScaleDevBenchContract`
and `VRRenderScaleLinkContract` are source-wiring checks; they are not
runtime bridge execution tests.

## Authoritative owner and service catalogue

The owner/service map is a diagnostic audit of existing work, not a new
scheduler. `ConfigureUpscaling` services concrete queued/recovery work;
the native submit-pair hook services relatch after the completed pair.
Intermediate cleanup remains serviced outside the broader handoff gate.
Existing loading and mutation guards continue to protect destructive
maintenance. Provider reset and resource checks also run at their render
or submit service points.

| Owner class                      | Acquisition / authoritative state                                                    | Service and transfer / release path                                                                                                            |
| -------------------------------- | ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `ControllerTransition`           | Requested/applying controller profile and nonzero target epoch                       | Apply pending transition and queued physical relatch; applied publication moves into stabilization                                             |
| `ControllerPresentation`         | Stabilizing controller with target epoch                                             | Submit promotion and stable publication; existing stereo/release checks finish stabilization                                                   |
| `PendingRequest`                 | `QueueVRRenderScaleRequest` stores concrete request ID and epoch under request mutex | `ApplyPendingVRUpscalingTransition`; supersession/coalescing is explicit                                                                       |
| `DeferredRequest`                | Concrete deferred request retained during unresolved physical recovery               | `ServiceDeferredVRRenderScaleRequestAfterPhysicalRecovery`; latest-wins transfer back to pending                                               |
| `PhysicalRelatch`                | Queued/in-progress physical worker with nonzero epoch                                | `ServiceVRRenderScaleRelatchAtFrameBoundary` and `ApplyPendingPerfModeRenderTargetRecreate`; validate current owner before mutation/completion |
| `PendingPostLoadReset`           | `RequestPostLoadRuntimeReset`, pending flag and epoch                                | `ApplyPendingPostLoadRuntimeReset`; reset/recovery handoff                                                                                     |
| `DeferredPostLoadRecovery`       | `DeferVRRenderScalePostLoadRecoveryUntilStable`, deferred epoch                      | `ServiceDeferredVRRenderScalePostLoadRecovery`; admit active recovery or explicitly abandon obsolete work                                      |
| `ActivePostLoadRecovery`         | `PrepareVRRenderScalePostLoadRecovery`, active recovery epoch                        | Relatch, cleanup and trim service; `CompleteVRRenderScalePostLoadRecovery` checks ownership                                                    |
| `PreMutationNativeFallback`      | Fallback transition epoch plus active admission                                      | `ServiceVRRenderScalePreMutationNativeFallbackWatchdog`; owned fallback clear or recovery transfer                                             |
| `ProviderNeutralNativeRecovery`  | Recovery epoch matching the live physical worker                                     | Pre-mutation recovery and queued relatch service; stale standalone hint cannot create this owner                                               |
| `UnresolvedPhysicalMutation`     | `MarkVRRenderScalePhysicalMutationUnresolved`, physical mutation epoch               | Post-mutation watchdog and mandatory recovery; owned physical clear; remains visible without serialization                                     |
| `PostMutationSerialization`      | Serialization epoch and recovery chain serial                                        | `TransferVRRenderScalePostMutationOwner`, watchdog, `TryRetireVRRenderScalePostMutationSerialization`; cannot retire a different chain         |
| `VendorWorkGate`                 | `ArmVRVendorWorkGate`, packed owner mask/epoch                                       | Provider/load/relatch service; `ReleaseVRVendorWorkGateIfUnchanged` and converged game-entry release                                           |
| `PostLoadCompositorHold`         | `ArmVRPostLoadCompositorHold`, protected hold epoch/state                            | Cycle-start and `CompleteVRPostLoadCompositorSubmit`; accepted both-eye completion, quarantine or owned reset                                  |
| `NativeRestore`                  | `BeginVRLowPeakNativeRestoreProgress`, owner epoch and phase                         | Physical relatch, retirement and provider teardown; progress/complete/abort methods retain exact ownership                                     |
| `NativeRestorePresentationGuard` | `ArmVRNativeRestorePresentationGuard`, epoch                                         | `ServiceVRNativeRestorePresentationRecovery` and accepted native presentation; final release checks                                            |
| `IntermediateRetirement`         | Retired intermediate sets, cleanup frame, pending fence/capacity                     | `ServiceVRIntermediateTextureCleanup`; exact retirement accounting before release                                                              |
| `EngineTargetRetirement`         | Retired engine generations, pending releases/fence/capacity                          | `ServiceVREngineTargetRetirement`; reconciled ownership and fences before release                                                              |
| `MemoryTrim`                     | `ArmVRRenderScaleMemoryTrim`, pending flag and owner epoch                           | `ServiceVRRenderScaleMemoryTrim`; same-owner completion and failure accounting                                                                 |
| `DLSSReset`                      | Pending DLSS reset and generation                                                    | `ApplyPendingVendorRuntimeReset` / resource recreation; matching-generation completion                                                         |
| `FSRReset`                       | Pending FSR reset and generation                                                     | Same reset service, with FSR drain/quarantine/device-loss guards                                                                               |
| `DLSSLifecycle`                  | Recorded dirty/draining/destroying/creating state backed by provider work            | DLSS reset/resource service; lifecycle phase alone does not authorize destruction                                                              |
| `FSRLifecycle`                   | Recorded FSR lifecycle work                                                          | FSR reset/resource service; provider drain tickets and actual resources govern mutation                                                        |
| `ResourceTrackingSync`           | Pending resource-tracking synchronization set by relatch                             | `CheckResources` synchronizes tracking or clears an already-reconciled request                                                                 |
| `FpsStabilizerSync`              | `QueueVRFpsStabilizerLoadSync`, pending frame                                        | `ApplyPendingVRFpsStabilizerLoadSync` from `ConfigureUpscaling`; queued profile handoff                                                        |

The authority tests separately toggle controller-state mirrors, deferred
request hints, stable-runtime-profile hints, viewport preparation, cycle
drain and awaiting-sync diagnostics. These cannot create or erase an
owner in the resolution. The production observer also exposes inconsistent
compound ownership and non-atomic controller observation through its
inconsistency mask and `controllerRevisionStable`; it cannot silently
turn that observation into mutation permission.

## Identity, milestones and physical execution findings

The public API binds an operation to the renderer request ID and epoch.
Renderer request IDs use a monotonic allocator; API completion follows
that request identity. Queue, physical-worker and controller ownership
are checked under their existing locks. Generation binding updates only
profiles owned by the matching epoch. Applied and stable publication
recheck current ownership. Owned-release assembly additionally rejects
changed request, epoch, generation, method, device, context, queue, fence,
provider ticket, recovery and cleanup obligations. The promotion fixture
executes 37 corrupted-receipt cases against the extracted production
admission method, plus final next-cycle revocation.

Presentation/fidelity qualification compares both eyes with the stable
physical contract, epoch, generation, method, dimensions, frame and
compositor cycle. FSR batch reuse retains the original dispatch proof
when desktop Present advances. Active per-eye FSR dispatches require
distinct serials; a native full-stereo batch supplies one shared serial.
These are different production dispatch shapes, not interchangeable
receipt evidence. Replacement proof also correlates the expected request,
epoch, generation, publication revision and device identity.

`EvaluateMilestones` separates presentation from cleanup; strict
completion requires both. Historical diagnostics remain visible even
after current presentation recovers. The two-frame stretch comparison
retains its raw `passed` value with `classification = diagnostic_only`.
Every other failed producer health gate still sets acceptance false.
Terminal state, vendor/bounds fallback, coherent backend/presentation,
retirement/trim, attribution/timing, active episode at stop and incomplete
stereo at stop remain independently checked. No cutoff, settling period,
qualification count or visual requirement was changed.

Native AA is a profile/execution combination, not merely native-sized
textures. DLSS at native quality requires coherent DLSS eye evidence
(DLAA); FSR at native quality requires coherent FSR dispatch evidence.
None/TAA require the native physical/presentation contract without vendor
evaluation. Requested/effective/stable profiles must agree separately
from physical backend evidence. An FSR4 preference may coexist with a
reported FSR host/runtime/fallback execution; the actual backend and
fallback remain explicit. Unit tests establish these admission rules,
not visual quality or the execution of the game's TAA shader.

## Demonstrated gap and correction

The original token coordinator rejected old frames while a publication
existed. `Reset(Lifecycle)` erased that publication, including its frame.
The production calls this reset during relatch and provider/resource
teardown. A delayed frame-10 caller could therefore acquire after frame
11 had already been published. The engine frame counter advances across
these lifecycle resets; it is not reset by them.

The added `TestLifecycleResetCannotReopenStaleFrame` directly includes and
invokes the production coordinator. Before the correction, its built
CTest executable failed with exit 8 and:

```text
Lifecycle reset reopened stale frame 10 after frame 11
```

The coordinator now retains the last published frame and resets only its
optional token. Same-frame reacquisition after lifecycle reset and later
frames remain allowed; older frames remain rejected even if reacquisition
fails. Dispatch-failure reset retains its existing behavior. The wrap test
also crosses a lifecycle reset. This changes stale-token admission in the
shared Streamline path for SE/AE/VR; it does not alter renderer scheduling,
provider selection, compositor holds, settling or resource ownership.

## Validation record and remaining evidence

The failing-first build and test, followed by the corrected build and
two-test pass, are preserved under:

```text
build/validation/step4-audit-20260914T225406827Z/
```

Commands used for the failure and correction:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target streamline_frame_token_publication_test -- /m:1
& D:/Coding/GitHub/_tools/CMake/4.4.1/bin/ctest.exe --test-dir build/ALL -C Release -R '^StreamlineFrameTokenPublication$' --output-on-failure --no-tests=error --timeout 300
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target streamline_frame_token_publication_test vr_submit_temporal_snapshot_test -- /m:1
& D:/Coding/GitHub/_tools/CMake/4.4.1/bin/ctest.exe --test-dir build/ALL -C Release -R '^(StreamlineFrameTokenPublication|VRSubmitTemporalSnapshot)$' --output-on-failure --no-tests=error --timeout 300
```

The complete DLL/controller/shader/CTest/preset validation uses the
repository runner and preserves the exact dirty source digest, Build ID,
inventory, compiler, stage commands, durations and complete logs in
`exact-build/summary.json` beneath that directory:

```powershell
pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/step4-audit-20260914T225406827Z/exact-build
pwsh ./tools/pre-commit.ps1 run --files src/Features/Upscaling/StreamlineFrameTokenPublication.h tests/streamline_frame_token_publication_test.cpp docs/development/vr-render-scale-tasks-1-5-audit.md docs/development/vr-render-scale-iteration.md
pwsh ./tools/git.ps1 diff --check
```

Exact-build runtime evidence still requires correlated operation/controller
receipts, both-eye HMD submissions, native vendor dispatch and Streamline
constants/evaluation traces. GPU fences, actual SDK lifetime and device
recovery are mocked or source-audited here. No HMD run, visual assay,
performance measurement or SE/AE gameplay test was performed. Existing
physical-HMD qualification and its scene/profile matrix remain unchanged.
