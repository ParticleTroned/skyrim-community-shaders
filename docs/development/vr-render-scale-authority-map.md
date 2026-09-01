# VR render-scale authority and liveness map

This document identifies every render-scale publication that can own work,
block a transition, or suppress presentation. It also separates those owners
from mirrors and evidence that must never decide whether work is serviced.

The executable companion is
`VRRenderScaleAuthorityPolicy.h`. Its resolver is diagnostic and test
authority only. Production scheduling must continue to inspect the owners
listed here directly.

## Safety rule

An authoritative owner is the source of truth for unfinished work. A release
publication may make a compound tuple visible after its payload is complete.
A derivative field can accelerate a read or describe the owner, but it cannot
prove that authoritative work is absent.

The former `vrRenderScaleMaintenanceWork` wake hint violated that distinction.
It could become empty while the physical-relatch owner was still live, so the
first relatch lost its service path. Commits `6e56b5426` and `5fba8965a`
introduced and hardened that gate; `d6d821404` removed it. The diagnostic
resolver deliberately has no production caller and must not become a wake
mask, cache, or early-return condition.

## Service classes

| Service class                 | Production service path                                                                                                                      |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `queued_relatch_work`         | `ConfigureUpscaling`, `ApplyVRRenderScaleRequest`, `ApplyPendingPerfModeRenderTargetRecreate`, and `ApplyPendingVRFpsStabilizerLoadSync`     |
| `pre_mutation_fallback_work`  | `ServiceVRRenderScalePreMutationNativeFallbackWatchdog` at the end-of-frame boundary and the exact provider-neutral successor path           |
| `post_load_recovery_work`     | `ApplyPendingPostLoadRuntimeReset`, `ServiceDeferredVRRenderScalePostLoadRecovery`, and recovery-state convergence in `ConfigureUpscaling`   |
| `post_mutation_recovery_work` | `ServiceVRRenderScalePostMutationWatchdog`, physical contract publication, coherent stereo promotion, and terminal recovery                  |
| `retirement_work`             | `CleanupRetiredVRIntermediateTextures`, `ServiceVREngineTargetRetirement`, and the native-restore relatch path                               |
| `trim_work`                   | `ServiceVRRenderScaleMemoryTrim` from bounded configure, recovery, and admission points                                                      |
| `presentation_hold_work`      | submit-hook compositor cycles, `TryPromoteVRRenderScaleSubmitStageContract`, hold release, and native-restore presentation watchdog handling |
| `provider_lifecycle_work`     | `EnsureResourcesCurrent`, `CheckResources`, `ApplyPendingVendorRuntimeReset`, provider teardown/recreation, and work-gate release callbacks  |

## Authoritative owners

### Transition and request owners

