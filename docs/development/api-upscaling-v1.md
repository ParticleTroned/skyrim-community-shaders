# Upscaling API v1 design

The native upscaling contract is declared in
`include/VRAPI/CSupscalingapi.h`. It is a new `csx.upscaling` service obtained
through the CSX service registry; it does not extend or replace the legacy
`ICSInterface001` vtable.

This branch defines, tests, and registers a live runtime-only provider. The
provider is registered during CSX service-registry initialization only after
its process-lifetime function table exists. It reports live renderer state and
uses the upscaling controller's explicit apply result; no placeholder or
optimistic success response is published.

## Contract identity

| Field                        | Value                                                                                                                  |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| Service name                 | `csx.upscaling`                                                                                                        |
| ABI                          | 1.0                                                                                                                    |
| Schema revision              | 1                                                                                                                      |
| Registry coarse capabilities | inspection, runtime mutation, asynchronous operations, events, transactions; persistent mutation only when implemented |

Major versions are ABI-breaking. Minor versions append function-table entries
or introduce new caller-sized structures without changing the meaning or
layout of existing fields. Schema revisions describe compatible semantic or
metadata additions and do not override ABI negotiation.

## Lessons carried forward

The legacy API remains supported, but new clients should not copy its mutation
pattern:

-   Individual `void` setters can silently clamp, ignore, or partially apply a
    profile. V1 accepts one complete profile and returns a structured result.
-   A separate `IsAllowed`/setter sequence races loading-menu handoff and renderer
    ownership. V1 preflight is advisory; `ApplyProfile` repeats validation,
    preflight, admission, and queue publication as one controller transaction.
-   “Configured” is not the same as “physically active.” V1 exposes configured,
    requested, applying, effective, stable, and persisted profiles separately.
-   Loading is an observed condition, not always an unconditional blocker. An
    environment-profile transition can be admitted through CSX's loading-door
    handoff without exposing or accepting internal serial tokens.
-   FSR4 is an FSR runtime selection, not a fourth upscale method.
-   DLSS currently has six selectable profiles: J, K, L, M, F, and E. V1 does not
    repeat the earlier five-value assumption.
-   Caller-created fades are not part of this API. CSX owns presentation coverage
    for renderer transitions and must not stack a second timed fade.

## Complete profile model

`Profile001` contains the canonical settings which must change together:

-   method: None, TAA, FSR, or DLSS;
-   shared quality mode: Native AA, Hoshipa, Ultra Quality, Quality, Balanced,
    Performance, or Ultra Performance;
-   VR render-scale-mode request;
-   selected DLSS profile;
-   selected FSR runtime (FSR3 or FSR4).

DLSS and FSR selections remain meaningful configuration when their backend is
inactive. A switch from DLSS to TAA therefore does not erase the user's DLSS
profile. Validation rejects unknown values and impossible active combinations:
render-scale mode requires VR, an FSR or DLSS target, and a sub-native quality
mode. Availability checks are performed after structural validation; a
well-formed DLSS profile can still be unsupported on the loaded system.

No field is silently clamped. If future backends or profile values are added,
their support is advertised in `Capabilities001` before clients send them.
Masks use `1ull << static_cast<uint32_t>(value)`.
Capabilities also report the resolution scale for every quality mode and a
condition mask for each unavailable method and FSR runtime. Consumers therefore
do not need to reverse-engineer why DLSS or FSR4 is pending or unavailable.

## State and revisions

`GetSnapshot` returns one coherent read model and a monotonic `stateRevision`.
The profile-presence mask says which profile copies are authoritative:

-   **configured**: current in-memory configuration, without runtime gates such
    as startup presentation or the master shader switch;
-   **requested**: latest admitted target;
-   **applying**: target whose physical work is in progress;
-   **effective**: logical backend and settings used for current dispatch;
-   **stable**: last physically converged renderer/resource contract;
-   **persisted**: last configuration known to have been durably saved.

Unknown state is represented by an absent presence bit, never by manufacturing
a default profile. Runtime dimensions and render-scale/transition state are
captured at the same revision.

Clients may pass the last observed revision to preflight and apply. A changed
revision returns `kStateConflict`; `AnyStateRevision` opts out. Omitting the
check is appropriate for a human “make this the current setting” action but not
for an automation controller performing a read/modify/write sequence.

## Preflight and atomic apply

`PreflightProfile` has no side effects and consumes no idempotency key. Its
decision predicts no-change, synchronous apply, queued apply, blocked, or
unsupported. The result reports both:

-   `observedConditions`: everything relevant that was present; and
-   `blockingConditions`: the subset which blocks this request after its purpose
    and admission route are considered.

The distinction prevents a valid loading-door handoff from hiding the fact
that a loading transition exists.

For a runtime-only request, `no-change` means the effective and stable runtime
profiles already agree with the target and no transition is active. It does
not require the separate configured profile to match, or depend on a
historical controller request slot: a runtime-only target may converge without
rewriting the user's stored configuration source. A successfully admitted API
target is then published as the latest requested profile once the controller
is settled at that target.

VR API profile changes publish through the render-scale transition controller
even when render-scale mode is disabled. Native None, TAA, DLAA, and FSR Native
AA operations do not become stable until one exact target-correlated stereo
presentation is observed. Their public requested, effective, and stable
profiles all retain the logical target. The separate render-scale resource key
remains inactive with backend `none` at native resolution; fixed-resolution
vendor execution is proven by the target-correlated presentation path rather
than by manufacturing an active reduced-resolution contract. DevBench retains
the actual DLSS or FSR backend, same-frame dispatch identity for both eyes, and
any FSR runtime-to-host fallback in that native-vendor presentation evidence.

