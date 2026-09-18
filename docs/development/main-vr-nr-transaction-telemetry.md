# Neural Rendering transaction telemetry

## Existing local runtime evidence

The bounded local inventory on 18 September 2026 found the newest complete
local NR measurement under:

`C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/roi-performance-dd47c7c61-20260915T212211Z`

That run was prepared at 21:22 UTC on 15 September, measured approximately
21:26–21:32 UTC, and finalized around 21:42 UTC. It retains `REPORT.md`,
`comparison.json`, `comparison.csv`, native analysis, individual API
responses, stereo images, activity recording, and the cached public schema.
Its producer identity is:

| Field           | Recorded value                                                     |
| --------------- | ------------------------------------------------------------------ |
| Compiled source | `dd47c7c61b80dbbc5fd9cc7a709dbb9f4d200860`                         |
| Build ID        | `2b25e76663aa4b0b6796ede73943bd5e6cf0ed8bae46307faa2e3b46ad8d974e` |
| DLL SHA-256     | `90435b99b55711f8b88d3ac6b8288ff2e316c567d6338ad93f5917e33307f945` |
| DLL size        | 24,804,864 bytes                                                   |
| HMD             | Valve null HMD, 1512 × 1680 pixels per eye                         |

The retained `deployment-identity.json` identifies the enabled physical AIO
provider and verifies the runtime producer. The matching staging manifest is
`C:/src/skyrim-community-shaders/build/nrc/aio-dd47c7c61-20260915T210422Z/SKSE/Plugins/CSX.BuildManifest.json`.
The build receipt is
`C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/roi-savings-gate/aio-build-receipt.json`.
These are historical producer checks; this inventory did not inspect or
change a live game or deployment.

The eight cases cover NR off, full-screen NR, and face/all-character NR with
single/multiple regions and the savings gate enabled/disabled. They use the
final-LDR insertion path, batched/direct dispatch, and FOV scale 0.95. Each
case has 40 observations, with 24–25 unique GPU samples in enabled cases.
This is evidence for the older source implementation, not runtime
qualification of all integrated full-resolution, foveated, and
reduced-resolution arrangements or their current character combinations.

### Missing transaction-level information

-   The saved Feature 18 GPU timer is aggregate. It does not attribute GPU
    time to each physical character-region dispatch. Category capture, mask,
    ROI preparation, and composite profiler timers are recorded as
    `timer_not_observed`; the ordinary profiler was disabled. CPU enqueue
    totals/last/max are not GPU timings or complete frame times.
-   Saved resource summaries include dimensions, selected DXGI formats, and
    evaluation subrects. They do not preserve all texture/view descriptors
    and stage timings together for each physical dispatch. One retained
    evaluation has color/guide/output extent 1512 × 1680 and subrect
    `(704, 768, 192, 256)`; color/output format is 26, depth 41, motion 34,
    and the control mask is absent.
-   A prepared snapshot at frame 78216 contains left regions
    `(1024, 768, 256, 256)` and `(704, 768, 256, 256)`, and right regions
    `(960, 768, 320, 256)` and `(704, 768, 192, 256)`. Aggregate evidence shows
    four evaluations, 262,144 pixels, and physical-slot mask 51. It cannot
    assign the aggregate GPU duration to those four regions individually.
-   Early GPU bounds reported 1,226 pending polls and zero successful uses;
    region preparation used the geometry fallback. Disabled mask-coverage
    diagnostics have null results, not measured zero coverage.

An earlier profiler attempt at approximately 18:56 UTC failed its temporal
state guard and restored the previous disabled preference. Its receipt is
`C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/roi-performance-20260915T185130Z/profiler/profiler-full-screen-r1-0359620444ca4eb393375d6c31b6a1cf/capture.receipt.json`.
The durable journal is
`C:/Users/quartus/AppData/Local/CSX-VR-Automation/profiler-captures/f90d5bfa71cf6feb3dfcf91ec897b832/transaction.journal.json`.
That failed capture supplies no completed profiler baseline.

Reports dated 17 September describe bright/dark color experiments on
another machine and refer to unavailable local `D:/` evidence. They do not
replace the retained local measurement above. The integrated branch's
subsequent successful builds and unit/WARP tests are implementation
validation, not additional Skyrim runtime measurements.