| Owner                 | Identity and idle state                                                                                                                                                                                                                                                               | Publication, lock, and ordering                                                                                                                                                                                                                                | Service, transfer, and clear                                                                                                                                                                                                                                                                               |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Transition controller | `vrRenderScaleTransitionController.state`, `targetEpoch`, and the exact `requested`, `applying`, `applied`, and `stable` profiles under `vrRenderScaleTransitionControllerMutex`. `Idle` and `Active` have no transition work; `Stabilizing` still owns presentation proof.           | Request admission publishes a complete profile and epoch before changing state. `StoreVRRenderScaleTransitionStateLocked` publishes the atomic state mirror after the compound object changes.                                                                 | `ConfigureUpscaling` advances request and relatch states. Physical publication transfers `Applying` to `Stabilizing`; coherent stereo proof transfers it to `Active`. Reset or supersession clears the active target while preserving independent cleanup and lifecycle snapshots.                         |
| Pending request       | `pendingVRRenderScaleRequest` under `pendingVRRenderScaleRequestMutex`; `nullopt` is idle. A live tuple requires nonzero `requestID` and `transitionEpoch`.                                                                                                                           | `QueueVRRenderScaleRequest` builds the immutable request before assigning the optional. `latestVRRenderScaleRequestID` provides latest-wins comparison, not independent work.                                                                                  | `ConfigureUpscaling` consumes it and records the controller request. It transfers to the physical queue or to the deferred request after destructive mutation. Completion, supersession, or protected fallback clears it.                                                                                  |
| Deferred request      | `deferredVRRenderScaleRequestAfterPhysicalRecovery` under `pendingVRRenderScaleRequestMutex`; `nullopt` is idle.                                                                                                                                                                      | `StoreDeferredVRRenderScaleRequestLatestWinsLocked` publishes the full request first and then sets `deferredVRRenderScaleRequestPending` as a release hint.                                                                                                    | `ReplayDeferredVRRenderScaleRequestAfterPhysicalRecovery` restores the exact tuple to `pendingVRRenderScaleRequest` after serialization retires. Replay or supersession clears the optional and then the hint.                                                                                             |
| Physical relatch      | `pendingPerfModeRenderTargetRecreate`, `perfModeRenderTargetRecreateInProgress`, and `pendingPerfModeRenderTargetRecreateEpoch` plus the force, frame, delay, origin, recovery, and snapshot payload under `perfModeRenderTargetRecreateQueueMutex`. Both worker flags false is idle. | `RequestPerfModeRenderTargetRecreate` fills every payload field and publishes `pendingPerfModeRenderTargetRecreate` last. `ApplyPendingPerfModeRenderTargetRecreate` sets `inProgress` before consuming `pending`, so service ownership has no false-idle gap. | `ConfigureUpscaling` and the permitted draw path call the apply function. The worker transfers to physical-mutation ownership immediately before creator entry, then publishes the applied contract or requeues the same exact epoch. Success, supersession, terminal recovery, or reset clears the tuple. |
| FPS Stabilizer sync   | `pendingVRFpsStabilizerSyncFrame`; zero is idle. Loading serial and API-admission fields are supporting identity under their existing loading/admission locks.                                                                                                                        | `QueueVRFpsStabilizerLoadSync` release-publishes the nonzero frame only after a destination is eligible.                                                                                                                                                       | `ApplyPendingVRFpsStabilizerLoadSync` consumes it from `ConfigureUpscaling`, publishes the requested profile/relatch, and then clears the frame. The destination-identity fallback is 30 frames.                                                                                                           |

The controller can coexist with a pending request during latest-wins
replacement, with a physical relatch during application, and with a compositor
hold during stabilization. A pending or deferred request may delay application
of another request, but it must not suppress a proven current generation before
physical mutation. A stale request can otherwise replay an obsolete profile;
a stale relatch can mutate resources for the wrong epoch.

### Recovery and mutation owners

