# Device-context and command-list identity slice

## Purpose

This slice extends the immediate-context execution evidence without treating a
deferred-context call as GPU submission. It defines the identities and state
transitions required to correlate command recording, command-list
materialization, and later execution.

The implementation must preserve three distinct facts:

1. a D3D call was recorded into a deferred context;
2. a successful `FinishCommandList` materialized that recording as a command
   list;
3. an immediate context later executed that command list.

No timestamp or thread order between the first two facts proves when the GPU
executed the work.

## Typed identities

### Device context

A `device-context` observation contains:

-   capture-local observation ID;
-   retained COM pointer evidence and pointer generation;
-   kind: `immediate` or `deferred`;
-   D3D device observation when available;
-   creation evidence: initial immediate context, observed creation hook, or
    first-seen fallback;
-   coverage state for each installed state/execution hook family.

The event envelope's `deviceContextObservationId` becomes non-null only after
this observation exists. A raw context pointer in a payload is evidence, not a
substitute for the typed ID.

The immediate and deferred identity paths are implemented. First observed
activity declares a capture-local context identity, and draw/dispatch events
carry both that identity and a monotonically increasing context-local command
sequence. Observed VS/PS/CS binds advance the same sequence without generating a
separate event. A deferred context missed by the creation hook is admitted only
after `GetType` proves it is deferred and is labelled `first-seen`.

### Recording epoch

A deferred context has one active `command-recording` epoch. The epoch starts
at deferred-context creation, after a successful `FinishCommandList`, or at a
first-seen fallback when capture starts in the middle of recording. It owns:

-   context observation ID;
-   monotonically increasing context-local epoch number;
-   capture-local recording observation ID;
-   inherited/initial pipeline-state status;
-   first and last recorded event sequence;
-   partial-at-capture-start and coverage-gap flags.

Deferred draws and dispatches use `observationDomain: command-recording` and a
`commandRecordingObservationId`. They are not emitted as immediate execution.
Selecting either event kind automatically selects its recording and context
declarations. If a recording declaration cannot be retained, the command is
omitted rather than being mislabelled as an immediate CPU call.

`sourceRecordingComplete` is conservative. It remains false while deferred
hook coverage is unqualified, when capture begins part-way through an epoch, or
when any observed command event cannot be retained. The accompanying
`sourceRecordingIncompleteReasons` array makes those cases distinguishable.

### Command list

A successful `FinishCommandList` creates a `command-list` observation with:

-   command-list COM pointer evidence and pointer generation;
-   source deferred-context and recording-epoch IDs;
-   `RestoreDeferredContextState` value;
-   completeness of the source recording;
-   zero or more later execution event references.

Failure does not create a command-list observation. It closes the attempted
epoch with the HRESULT and starts the next epoch according to observed D3D11
state semantics.

## State transitions

```text
deferred context created/first seen
        |
        v
recording epoch active -- Draw/Dispatch --> recording events
        |
        +-- FinishCommandList failure --> failed epoch --> next epoch
        |
        +-- FinishCommandList success --> command-list observation --> next epoch
                                                        |
                                                        v
immediate ExecuteCommandList --------------------> execution event
```

`ExecuteCommandList` records the immediate context, command-list identity,
`RestoreContextState`, and a command-stream sequence. The command-list contents
remain recording-domain events linked through the command-list observation;
they are not duplicated as a guessed series of immediate calls.

The derived graph retains recorded draw/dispatch nodes, exact recording edges,
and observed stage-shader references. It does not apply global
immediate-context SRV, UAV, target-binding, resource-version, or hazard state to
those nodes. Until command-list-local state capture exists, it emits an explicit
deferred-resource-provenance gap instead.

When `RestoreContextState` is true, the immediate-context pipeline tracker is
restored to its pre-call state. When false, D3D11 returns the immediate context
to default state; the tracker must invalidate/reset its bindings rather than
retain stale stage objects. The corresponding
`RestoreDeferredContextState` rule is applied to the next recording epoch.

## Capture boundaries

-   Starting during an existing recording creates a partial recording epoch.
-   Stopping before `FinishCommandList` leaves that epoch incomplete.
-   A command list first seen at execution receives a minimal identity with
    unknown source recording; no source context is invented.
-   A command list created before capture may execute during capture and remains
    valid evidence with an explicit missing-recording gap.
-   Repeated execution reuses the same command-list observation ID within the
    capture.
-   Pointer reuse requires a new pointer generation after an observed release or
    an incompatible semantic identity.