## Backend execution and timing contract

`ExecutionEvidence` extends the existing capture record. After validation
and physical ROI expansion, its immutable descriptor freezes the route,
insertion, frame/source/generation, configuration and capture identities,
mode/FOV context, actual grids, crops, formats, motion-vector scales and
region identities. Each physical region retains its own caller context
and character-preparation evidence. Unprovided caller facts remain
unavailable. Reduced-resolution mode is explicit even when its insertion
enum is the historical `UpscaledCenter` value.

The rendered colour configuration still latches by route, frame, source
world frame, generation and insertion. Capture transaction ID and epoch
reset only observational state; changing capture settings within the same
rendered transaction cannot split eye or region colour settings. Capture
inputs retain those identities and allocation-failure counts even when no
execution handle could be allocated. Process-lifetime submission IDs do
not restart when the backend is recreated.

The planned physical-region count and slot mask describe validated work,
not allocated slot capacity. Attempted and successful evaluations are
recorded separately. Private-output commit is recorded only after every
private output copy and the final device check succeed; it does not imply
that the caller published a final stereo frame. The shared capture record
pins the execution handle, and each command context carries that exact
handle until delayed completion. Results never join the latest frame by
frame number alone.

Per-region D3D12 timestamps bracket the actual `evaluateFeature` call.
They exclude feature creation, D3D11 preparation and final composition.
The existing whole-batch timestamps, counters and profiler scopes remain;
region query resolves follow the legacy batch end timestamp. Three command
contexts reserve ten queries each: two batch queries and four region pairs.
That is 240 logical readback bytes versus the previous 48, plus query-heap
metadata, including when capture is off. Optional region queries are
issued only when execution evidence is armed. D3D11 preparation,
reconstruction and copy stages use retained profiler handles; their GPU
results require the existing profiler capture to be active. Durations
from unrelated queues are not added into an inferred total.

Feature creation and evaluation have separate measured CPU durations and
success/result fields. Creation evidence distinguishes a missing feature
from rejection of an incompatible live configuration. Resource rebuild
flags distinguish initial allocation from changed resource contracts.
Reset flags report caller, invalid-history, changed-history-key,
evaluation/source discontinuity, synchronized and cluster-peer causes;
telemetry does not change those decisions.

`resetReasonFlags` uses bits 0–6 for caller reset, invalid history,
history-key change, evaluation discontinuity, source discontinuity,
synchronized reset and cluster-peer reset respectively.
`rebuildReasonFlags` uses bit 0 for unallocated resources and bit 1 for a
changed resource contract. Zero means no recorded cause at that stage;
`resourcesReady` and the actual evaluation flags show whether it was reached.

CPU preparation, private commit, resource retirement and command-begin
durations are measured per transaction. Actual existing fence waits also
record operation, elapsed microseconds, result, error and timeout. Sixteen
samples are retained, with an explicit dropped count and complete totals.
An observed wait stage starts with zero calls and elapsed time; an
unobserved stage remains unavailable. A completed sample may validly
measure zero microseconds. No extra fence waits, GPU flushes or blocking
readbacks are introduced to finish telemetry.

Logical byte evidence distinguishes retained capacity, newly allocated
texture payloads and copies actually enqueued. It uses DXGI pitch rules,
not guessed bytes per pixel. It excludes driver alignment/residency,
private model allocations, shader traffic, constant buffers and diagnostic
resources; an unsupported size calculation remains unknown. These values
are not total GPU memory use or measured memory bandwidth.

Timing states distinguish not requested, pending, complete, failed and
unavailable; absent or failed measurements do not become numeric zero.
Adversarial corrections preserve complete sibling timings while failing
missing-end pairs when their completed command context is retired, keep
capture restarts separate from colour latching, and contain optional
evidence allocation/read failures with explicit failure evidence. Aborted
recording, command-close/queue-signal failures and backend abandonment fail
pending samples.

### Focused backend validation

The following commands passed from the branch worktree:

```powershell
& ./tools/cmake.ps1 --build build/ALL --config Release --target neural_execution_evidence_test neural_color_route_latch_test
ctest --test-dir build/ALL -C Release -R '^(NeuralExecutionEvidence|NeuralColorRouteLatch)$' --output-on-failure
& ./tools/cmake.ps1 --build build/nr-color-tests --config Release --target nr_color_settings_test
ctest --test-dir build/nr-color-tests -C Release -R '^NRColorSettings$' --output-on-failure
```