| Owner                            | Identity and idle state                                                                                                                                                                                                                                                                                                                                   | Publication, lock, and ordering                                                                                                                                                                      | Service, transfer, and clear                                                                                                                                                                                                                                                                                                                            |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Pending post-load reset          | `postLoadRuntimeResetPending` is the release publication; `pendingPostLoadRuntimeResetEpoch` is its payload. The Boolean false is idle for this stage. Ordinary load publication uses `perfModeRenderTargetRecreateQueueMutex`; the deadline-fallback path uses its existing hold/loading/physical/controller lock transaction and admission reservation. | The publisher creates the controller recovery owner, stores its epoch, and publishes the Boolean last. An epoch may remain after this stage transfers, so the epoch alone is not pending-reset work. | `ApplyPendingPostLoadRuntimeReset` claims the Boolean and transfers the epoch to a recovery relatch. Rejection republishes the Boolean; fixed-vendor no-op, supersession, or completion clears it.                                                                                                                                                      |
| Active post-load recovery        | `vrRenderScaleTransitionController.postLoadRecovery.active` and `recoveryEpoch` under the controller mutex; false is idle.                                                                                                                                                                                                                                | `BeginVRRenderScalePostLoadRecoveryLocked` initializes the complete snapshot before returning the owner epoch.                                                                                       | Recovery sampling, trim, physical relatch, and presentation proof update it. It may transfer to deferred recovery while the new contract stabilizes. `CompleteVRRenderScalePostLoadRecovery` clears the exact owner.                                                                                                                                    |
| Deferred post-load recovery      | `deferredVRRenderScalePostLoadRecoveryEpoch`; zero is idle.                                                                                                                                                                                                                                                                                               | `DeferVRRenderScalePostLoadRecoveryUntilStable` publishes only the exact active recovery epoch after binding its transition.                                                                         | `ServiceDeferredVRRenderScalePostLoadRecovery` completes it after presentation/cleanup conditions converge. Compare-exchange prevents an older completion from clearing a newer owner.                                                                                                                                                                  |
| Pre-mutation native fallback     | `vrRenderScalePreMutationNativeFallbackTransitionEpoch` is the release token for admission, hold, loading, recovery, and start-tick fields. Zero is idle; `vrRenderScalePreMutationNativeFallbackAdmissionActive` must agree.                                                                                                                             | The payload is overwritten before release-publishing the transition epoch. Hold and physical locks protect transfers that cross compositor and mutation domains.                                     | The end-of-frame watchdog services it. It either retains a truthful stable contract, queues the provider-neutral native successor, or transfers to post-mutation serialization at creator entry. The ordinary and same-generation deadlines are 15 seconds. Exact clear uses compare-exchange on the epoch.                                             |
| Provider-neutral native recovery | `vrRenderScaleProviderNeutralNativeRecoveryEpoch` is live only when the physical-relatch queue or in-progress worker has the same epoch and requests the forced native recovery tuple. Zero or a mismatched worker is idle and diagnostically inconsistent.                                                                                               | The native request and relatch tuple are published first; the provider-neutral epoch is published last.                                                                                              | The physical-relatch service consumes it. Creator entry transfers to post-mutation ownership; convergence or exact supersession clears it. A lone stale marker must never create work.                                                                                                                                                                  |
| Unresolved physical mutation     | `vrRenderScaleUnresolvedPhysicalMutationEpoch` with start/progress fields under `vrRenderScalePhysicalMutationMutex`; zero is idle.                                                                                                                                                                                                                       | `PublishVRRenderScalePhysicalMutation` first establishes the immutable serialization chain, then publishes the unresolved creator epoch before destructive work.                                     | Creator progress and contract publication service it. Exact coherent physical publication clears only the unresolved epoch, leaving serialization live. Device loss or terminal recovery handles failure.                                                                                                                                               |
| Post-mutation serialization      | `vrRenderScalePostMutationSerializationEpoch` and nonzero `vrRenderScalePostMutationChainSerial` under `vrRenderScalePhysicalMutationMutex`; zero epoch is idle.                                                                                                                                                                                          | The chain serial and timing payload are initialized on the zero-to-nonzero boundary. Retry, recovery, and load events cannot renew the clock.                                                        | Coherent presentation, compositor-hold release, recovery, and `ServiceVRRenderScalePostMutationWatchdog` service it. It retires only after the exact presentation chain is safe. Emergency creator service begins after 2 seconds; terminal limits are 15 seconds without progress or 60 seconds with progress, with the documented debugger extension. |

Unresolved physical mutation and serialization must coexist from creator entry
until physical publication. Serialization then legitimately outlives the
unresolved marker through stereo proof. The resolver therefore reports
unresolved mutation as post-mutation work even if serialization is missing and
also reports that combination as an inconsistency.

These owners may block new request _application_ so a mandatory recovery is not
superseded. Only unresolved mutation or an exact post-mutation recovery contract
may invalidate the old generation. Pending admission, memory rejection, shader
preparation, or queued recovery before mutation must not do so.

### Presentation and native-restore owners