Absence of deferred events is proof of absence only when the manifest declares
complete deferred-context hook coverage for the relevant context and recording
epoch.

## Hook ownership

CSX currently has overlapping D3D11 observation interests:

-   underwater composition owns immediate `Draw` and `DrawIndexed` behaviour;
-   VR menu diagnostics can install broad context hook banks, including deferred
    contexts and command lists;
-   render-map capture now owns immediate stage and execution observations.

The implementation must introduce one composable D3D hook owner or an explicit
observer fan-out at each already-owned thunk. It must not independently attach
another opaque detour to the same target and assume ordering. Behaviour-changing
owners run first where required; evidence observers receive the effective call
and cannot suppress or mutate it.

The hook registry records target address, vtable slot, owner, context coverage,
and installation result. A failed or exhausted hook bank is surfaced as capture
incompleteness.

## Bounded storage

The process catalogue is hard-bounded to 256 deferred contexts and each capture
is hard-bounded to 8,192 command lists. Command-list state is cleared at capture
start; a pre-existing list is deliberately re-admitted as first-seen execution
evidence. The common event/byte budget bounds recording observations and events.
Hot hooks retain compact IDs and numeric arguments; serialization, pointer
formatting, and graph joins remain off the render thread.

The service capability remains false until live qualification, so exhaustion of
these provisional hard bounds is currently represented by missing typed
evidence rather than a dedicated per-catalogue drop counter. Configurable bounds
and explicit overflow counters are required before the capability is promoted.

## Implementation order

1. **Implemented:** capture-local typed context, recording, and command-list
   identities.
2. **Implemented:** schemas, serialization, graph projection, and unit tests.
3. **Partially implemented:** one immediate-context hook bank observes the
   implementation targets for stage binds, draw/dispatch, finish, and execute.
   Exact deferred-vtable target coverage still requires live proof.
4. **Implemented:** deferred-context creation and first-seen admission start
   recording epochs.
5. **Partially implemented:** VS/PS/CS state and draw/dispatch are recorded in
   the command-recording domain. Deferred resource, target, UAV, copy, and CPU
   access state is not yet promoted into the recording identity.
6. **Implemented:** successful `FinishCommandList` materializes a typed list;
   failed finish remains evidence without inventing a list.
7. **Implemented:** immediate `ExecuteCommandList`, reset-on-no-restore, and
   repeated list use.
8. **Next MO2 requirement:** live-validate the hook route and observed identities
   against Unified Water's loaded-scene deferred flowmap path.

Only after steps 1 through 7 pass unit and bounded live tests may the service
advertise `deferredContexts: true` and `commandLists: true`.

## Static qualification and next live gate

The implementation passes the focused runtime, collector, graph-builder, and
schema-contract tests and a release plugin build. The registry intentionally
continues to report `deferredContexts: false` and `commandLists: false`.

The next qualification needs an MO2 lease and a loaded scene with Unified Water
active. A bounded capture must prove that the deferred context's vtable routes
the installed VS/PS/CS, draw/dispatch, and slot-114 `FinishCommandList` targets
through the observer, then prove that slot-58 `ExecuteCommandList` refers to the
same materialized list on the immediate context. If the deferred implementation
targets differ from the immediate-context targets, the next source change is a
separate bounded deferred-context hook bank registered at
`CreateDeferredContext`; absence of events must not be interpreted as absence of
deferred work.

## Live validation: 2026-08-26 main menu

The immediate-context portion passed a four-frame Valve null-HMD capture against
the integrated PR1-3 build. Capture
`capture-live-00012b76f46a641c-1` completed on its frame bound without
truncation or dropped events. Its durable evidence is retained at:

`L:/Codex/evidence/render-map-live-capture/20260826T003444Z-device-context-command-stream/capture/capture-live-00012b76f46a641c-1`

Observed results:

-   537 events, including 135 draws and one immediate-context declaration;
-   context-local command sequences covered 7 through 411;
-   every draw carried a typed context ID and command-stream sequence;
-   no draw preceded its context declaration;
-   command-stream sequences were strictly increasing for the context;
-   all typed observation references resolved to an earlier declaration;
-   no event, shader-observation, or stage-observation drops were reported.

The capture contained no dispatch or deferred-context work. It therefore
validates immediate draw ordering only; it does not change the advertised
`deferredContexts: false` or `commandLists: false` coverage. It also confirms
the next high-value gap: render-target/depth-target identity and VR eye
attribution are still absent from otherwise well-joined draw evidence.
