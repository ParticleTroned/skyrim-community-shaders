# Profiler API v1

The profiler API is a versioned service beside the existing profiling UI and
legacy `communityshaders.profiler` DevBench tool. Those existing interfaces are
preserved. Native consumers discover `include/VRAPI/CSprofilerapi.h` through
the CSXR service registry; DevBench exposes the parallel contract as
`communityshaders.profiler_api`.

## Contract

-   Service: `csx.profiler`; paired contract `1.1`, schema revision `2`;
    additive independent CPU contract `1.2`, schema revision `3`.
-   Native calls are main-thread-affine. Timer names are borrowed until the next
    profiler update and must be copied by callers.
-   The process-lifetime interface reports runtime unavailability rather than
    unregistering itself after a renderer/device transition.
-   Major versions are ABI-breaking. Additive future native shapes use a new
    numbered structure or interface rather than extending v1 structures in place.
-   DevBench uses the common versioned envelope, build pinning, idempotent
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

The `kCapabilitySelfTime` capability identifies GPU and CPU timer values,
statistics, and histories as self time: profiled descendants are excluded
before repeated names are combined. CPU accounting includes nesting between
CPU-only and GPU-backed scopes. Version 1.0 / schema revision 1 reported
inclusive timer values. Native layouts and signatures remain unchanged.

GPU `topLevelMs` and resolved GPU totals retain inclusive depth-zero scope
time. CPU totals sum self times across both scope types. Invalid GPU queries
do not invalidate CPU samples, and valid GPU descendants skip invalid
ancestors when resolving self time. No result is published while a required
GPU query is pending in the paired view. The independent CPU view described
below publishes without waiting for those queries.

Detailed UI rows and combined totals use self time. Per-feature summaries
retain inclusive outermost scope costs, so nested feature costs can overlap
and must not be added together. Comparisons with captures from schema 1
must account for this change in timer semantics.

## Migration from 1.0 to 1.1

Native clients must accept minor version 1 in their service discovery query
(`maximumMinor >= 1`). A client capped at minor version 0 cannot discover
the 1.1 service. Require `minimumMinor = 1` or `kCapabilitySelfTime` when
calculations depend on additive self-time samples; clients supporting both
versions should inspect the returned version and capabilities.

DevBench callers keep `contractMajor: 1`. Responses advertise minor version
1 and schema revision 2, and the registry reports
`timingSemantics: gpu_cpu_self_time`. The legacy profiler status reports the
same timing-semantics marker. Update analysis that assumed inclusive timer
values, and label historical comparisons with the producing version.

## Independent CPU view (1.2)

Native discovery retains the exact 1.1 registration for clients capped at
minor 1. Query minor 2 for `Interface002`, whose first member is the complete
unchanged `Interface001` ABI. Check the returned size before reading the new
function pointers. Its `paired` methods keep the existing paired publication
and bounded-capture semantics. The extension advertises
`kCapabilityIndependentCpu`; it does not alter `Snapshot001` or timer layouts.

`RequestCapture` requests CPU, GPU, or both for one future frame. Requests
combine, and bounded captures continue to require both sources. CPU-only
requests issue no GPU timestamps unless another consumer also requests GPU
capture. GPU-only requests do not read the CPU clock. Closing a scope still
balances the acquisition it opened when profiling is disabled mid-frame.

`GetCpuSnapshot`, `GetCpuTimerCount`, `GetCpuTimerDescriptor`, and
`GetCpuHistorySample` expose a separate CPU catalog. These read-only calls do
not request capture. CPU samples publish at their engine-frame boundary,
including when the GPU query ring is pending or GPU interval capacity is
exhausted. Missing frame or timestamp query objects also use CPU fallback
when CPU capture is requested; GPU-only capture refuses the affected scope.
No GPU sample is generated for that scope. Device reinitialization recreates
the query resources. GPU-backed CPU samples and CPU-only scopes retain CSX self-time
and feature-root accounting. Same-name scopes contribute one summed history
sample per CPU capture cycle; absent known timers receive zero. GPU-only and
idle cycles do not advance this CPU history.

The CPU snapshot's `capturedFrameCount`, `publicationCount`, and
`resolvedTotalMs` describe that CPU publication only. A zero publication count
means no CPU frame has been published since initialization or history reset.
The last publication remains readable while idle; `capturing` identifies
current acquisition separately. Do not pair these CPU values with the older
GPU/paired frame stamp. CPU catalog indices are separate from paired and
bounded indices, and names remain borrowed until the next profiler update.