| Owner                             | Identity and idle state                                                                                                                                                                                                                                                                                                                      | Publication, lock, and ordering                                                                                                                                                                                                                           | Service, transfer, and clear                                                                                                                                                                                                                                                                                                                             |
| --------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Vendor work gate                  | The packed `vrVendorWorkGateState` contains the authoritative source mask and reacquisition epoch. An empty mask is idle.                                                                                                                                                                                                                    | `ArmVRVendorWorkGate` and release/reconciliation paths update the mask with compare-exchange; reacquiring the same source advances the epoch.                                                                                                             | Menu, load, and completed-world-frame callbacks release their own source. The gate may block provider lifecycle mutation, but Task 2 exact-provider admission permits proven current presentation where safe. A stale source can indefinitely defer creation/destruction.                                                                                |
| Post-load compositor hold         | `vrPostLoadCompositorHoldState` and `vrPostLoadCompositorHoldEpoch`, with route, loading serial, deadlines, release contract, candidate stereo identities, repair evidence, in-flight counts, quarantine, and Fader ownership under `vrPostLoadCompositorHoldMutex`. `Idle` is idle; `Completed` remains a cycle-boundary owner until reset. | Arm keeps the lock-free state observably non-idle while `ResetVRPostLoadCompositorHoldLocked` advances the epoch and initializes payload. State is published after payload. Release records exact stereo and mutation identity before scheduling release. | OpenVR submit scopes and compositor-cycle callbacks advance and drain it. Coherent stereo releases the hold; ambiguous occupied cycles are quarantined. Soft deadlines are 1 second for render transitions, 3 seconds in-world, and 6 seconds in the main menu, each with 0.5-second hard-deadline grace. It can block presentation and the Fader clock. |
| Native-restore progress           | `vrLowPeakNativeRestoreProgress` and `vrLowPeakNativeRestoreOperation` under `vrLowPeakNativeRestoreProgressMutex`; zero owner or `Idle` is idle.                                                                                                                                                                                            | `BeginVRLowPeakNativeRestoreProgress` binds an exact epoch and operation. Retirement serial is attached only to the same owner.                                                                                                                           | The physical-relatch path polls vendor teardown, intermediate retirement, and the exact fence serial before recreating targets. `Complete` transfers back to the queued relatch. Exact completion/abort clears it. The 60-frame recovery backoff is pacing, while the enclosing mutation/fallback watchdog supplies liveness.                            |
| Native-restore presentation guard | `vrNativeRestorePresentationGuardEpoch`; zero is idle. Watchdog counters under `vrNativeRestorePresentationWatchdogMutex` are payload.                                                                                                                                                                                                       | `ArmVRNativeRestorePresentationGuard` publishes the exact transition epoch and resets watchdog evidence when ownership changes.                                                                                                                           | Native presentation proof or bounded fail-open/recovery clears the exact epoch. This owner may suppress unproven native output; stale ownership can leave presentation permanently guarded.                                                                                                                                                              |
| Controller presentation           | Controller state `Stabilizing` with nonzero `targetEpoch` and exact `applied` contract.                                                                                                                                                                                                                                                      | Physical contract publication writes `applied` before publishing `Stabilizing`.                                                                                                                                                                           | `TryPromoteVRRenderScaleSubmitStageContract`, native presentation observation, and `PublishVRRenderScaleTransitionStable` service it. It transfers to `Active` only after coherent proof. The required ordinary/door vendor proof is three/two stable stereo frames.                                                                                     |

The compositor hold, controller stabilization, native guard, and post-mutation
serialization commonly coexist. Their exact epochs determine which one may
release another. They may block presentation only when the current texture or
generation is unproven, a compositor cycle is already occupied, or destructive
mutation has started. They must not convert unrelated trim or a queued
replacement into presentation suppression.

### Provider and resource owners