The final backend pair passed 2/2; settings passed 1/1. Coverage includes
timestamp bounds/conversion, delayed-handle retention, incomplete pairs,
wait causes and bounded overflow, capture epochs, and unchanged colour
latching across capture restarts. Scoped `git diff --check` also passed.
These CPU tests do not validate live D3D12 inference or Skyrim performance.

Further review covered existing device-change teardown waits, optional
evidence copies and exceptions after provider evaluation. Teardown,
shutdown and command-begin stages now retain the same wait accumulator;
failed shared-texture creation makes allocation bytes unknown. Optional
source contexts copy without allocation, and a scope guard retains actual
provider-call facts on unwind. Full-resolution preflight fallback publishes
a current no-attempt record. Delayed joins reject changed timing handles or
unmatched character/support producers, while allowing legitimate completion
and unchanged retained-mask reuse.

## Capture and interpretation

The existing DevBench `captureEvidence` control arms this evidence. No new
capture service, runtime controller or profiler preference is introduced.
Detailed D3D11 GPU and paired CPU pass samples require the existing profiler
to be enabled and capturing. D3D12 evaluation samples use the existing NR
command-context retirement path. An inactive profiler, unresolved query,
disabled mask coverage or missing producer is explicitly unavailable.

Every source record carries a process-lifetime source transaction ID,
publication sequence, capture epoch and configuration epoch. Each physical
evaluation has its own submission and region identity. The source record
freezes requested settings, colour/exposure configuration, source and
evaluation frames, per-eye caller crops/origins and engine jitter. Actual
DLSS dispatch receipts are separate from the caller's DLSS route contract;
they must match the frame, compositor cycle, capture epoch and route. C is
named `render_resolution_before_dlss`, irrespective of the legacy
`UpscaledCenter` insertion value. A can operate without a DLSS dispatch.

Character evidence preserves the authored semantic grid, exact capture
rectangles, source jitter, selected ROI plan, dirty mask rectangle and
dispatch counts. CPU detection and planning have separate scopes. Bounds
readiness and actual bounds use are separate facts. GPU mask support is
reported only from the existing coverage readback; diagnostics being off
does not imply an empty mask. Logical capacity bytes and observed copied
bytes remain distinct from evaluated pixels and dirty dispatch pixels.

The screenshot pipeline pins the exact transaction **and publication** at
source acquisition. Existing capture finalization takes one nonblocking
snapshot of its retained handles, then releases the lease. Later GPU
completion may still be unavailable in that finalized file; finalization
never waits for telemetry. The offline NR assessment validates identity,
immutable geometry, profiles, actual evaluation counts and stage
availability before joining that companion. A delayed telemetry failure
remains unavailable and cannot qualify a colour campaign.

`NoWork`, unavailable, failed evaluation, fallback publication and successful
publication remain distinct. A successful private copy does not prove
stereo publication. A bypassed eye does not claim that model output reached
the producer. The existing screenshot source and stereo rules remain in
force. Capture enable/disable does not alter colour transaction latching,
lighting defaults, saved settings or NR history decisions.

The generic profiler uses bounded optional detail queries and retained
handles, without adding diagnostic rows or subtracting these scopes from
legacy parent self time. Repeated references to one pass carry the same
`captureId`; consumers must not count them twice. CPU inclusive scopes can
overlap other CPU scopes. D3D11 and D3D12 clocks are distinct. Only complete,
nonoverlapping physical `evaluateFeature` intervals on the NR queue are
summed into aggregate evaluation GPU time. Existing whole-NR timers retain
their original boundaries and remain available for historical comparisons.

## Acceptance scope

The production serializers have fixture coverage across A/B/C, character
selection and FOV combinations, including zero, one and four actual
evaluations, pending-to-complete timestamps, partial failure and private
commit without visible publication. This establishes record structure and
join behavior. It does **not** establish live Feature 18 timings, image
quality or telemetry overhead across that matrix.

No replacement Skyrim measurement was launched, no live producer was
changed and nothing was deployed or pushed. The existing local runs above
cannot be retroactively upgraded into transaction-complete evidence.