GPU interval exhaustion increments the existing GPU refusal counter while
still attempting a CPU fallback. CPU-only/fallback scopes retain a separate
128-interval-per-frame limit; GPU-backed CPU scopes can contribute another 128. CPU refusal counts are cumulative until device initialization. Query
resolution never moves unpaired CPU samples into a different engine frame.
Pending GPU slots continue draining after a mode switch or disable, including
captures shorter than the three-slot ring.

DevBench retains `contractMajor: 1` and advertises minor 2/schema 3. New
actions are `request_capture` (optional `mode`: `cpu`, `gpu`, or `both`),
`cpu_snapshot`, `cpu_timers`, and `cpu_history`. CPU timer/history responses
include their CPU snapshot in the same main-thread operation. CPU views reject
`captureId`; use the existing bounded actions for paired session results.
Use fresh command IDs to request subsequent frames. Inspection alone does not
extend acquisition, and other active profiling consumers may broaden the
requested sources.

The existing UI and legacy profiler tool continue using the paired view.
This change does not import upstream UI or engine-pass instrumentation.

## Bounded captures

A bounded capture contains 1 to 300 submitted profiler frames, matching the
history capacity. Only one bounded session can run at a time. The profiler must
already be enabled; this makes ownership explicit and prevents a measurement
request from silently changing the user's profiling preference.

Each session reports:

-   `captureId`: process-local monotonic identity;
-   `requestedFrames`: the contract target;
-   `submittedFrames`: frames for which CSX closed a profiler query set;
-   `resolvedFrames`: submitted query sets whose GPU/CPU data have resolved;
-   terminal state: `completed` or `cancelled`.

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

The API rejects history reset while a bounded session is running. A direct
internal reset cancels the session before discarding its pending frames;
open scopes can still close without republishing cleared timer names.

## DevBench actions

| Action               | Effect                                                  |
| -------------------- | ------------------------------------------------------- |
| `registry`           | Contract, capabilities, limits, and action discovery    |
| `snapshot`           | Non-mutating current state                              |
| `request_capture`    | Request CPU, GPU, or both for one future frame          |
| `cpu_snapshot`       | Non-mutating independent CPU state and frame provenance |
| `cpu_timers`         | Independent CPU catalog with its current snapshot       |
| `cpu_history`        | Independent CPU history with its current snapshot       |
| `timers`             | Timer catalog, optionally filtered by `prefix`          |
| `history`            | Paged GPU or CPU history for a timer index              |
| `set_enabled`        | Explicit runtime profiler enable/disable                |
| `clear_history`      | Clear timers when no bounded session is running         |
| `start_capture`      | Start a bounded session; optional history reset         |
| `capture_status`     | Poll exact submitted/resolved progress                  |
| `cancel_capture`     | Idempotently reach a terminal cancelled state           |
| `events`             | Poll ordered capture lifecycle events                   |
| `acknowledge_events` | Release journal entries through a cursor                |

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

## Validation coverage

`ProfilerCapture` compiles the production profiler with deterministic clock
and D3D query inputs. It covers pending queries, CPU-only and GPU-only modes,
combined requests, nested accounting, capacity fallback, failed frame and
timestamp query allocation, device recovery, partial ring draining,
bounded ownership, feature removal, and reinitialization. `ProfilerTiming`
retains the real D3D11 WARP self-time and bounded-capture test.
`ApiProfilerContract` checks the legacy ABI prefix and the new structure.

The initial API additions were prepared under a no-build instruction.
On 2026-09-19, the query-allocation safeguard backport passed both
`ProfilerCapture` and `ProfilerTiming`, compiling the actual main-VR
`src/Profiler.cpp` in an isolated MSVC Release harness. The allocation
matrix covers 15 combinations: CPU/GPU/Both capture with a missing frame
query, either first-scope timestamp, or either nested-scope timestamp.
It checks immediate CPU publication, nesting, absence of fabricated GPU
samples, and successful query recreation. Before the guards, the new test
failed with `Begin received a missing query`.

Local evidence is in `build/profiler-query-validation-20260919/`, including
`before-guards.xml`, `after-guards.xml`, and the standalone CMake harness.
This focused validation includes real D3D11 WARP timing; it does not
establish in-game behaviour or constitute a new full-DLL/shader build.
