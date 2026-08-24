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

- capture-local observation ID;
- retained COM pointer evidence and pointer generation;
- kind: `immediate` or `deferred`;
- D3D device observation when available;
- creation evidence: initial immediate context, observed creation hook, or
  first-seen fallback;
- coverage state for each installed state/execution hook family.

The event envelope's `deviceContextObservationId` becomes non-null only after
this observation exists. A raw context pointer in a payload is evidence, not a
substitute for the typed ID.

### Recording epoch

A deferred context has one active `command-recording` epoch. The epoch starts
at deferred-context creation, after a successful `FinishCommandList`, or at a
first-seen fallback when capture starts in the middle of recording. It owns:

- context observation ID;
- monotonically increasing context-local epoch number;
- capture-local recording observation ID;
- inherited/initial pipeline-state status;
- first and last recorded event sequence;
- partial-at-capture-start and coverage-gap flags.

Deferred draws and dispatches use `observationDomain: command-recording` and a
`commandRecordingObservationId`. They are not emitted as immediate execution.

### Command list

A successful `FinishCommandList` creates a `command-list` observation with:

- command-list COM pointer evidence and pointer generation;
- source deferred-context and recording-epoch IDs;
- `RestoreDeferredContextState` value;
- completeness of the source recording;
- zero or more later execution event references.

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

When `RestoreContextState` is true, the immediate-context pipeline tracker is
restored to its pre-call state. When false, D3D11 returns the immediate context
to default state; the tracker must invalidate/reset its bindings rather than
retain stale stage objects. The corresponding
`RestoreDeferredContextState` rule is applied to the next recording epoch.

## Capture boundaries

- Starting during an existing recording creates a partial recording epoch.
- Stopping before `FinishCommandList` leaves that epoch incomplete.
- A command list first seen at execution receives a minimal identity with
  unknown source recording; no source context is invented.
- A command list created before capture may execute during capture and remains
  valid evidence with an explicit missing-recording gap.
- Repeated execution reuses the same command-list observation ID within the
  capture.
- Pointer reuse requires a new pointer generation after an observed release or
  an incompatible semantic identity.

Absence of deferred events is proof of absence only when the manifest declares
complete deferred-context hook coverage for the relevant context and recording
epoch.

## Hook ownership

CSX currently has overlapping D3D11 observation interests:

- underwater composition owns immediate `Draw` and `DrawIndexed` behaviour;
- VR menu diagnostics can install broad context hook banks, including deferred
  contexts and command lists;
- render-map capture now owns immediate stage and execution observations.

The implementation must introduce one composable D3D hook owner or an explicit
observer fan-out at each already-owned thunk. It must not independently attach
another opaque detour to the same target and assume ordering. Behaviour-changing
owners run first where required; evidence observers receive the effective call
and cannot suppress or mutate it.

The hook registry records target address, vtable slot, owner, context coverage,
and installation result. A failed or exhausted hook bank is surfaced as capture
incompleteness.

## Bounded storage

Context, recording, and command-list observations have independent configured
bounds. Overflow increments distinct counters and marks durable artifacts
incomplete. Hot hooks retain compact IDs and numeric arguments; serialization,
pointer formatting, and graph joins remain off the render thread.

The maximum number of recording events remains constrained by the common event
and byte budgets. No command-list map may grow independently of its declared
observation bound.

## Implementation order

1. Add capture-local typed context, recording, and command-list catalogues.
2. Add schemas and serializer tests without enabling deferred capture.
3. Consolidate/fan out existing D3D hook ownership and publish exact coverage.
4. Observe deferred-context creation and start recording epochs.
5. Track deferred pipeline bindings and emit recording-domain draw/dispatch.
6. Observe `FinishCommandList` and materialize command-list identity.
7. Observe immediate `ExecuteCommandList`, state restoration, and repeated use.
8. Live-validate against Unified Water's existing deferred flowmap path.

Only after steps 1 through 7 pass unit and bounded live tests may the service
advertise `deferredContexts: true` and `commandLists: true`.