| Owner                  | Identity and idle state                                                                                                                                                                                                  | Publication, lock, and ordering                                                                                                                                 | Service, transfer, and clear                                                                                                                                                                                           |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DLSS reset             | `pendingDLSSReset` is the release publication and `pendingDLSSResetGeneration` is the resource identity. False is idle.                                                                                                  | `MarkVendorRuntimeResourcesDirty` stores generation before publishing the Boolean.                                                                              | `ApplyPendingVendorRuntimeReset` or the physical relatch claims it and performs Streamline drain/recreation. Failure republishes dirty ownership; exact ready/clear paths zero generation before clearing the Boolean. |
| FSR reset              | `pendingFSRReset` is the release publication and `pendingFSRResetGeneration` is the resource identity. False is idle.                                                                                                    | The same generation-before-Boolean ordering is used for FidelityFX ownership.                                                                                   | `ApplyPendingVendorRuntimeReset`, physical relatch, and FSR teardown polling service it. Quarantined opaque host ownership is retained rather than destroyed blindly. Ready/clear paths retire the exact reset.        |
| DLSS lifecycle         | `vrRenderScaleTransitionController.dlssLifecycle` in phases `Dirty`, `WaitingForDrain`, `Destroying`, or `Creating`, under the controller mutex. `Inactive`, `Ready`, and terminal `Failed` are not live service phases. | `RecordVRVendorRuntimeLifecycle` records method, backend, target epoch, requested/runtime generation, resource presence, and counters in one controller update. | Streamline polling and recreation update it. It may overlap a reset owner; `Ready` transfers authority to the published provider contract. A stale retiring phase can defer successors.                                |
| FSR lifecycle          | `vrRenderScaleTransitionController.fsrLifecycle` with the same active phases and lock.                                                                                                                                   | FidelityFX teardown/recreation publishes the complete lifecycle snapshot.                                                                                       | FSR drain, teardown, runtime/host creation, quarantine, and fallback update it. It may overlap the FSR reset. Terminal failure is evidence, not pending service, until a new request or recovery owner is published.   |
| Resource-tracking sync | `vrRenderScaleResourceTrackingSyncPending`; false is idle.                                                                                                                                                               | A successful physical relatch publishes it after render-target/provider work and before ordinary resource-change bookkeeping resumes.                           | `CheckResources` synchronizes `previous*` resource keys to the applied controller contract and clears it. It can block the resource stable-cache hit but not presentation by itself.                                   |

Provider reset ownership can block evaluation only when its exact reset
generation invalidates the offered provider. A queued replacement reset for a
different generation must not erase an exact current-provider contract.
Lifecycle phases can block successor application or mutation; the work gate
does not independently invalidate exact current resources.

### Cleanup owners

| Owner                    | Identity and idle state                                                                                                                                                                                                                                                          | Publication, lock, and ordering                                                                                                                                                                                           | Service, transfer, and clear                                                                                                                                                                                                                                          |
| ------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Intermediate retirement  | `retiredVRIntermediateTextures`, `deferredVRIntermediateTextureCleanupFrame`, `vrIntermediateTextureCleanupFence`, and `vrIntermediateTextureCleanupFenceBatchMaxSerial`; an empty collection with no frame/fence is idle. Serial atomics identify issued and completed batches. | The render thread moves strong texture ownership into a bounded generation record, publishes serials, and updates the controller retirement snapshot under its mutex. A fence batch owns only the captured serial prefix. | `CleanupRetiredVRIntermediateTextures` waits at least the four-frame tail and then the D3D fence before releasing strong ownership. Capacity can defer replacement mutation, never current presentation.                                                              |
| Engine-target retirement | `retiredVREngineTargetGeneration`, `vrEngineTargetRetirementFence`, and retained/poisoned provenance collections; no generation/fence is idle.                                                                                                                                   | Native creator capture validates and retains exact `IUnknown` identities before old slots can be released. The controller snapshot is updated after changes.                                                              | `ServiceVREngineTargetRetirement` fences and releases proven old target identities. Unproven identities remain retained or poisoned. Capacity may block another destructive mutation, not presentation.                                                               |
| Memory trim              | `vrRenderScaleMemoryTrimPending` is the release publication for `vrRenderScaleMemoryTrimOwnerEpoch`, reason, frame, and fence. False is idle.                                                                                                                                    | `ArmVRRenderScaleMemoryTrim` writes the owner payload, updates the controller snapshot, then publishes the Boolean.                                                                                                       | `ServiceVRRenderScaleMemoryTrim` polls its bounded fence and trim operation, records completion, clears controller payload, and finally clears the Boolean. Trim can block memory admission but must not withhold presentation stability for an unrelated generation. |

