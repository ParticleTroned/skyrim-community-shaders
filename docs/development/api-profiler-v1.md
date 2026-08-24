# Profiler API v1

The profiler API is a versioned service beside the existing profiling UI and
legacy `communityshaders.profiler` DevBench tool. Those existing interfaces are
preserved. Native consumers discover `include/VRAPI/CSprofilerapi.h` through
the CSXR service registry; DevBench exposes the parallel contract as
`communityshaders.profiler_api`.

## Contract

- Service: `csx.profiler`; contract `1.0`; schema revision `1`.
- Native calls are main-thread-affine. Timer names are borrowed until the next
  profiler update and must be copied by callers.
- The process-lifetime interface reports runtime unavailability rather than
  unregistering itself after a renderer/device transition.
- Major versions are ABI-breaking. Additive future native shapes use a new
  numbered structure or interface rather than extending v1 structures in place.
- DevBench uses the common versioned envelope, build pinning, idempotent
  `clientId`/`commandId`, and ordered event journal.

## Observation

`snapshot` is non-mutating. Unlike the legacy status action, it does not request
a new capture. It reports profiler availability, user-enabled and currently
capturing state, live and last-resolved totals, query-slot pressure, history
capacity, frame latency, capabilities, and exact build identity.

`timers` returns stable indices for the current catalog plus CPU and GPU
presence, current activity, current/average/p95/p99 values, top-level GPU
contribution, and history counts. A prefix can select one feature family.
`history` pages oldest-to-newest samples for one timer index and timing domain.
Indices are snapshot-local: clients must refresh the catalog after clearing
history, device reinitialization, or feature changes.

## Bounded captures

A bounded capture contains 1 to 300 submitted profiler frames, matching the
history capacity. Only one bounded session can run at a time. The profiler must
already be enabled; this makes ownership explicit and prevents a measurement
request from silently changing the user's profiling preference.

Each session reports:

- `captureId`: process-local monotonic identity;
- `requestedFrames`: the contract target;
- `submittedFrames`: frames for which CSX closed a profiler query set;
- `resolvedFrames`: submitted query sets whose GPU/CPU data have resolved;
- terminal state: `completed` or `cancelled`.

Completion is based on resolved query sets, not sleeps, wall-clock guesses, or
poll count. CSX continues requesting profiler work until the requested number
of frames has actually been submitted. Frames with no instrumented pass do not
consume the budget. `clearHistory` is optional; when selected it clears pending
and retained timer data before the new session begins.

Each bounded session also owns a separate timer catalog and CPU/GPU histories.
Pass `captureId` to `timers` and `history` to read that exact result set. It is
updated only by ring slots tagged with the session ID, then remains frozen after
completion. Live profiling requested by the menu or legacy tool cannot add
samples to it. Starting a newer bounded session releases the prior session's
result set; preserve completed responses before doing so.

Disabling the profiler cancels an active bounded session. Cancelling a running
session stops scheduling additional frames; already-submitted D3D queries may
still resolve internally but cannot change the cancelled terminal state. A
cancel request for an already-terminal session preserves that terminal state.
Starting a new session after cancellation is safe because each submitted ring
slot carries its owning session ID.

## DevBench actions

| Action | Effect |
| --- | --- |
| `registry` | Contract, capabilities, limits, and action discovery |
| `snapshot` | Non-mutating current state |
| `timers` | Timer catalog, optionally filtered by `prefix` |
| `history` | Paged GPU or CPU history for a timer index |
| `set_enabled` | Explicit runtime profiler enable/disable |
| `clear_history` | Clear timers when no bounded session is running |
| `start_capture` | Start a bounded session; optional history reset |
| `capture_status` | Poll exact submitted/resolved progress |
| `cancel_capture` | Idempotently reach a terminal cancelled state |
| `events` | Poll ordered capture lifecycle events |
| `acknowledge_events` | Release journal entries through a cursor |

Example:

```json
{
  "contractMajor": 1,
  "clientId": "shader-lab",
  "commandId": "capture-quality-001",
  "expectedBuildId": "<exact loaded build id>",
  "action": "start_capture",
  "frameCount": 120,
  "clearHistory": true
}
```

Poll `capture_status` with a fresh command ID until `state` is `completed`, then
read `timers` and the required histories with that `captureId`. Preserve the full response envelopes
with test conditions; a timer average alone is not sufficient provenance.