`ApplyProfile` copies every input before returning. It performs validation and
preflight before admission. The queued main-thread transaction repeats the
revision comparison and admission checks immediately before immutable request
publication, closing the advisory-preflight race. It then
returns exactly one disposition:

-   rejected (with status and conditions);
-   no change;
-   applied synchronously; or
-   queued (with a non-zero operation ID).

The receipt also says whether rejection is retryable, whether the accepted
route requires a restart, and whether persistence will occur after stability.

The old ambiguous state—“unchanged, applied, or rejected”—is not permitted.
Queued acceptance means CSX owns the operation; it does not mean the physical
renderer contract is already active.

## Idempotency and persistence

`clientId` plus `commandId` is the mutation idempotency key. Both are required,
UTF-8, and limited to 128 bytes. The optional diagnostic reason is limited to
256 bytes. Inputs are copied during the call. Replaying an identical retained
command returns its original receipt and sets `idempotentReplay`. Reusing the
key with different arguments returns `kIdempotencyConflict`.

`kRuntimeOnly` changes in-memory/runtime state. `kPersistWhenStable` requests a
durable save only after physical convergence. A provider must reject that
policy unless it advertises persistent mutation and can make the save
transaction truthful. A failed or superseded operation must not persist its
target.

## Operations and events

Asynchronous work is observed with `GetOperation`. Terminal states are
completed, failed, and superseded. V1 does not expose cancellation: the current
renderer transition cannot promise safe cancellation once physical mutation
begins. A later additive interface may offer cancellation if the controller
gains that guarantee.

Operation flags distinguish persistence requested, physical state stable, and
durably persisted. Completion without the persisted flag is therefore not
misreported as a successful persistent transaction.

`ReadEvents` is a bounded, polling journal. It has no cross-DLL callbacks and no
global acknowledgement operation. Each client owns its cursor; one client
cannot trim events needed by another. The page reports cursor expiry and the
oldest retained event so a client can recover by fetching a fresh snapshot.
Event ordering is stable per operation through `eventIndex` and globally
through `eventId`.

## Threading and ownership

Every function-table entry is callable from any thread, retains no caller
pointer, and throws no exception across the DLL boundary. The adapter copies
inputs and schedules state collection and controller access on the game main
thread. Synchronous inspection/preflight calls have a bounded five-second wait;
failure to obtain the main thread is reported as `kServiceUnavailable`.
Mutation returns an operation receipt and performs renderer work asynchronously.
The ABI exposes no game objects, renderer pointers, STL containers, RTTI
objects, or internal loading-door tokens.

Returned structures are caller-sized. The provider checks `structSize` before
writing. Strings in requests are borrowed only for the duration of the call.
The interface and its context have process lifetime, matching registry rules.

## Live provider boundary

The implementation should be split into three layers:

1. **Domain controller** — the sole owner of admission, immutable transition
   requests, exact apply disposition, and physical convergence.
2. **Native adapter** — validates ABI structures, copies inputs, maps domain
   snapshots/results, publishes the monotonic API revision, maintains bounded
   command receipts, and exposes the bounded event cursor.
3. **DevBench adapter** — uses the same explicit controller apply result in
   JSON form and no longer infers acceptance by comparing pending request IDs.

The command journal retains at most 1,024 keys for one hour and atomically
reserves a key before preflight. An identical concurrent call receives
`kBusy` until the original receipt is ready; a different request using that
key receives `kIdempotencyConflict`. The event journal retains 4,096 events and
pages at most 500 at a time.

Persistence remains deliberately unavailable in ABI 1.0. The provider does
not advertise persistent mutation, does not claim a persisted profile, and
rejects `kPersistWhenStable` with the persistence-unavailable condition. That
capability can be added only when save-on-stable and durable-state tracking are
truthful.

Legacy CSAP methods can later delegate to the controller while retaining their
existing ABI and observable compatibility behaviour. They must not become the
implementation underneath the v1 adapter.

## Implemented integration sequence

1. The controller returns explicit rejected, no-change, synchronous, queued,
   deferred, or coalesced outcomes with its request and transition identifiers.
2. The adapter publishes one monotonic state revision whenever exposed domain
   profiles, transition state, active admitted operation, capability
   availability, or dimensions change. A command reservation which has not yet
   entered the renderer controller does not manufacture a state revision.
3. Command receipt retention and a bounded per-process event journal are live.
4. The native adapter registers `csx.upscaling` with truthful inspection and
   runtime-only mutation paths.
5. DevBench apply uses the exact controller receipt.
6. Legacy entry points retain their existing calls and observable compatibility
   behaviour while ignoring the additive result value.

When `DEVBENCH_BRIDGE` is enabled, `communityshaders.upscaling_api` is the
runtime conformance surface for this contract. It deliberately obtains
`Registry001` from the native provider and queries `csx.upscaling`; it does not
call the domain controller directly. The tool exposes registry inventory,
capabilities, snapshots, preflight, apply, operation lookup, and bounded event
pages. Every response carries the loaded producer identity. `apply` additionally
requires an exact `expectedBuildId` plus `clientId` and `commandId`, so automated
mutation fails closed against an unintended DLL and remains replay-safe.

The older `communityshaders.renderscale` DevBench tool remains available for
controller diagnostics and compatibility. It is not evidence that the public
registry/function-table ABI is working; conformance runs must use the public-ABI
tool and may correlate its results with the older diagnostic surface.

Persistent mutation remains the explicit next capability boundary, not an
implicit part of runtime apply.

Frame generation, Reflex, foveated dispatch, and sharpening remain outside the
v1 transition profile. They have different safety, lifecycle, and persistence
rules and should be added as explicit, separately preflighted surfaces in a
later minor revision rather than hidden inside the renderer-relatch transaction.