Cleanup owners may coexist with every stable or replacement state when their
resources are strongly retained. They can refuse a new destructive overlap at
capacity or under memory pressure. They are not allowed to suppress a proven
current presentation. Leaving them stale retains memory and can indefinitely
block later allocation; clearing them early risks GPU use-after-free.

## Deadlines and stale-owner failures

`None` means the owner is serviced opportunistically or is covered by an
enclosing recovery deadline. This audit does not add or alter a deadline.

| Owner                             | Deadline or watchdog                                                            | Failure if stale                                                                        |
| --------------------------------- | ------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Transition controller             | Relatch pacing before mutation; mutation and presentation owners after transfer | Request remains non-active, later requests defer, or presentation never promotes.       |
| Controller presentation           | Two door-handoff or three ordinary coherent stereo frames                       | Current contract remains in stabilization and hold/serialization may not retire.        |
| Pending request                   | None; serviced by each eligible configure cycle                                 | An obsolete profile may be applied later or a newer request may never advance.          |
| Deferred request                  | Enclosing post-mutation chain                                                   | Latest user intent never replays after mandatory recovery.                              |
| Physical relatch                  | Same-frame floor plus selected 1/6/60/120/300/600-frame pacing                  | Physical transition never starts, or an obsolete tuple mutates the wrong target.        |
| FPS Stabilizer sync               | 30-frame destination-identity fallback                                          | Door handoff remains unresolved and can delay activation.                               |
| Pending post-load reset           | Enclosing recovery/hold deadlines                                               | Post-load recovery never transfers to its relatch worker.                               |
| Active post-load recovery         | 120-frame memory-settle bound plus enclosing hold/mutation deadlines            | Recovery and cleanup remain attributed to a load that has already completed.            |
| Deferred post-load recovery       | Enclosing presentation proof                                                    | Recovery never closes after stable presentation.                                        |
| Pre-mutation native fallback      | 15 seconds                                                                      | A blocked, non-destructive replacement can retain a cover indefinitely.                 |
| Provider-neutral native recovery  | Enclosing fallback or mutation deadline                                         | Mandatory native recovery can be stranded or a lone marker can misdescribe work.        |
| Unresolved physical mutation      | 2-second emergency service and post-mutation deadline                           | Old resources could be treated as current, or presentation remains fail-closed forever. |
| Post-mutation serialization       | 15 seconds stalled / 60 seconds progressing, with debugger extension            | New requests remain deferred and the terminal recovery chain never retires.             |
| Vendor work gate                  | Source lifecycle plus 6.5-second liveness cue for recovery visibility           | Provider creation/destruction remains deferred after the owning menu/load ended.        |
| Post-load compositor hold         | 1/3/6-second route soft deadline plus 0.5-second hard grace                     | Black keepalive, frozen fade, or quarantined-cycle ownership remains visible.           |
| Native-restore progress           | Enclosing recovery chain; 60-frame retry pacing                                 | Vendor or retired resources remain owned and native target recreation cannot proceed.   |
| Native-restore presentation guard | Native presentation watchdog and enclosing mutation chain                       | Native output remains suppressed despite a valid completed restore.                     |
| DLSS reset                        | Provider polling/retry cadence                                                  | Valid DLSS evaluation remains blocked or stale resources are reused.                    |
| FSR reset                         | Provider polling/retry cadence                                                  | Valid FSR evaluation remains blocked or opaque resources are destroyed/reused unsafely. |
| DLSS lifecycle                    | Provider polling/retry cadence                                                  | A successor is indefinitely classified as conflicting with DLSS retirement.             |
| FSR lifecycle                     | Provider polling/retry cadence                                                  | A successor is indefinitely classified as conflicting with FSR retirement.              |
| Resource-tracking sync            | Next eligible `CheckResources` call                                             | Stable-cache admission remains disabled and resource changes may be reprocessed.        |
| Intermediate retirement           | Four-frame tail plus D3D fence                                                  | VRAM remains retained or capacity blocks another overlap.                               |
| Engine-target retirement          | D3D fence                                                                       | Engine resources remain retained/poisoned or capacity blocks mutation.                  |
| Memory trim                       | D3D fence and bounded retry/failure accounting                                  | Admission remains blocked or a trim result is attributed to the wrong epoch.            |

## Coexistence matrix

The matrix groups owners only where their coexistence rule is identical. `Y`
is an ordinary valid overlap, `E` requires matching exact epoch/identity, `T`
is a publication-transfer overlap, and `D` means only a deferred request may
survive. A mismatched exact owner is invalid even in a `Y` cell.

| Group                             | R   | Q   | L   | F   | M   | P   | V   | C   | S   |
| --------------------------------- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| R request/controller              | Y   | T   | Y   | D   | D   | Y   | Y   | Y   | Y   |
| Q physical relatch                | T   | E   | E   | T   | T   | Y   | Y   | Y   | Y   |
| L post-load recovery              | Y   | E   | E   | E   | E   | E   | Y   | Y   | Y   |
| F pre-mutation fallback           | D   | T   | E   | E   | T   | E   | Y   | Y   | Y   |
| M physical mutation/serialization | D   | T   | E   | T   | E   | E   | E   | Y   | Y   |
| P presentation/hold/guard         | Y   | Y   | E   | E   | E   | E   | Y   | Y   | Y   |
| V work gate/provider/reset/sync   | Y   | Y   | Y   | Y   | E   | Y   | Y   | Y   | Y   |
| C native restore/retirement/trim  | Y   | Y   | Y   | Y   | Y   | Y   | Y   | Y   | Y   |
| S FPS Stabilizer sync             | Y   | Y   | Y   | Y   | Y   | Y   | Y   | Y   | E   |

Within `R`, the pending request may overlap the controller during publication;
after mutation, only the deferred request may retain newer user intent. Within
`C`, native restore uses exact epoch/retirement serials even though unrelated
cleanup may coexist freely. Within `V`, a reset and its lifecycle phase may
overlap, while an unrelated current provider can remain present. Within `P`,
the hold, controller stabilization, and guard must bind to the same transition
before one may release another.

## Derived and diagnostic publications

The following fields do not create a service class in the executable resolver:

| Symbol                                                                                         | Classification and rule                                                                                                                            |
| ---------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `vrRenderScaleTransitionState`                                                                 | Derived hot-path mirror of controller state. Clearing or lagging this mirror must not hide controller-owned work.                                  |
| `deferredVRRenderScaleRequestPending`                                                          | Derived mutex-avoidance hint. The optional request is authoritative.                                                                               |
| `vrRenderScaleStableRuntimeProfileAuthoritative`                                               | Derived fast admission for reading the controller's stable profile while a replacement waits. It never owns work.                                  |
| `attemptedVRRenderScalePreparationRequestID` and `preparedVRRenderScale*`                      | Preparation attempt and immutable readiness proof. The pending request owns the one-shot synchronous preparation; these fields do not schedule it. |
| `submitStageDLSSViewportPreparationPending`                                                    | Observation of Streamline slot preparation. Controller stabilization and the lifecycle snapshot own retry service.                                 |
| `vrPostLoadCompositorCycleDrainPending`                                                        | Cycle-boundary payload of the compositor hold/quarantine owner. It cannot create a hold alone.                                                     |
| `vrPostLoadCompositorHoldAwaitingSyncEpoch`                                                    | Stabilizer-sync payload valid only for the matching compositor-hold epoch.                                                                         |
| `desiredOwner`, `physicalOwner`, `presentationOwner`, `physicalPhase`, and `presentationPhase` | Orthogonal controller diagnostics. They expose ordering mistakes but do not schedule or suppress work.                                             |
| metrics, fidelity, presentation, preparation, memory, and stress telemetry                     | Diagnostic evidence. Policy may qualify a result from it, but production liveness must come from the owners above.                                 |

The release Booleans for physical relatch, pending post-load reset, provider
reset, and memory trim are not in this table: each is part of an authoritative
compound owner. A Boolean without its required identity is stale and resolves
to no work where the owner contract requires that identity.

## Blocking matrix

`May delay request` means application may wait; accepted latest-wins input can
still be retained. `May block presentation` applies only to the exact safety
condition in the notes above.

| Owner group                                      | May delay request                     | May block presentation                              | May coexist with proven current presentation                      |
| ------------------------------------------------ | ------------------------------------- | --------------------------------------------------- | ----------------------------------------------------------------- |
| Pending/controller/physical queue before creator | Yes                                   | No, by itself                                       | Yes                                                               |
| Pre-mutation fallback/provider-neutral successor | Yes                                   | Only the compositor hold may cover it               | Yes, when the old contract is exact                               |
| Unresolved mutation/serialization                | Yes                                   | Yes                                                 | No after creator entry; old generation is invalid                 |
| Post-load compositor hold/native guard           | Yes                                   | Yes, with bounded release/recovery                  | Yes only through its selected exact stretch/keepalive disposition |
| Vendor work gate                                 | Yes, for mutation                     | No, for an exact current provider                   | Yes                                                               |
| Provider reset/lifecycle                         | Yes                                   | Only when the reset invalidates that exact provider | Yes for an unaffected exact provider                              |
| Retirement/trim                                  | Yes, at capacity or admission failure | No                                                  | Yes                                                               |
| Diagnostic/derived fields                        | No                                    | No                                                  | Yes                                                               |

## Executable invariants

`VRRenderScaleAuthorityPolicy::Resolve` accepts a value snapshot and returns:

-   `owners`: all live authoritative owners;
-   `services`: the union of their required service classes;
-   `unmappedOwners`: owners without a registered service path;
-   `inconsistencies`: impossible or incomplete compound tuples.

The policy is pure, allocation-free, and has no mutable storage. A compile-time
mapping assertion and the focused controller test establish that:

1. every owner enumerator resolves to its expected nonzero service class;
2. derived fields alone create no work;
3. clearing derived fields cannot hide a live owner;
4. request-to-relatch transfers remain serviceable at source, overlap, and
   destination observations;
5. unresolved physical mutation never resolves idle, even with missing
   serialization;
6. a stale relatch Boolean without an epoch creates no work; and
7. adding an owner without exactly one nonzero mapping fails compilation.

DevBench exposes this as `authorityLiveness` in status, including decoded masks,
the compound owner summary, inconsistencies, and whether the controller
revision remained stable across the cross-domain sample. This status is
observational. No production service path reads the result.

## Audit result and follow-up boundary

The current audit found service points for every mapped authoritative owner.
No production liveness defect was proven, so this work changes no transition,
presentation, timeout, memory-admission, or scheduling behavior.

### Runtime validation

The `nvidia-mtlid7m3` assay exercised build
`54b17d9e36fdbdeead739396a4637acc529bf43867ce39e47db17a6175a14a56`
through 66 transitions in two passes. All transitions rendered and qualified,
all 32 presentation-stretch episodes recovered, and no device loss, OOM,
terminal provider failure, or credible liveness timeout occurred.

The retained evidence contained 159 decoded `authorityLiveness` status
payloads. Every payload was controller-revision-stable and reported zero
unmapped owners and zero inconsistent compound owners. All sampled status
boundaries were quiescent, so runtime evidence proves clean retirement but not
each live transfer interval. Compile-time controller tests provide exhaustive
owner-to-service mapping and transfer coverage. Six native-destination Task 2
rows remained inconclusive because of generation-owner correlation; they had
no authoritative invariant violation and are outside this diagnostic policy.

Potential redesigns belong in later evidence-backed work:

1. Replace repeated cross-domain reads with one revision-checked diagnostic
   snapshot, without using it for scheduling.
2. Make reset and resource-sync tuples explicit types so their publication
   ordering is structural rather than conventional.
3. Split controller transition and presentation proof into named subobjects
   only if measurements show the current combined lock is material.
4. Consolidate compositor-hold payload clearing behind one transaction type;
   retain the current epoch and in-flight-cycle proofs.
5. Add a runtime assertion mode that reports persistent owner/service
   inconsistencies after a grace interval; never make it a production gate.
