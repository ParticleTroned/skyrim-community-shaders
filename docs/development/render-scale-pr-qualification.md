# Render-scale PR qualification

Render-scale changes use revision 4 of the versioned
`csx-render-scale-pr-v1` DevBench protocol. The canonical runner lives in the
Skyrim VR automation repository at
`tools/render-scale-qualification/Start-CSXRenderScaleQualification.ps1`.
Copy the generated `pr-summary.md` into every PR that changes render-scale
behavior and retain the complete evidence directory with the candidate build.

The suite is deliberately bounded. Its one-shot package has 600 seconds for
preflight, all three assays, both 30-second recovery barriers, six unattended
image-model evaluations, and evidence finalization. Capture orchestration uses
at most 495 seconds, the image-model stage uses at most 90 seconds, and final
evidence work retains 15 seconds. It also reports a narrower performance
interval from the first COC dispatch through the third capture manifest. Game
and MO2 launch remain outside the package.

Configure the DevBench runtime and controlled fixture once. Then launch the
chosen DLL and game, reach the fixed exterior start scene, and invoke the
package. The wrapper discovers the exact running Build ID, artifact identity,
and GPU matrix instead of accepting a caller-supplied candidate identity:

```powershell
$env:CSX_DEVBENCH_RUNTIME_PATH = 'C:\Path\To\devbench\runtime.json'
$env:CSX_RENDER_SCALE_FIXTURE_PATH = 'C:\Evidence\render-scale-fixture.json'
pwsh .\tools\render-scale-qualification\Start-CSXRenderScaleQualification.ps1
```

The `render-scale-qualification` automation skill exposes the conversational
equivalent. After the game is ready, say `start render-scale qualification` or,
when that test is already the active topic, simply `start`. The agent invokes
the same package without building, deploying, relaunching, or requesting a
human review. Each run creates a new timestamped evidence directory by default.

PR mode also requires a previously accepted baseline evidence directory (or
its `run.json`) and its intended Build ID. Candidate and baseline Build IDs
must differ. A finalized `LOCAL_PASS` from this exact protocol and fixture may
bootstrap the first baseline for a new revision; it remains ineligible as the
candidate PR result:

```powershell
pwsh .\tools\render-scale-qualification\Start-CSXRenderScaleQualification.ps1 `
    -PrMode `
    -BaselinePath C:\Evidence\render-scale-baseline `
    -ExpectedBaselineBuildId '<64-character baseline CSX build ID>'
```

Final `PASS` or `LOCAL_PASS` returns 0, qualification `FAIL` returns 2,
and `INFRASTRUCTURE_ERROR` returns 4. There is no pending or manual-finalization
state. Infrastructure errors cover transport, exact
runtime/tool/capability/fixture/baseline binding, image-model availability or
schema failure, and evidence-finalization setup failures. A valid negative or
inconclusive visual-quality decision is a qualification `FAIL`.

## Fixed fixture

Run a candidate and its baseline with the same save, camera pose, HMD runtime,
refresh rate, eye resolution, GPU and driver, game runtime, Community Shaders
settings, VR FPS Stabilizer rules, and DevBench/automation versions. The runner
rejects a comparison when their fixture fingerprints differ.

The `csx-render-scale-fixture-v1` manifest records a fixture ID; save ID and
SHA-256; camera ID and configuration SHA-256; VR FPS Stabilizer version and
configuration SHA-256; GPU vendor, device ID, and driver; and HMD model,
runtime, runtime version, and refresh rate. GPU vendor, device ID, and driver
are checked against the live D3D adapter returned by render-scale status. The
save, camera, VR FPS Stabilizer, and HMD values are explicitly
operator-attested; their hashes prevent silent drift between paired runs but
the runner does not independently discover them. The runner adds the observed
eye dimensions, FSR runtime, service capabilities, protocol hash, and bound
runtime identity before hashing the effective fixture. Machine-specific paths
are not part of the manifest. Start from the distributed
`fixture.example.json`; the runner rejects its placeholders and all-zero
hashes, so it must be filled with the real controlled fixture before a run.
Copy `status.adapter.deviceId` and `status.adapter.driverVersion` exactly, and
complete the operator/UTC attestation block.

Start in `WindhelmExterior01`. The COC route alternates with
`WhiterunDragonsreach`. The VR FPS Stabilizer configuration must select these
exact profiles:

| Location | NVIDIA                                  | AMD                             |
| -------- | --------------------------------------- | ------------------------------- |
| Interior | DLSS Native AA (DLAA), render scale off | FSR Native AA, render scale off |
| Exterior | FSR Hoshipa, render scale on            | FSR Hoshipa, render scale on    |

The exterior profile follows the shared FSR configuration in the protocol; it
is not inferred from the installed GPU vendor. If a different NVIDIA exterior
profile is desired, create a new versioned fixture instead of silently changing
this one.

The setting called "FOV" in test notes means the foveated rendering contract,
not the camera field of view. It must remain enabled throughout all three
assays, with vendor dispatch enabled, center area `0.3`, periphery TAA enabled,
periphery TAA center area `0.3`, and periphery TAA outer scale `0.7`. Preflight
records the complete Upscaling settings object and the runner rejects any
mid-run drift.

## Assay 1: 20 immediate COC transitions

Run exactly 20 real transitions, beginning with Dragonsreach and alternating
back to Windhelm. The final transition therefore ends in the exterior.

Each transition is a fail-fast sequence of checked top-level MCP calls:

1. `communityshaders.renderscale qualification_begin` captures the server QPC,
   frame, source cell, profile, stress counters, and diagnostic baselines.
2. `communityshaders.renderscale qualification_dispatch` freezes the server
   QPC and frame used as the latency origin. Its accepted receipt is required
   before the mutation, so its bounded loopback acknowledgement is included in
   absolute latency.
3. DevBench executes the one `coc` command only after that receipt succeeds.
4. `communityshaders.renderscale qualification_wait` waits for the exact
   destination, requested/effective/stable profile agreement, complete physical
   resources, clean lifecycle, and a coherent two-eye presentation.

### Qualification milestones

`qualification_wait` accepts the optional `milestone` value `strict`,
`presentation`, or `cleanup`. Omitting it is exactly equivalent to selecting
`strict`; the revision-4 runner therefore retains its existing combined
qualification semantics. Strict success requires presentation stability and
drained cleanup at the same observation. Neither named milestone can turn a
strict failure into a protocol pass.

Presentation stability proves the exact destination and profile, current
render-target resource publication, provider generation and resources, valid
dimensions, settled mutation authority, device health, fidelity, and a
coherent current stereo submission. It remains blocked by an active API or
controller operation, relatch, recovery, unresolved physical mutation,
terminal provider/lifecycle failure, or shader compilation still required by
the active contract. A completed vendor stereo cycle remains coherent while
one live eye advances into the immediately following compositor cycle.

CSX evaluates resource publication against the physical main render target.
The diagnostic reads the current `kMAIN` texture descriptor directly and does
not reinterpret per-eye dimensions. Runners preserve the emitted expected and
published fields and do not calculate, override, or repair `dimensionsMatch`.

Cleanup drain proves that no active operation can create more cleanup debt and
that passive work has completed. Its outstanding-debt snapshot includes the
work gate, memory trim, intermediate and engine-target retirement, post-load
recovery, physical mutation ownership, and shader-compilation state. A retired
generation waiting on its safe tail or fence, a nonblocking trim, or historical
debt unrelated to the current proven generation does not by itself withhold
the presentation milestone.

The policy classifies the previous strict checks as follows:

| Classification                  | Checks                                                                                                                                                         |
| ------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Presentation mutation authority | API operation and blocking conditions, controller state, relatch, recovery, physical mutation, current resource publication, exact physical/provider ownership |
| Cleanup-only debt               | effective work gate, memory trim, intermediate retirement, engine-target retirement, and legacy global shader-compiler drainage                                |
| Measured verdict failure        | recovered fidelity/fallback diagnostics, runtime failures, and counter anomalies retained by the transition receipt                                            |
| Terminal error                  | build/session/owner failure at the request boundary, required telemetry ownership loss, device loss, and terminal controller/provider state                    |

Every receipt retains server-owned dispatch, first `presentationStable`, first
`cleanupDrained`, and first strict-completion QPC/frame timestamps. It returns
the three Boolean results, separate failure masks and decoded reasons, elapsed
milliseconds and frames, and the latest outstanding cleanup-debt snapshot.
First-observed timestamps remain immutable if a later observation changes.
Timeout receipts include `timedOutMilestone` and the current masks for all three
decisions.

The `milestoneTimings` object preserves those three observations separately,
reports the signed presentation-to-cleanup frame/time delta, and reports the
nonnegative cleanup tail. `sameObservation: true` therefore means a measured
zero tail, not missing data. A single strict waiter records all three values;
collectors must not add a second wait or another deadline.

The same terminal receipt preserves a compact `replacementTimeline` containing
dispatch, last pre-mutation, first blocked-admission, and first destructive-
mutation evidence. Each entry includes current-presentation proof and
generation, replacement admission state and decoded reasons, the first
physical-mutation marker, selected presentation disposition, device/resource
identity, and per-eye path/generation. Provider invalidation, dirtying, or
teardown starts physical mutation even when engine-target creation has not yet
begun.

### Replacement presentation state matrix

Replacement admission and current presentation have separate authority. The
following matrix defines their shared decision boundary; memory, shader,
provider-preparation, work-gate, retirement, and trim deferrals all use the
same pre-mutation row.

| Physical phase                                        | Current contract proof                                                                                                | Required disposition                                                                             |
| ----------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| No replacement                                        | Exact native or vendor contract                                                                                       | Present normally                                                                                 |
| Replacement queued or preparing; mutation not started | Exact current DLSS or FSR provider, publication, device, dimensions, options, backend, resources, and stereo identity | Evaluate the current provider                                                                    |
| Replacement queued or preparing; mutation not started | Strongly owned immutable completed output                                                                             | Reuse that output                                                                                |
| Replacement queued or preparing; mutation not started | Missing, dirty, stale, or wrong-generation current proof                                                              | Use presentation stretch or the established fail-safe path; never evaluate the unproven provider |
| Creator entered or physical mutation unresolved       | Any previous-generation proof                                                                                         | Forbid the previous generation and fail closed through recovery                                  |
| Replacement contract published                        | Exact new publication, provider generation, resources, device, path, and coherent stereo proof                        | Present the new generation                                                                       |
| Replacement contract published                        | Incomplete or mismatched new proof                                                                                    | Quarantine or fail closed until the exact new proof exists                                       |

Ordinary world and the CS in-scene menu use this matrix directly. LoadingMenu,
post-load cover, main-menu, RaceSex, and other presentation-owned contexts keep
their existing stretch, black-keepalive, quarantine, or fail-closed overrides.
A proven stabilizer door handoff may release the cover only before mutation;
after mutation it follows recovery. Cleanup-only debt never changes the
current-provider decision. A pending vendor reset owned by a different,
nonzero replacement generation does not by itself dirty the current provider;
an unowned reset or one owned by the current generation does.

The strict waiter stops on the first observation that satisfies both
milestones. A frame-level vendor or fidelity failure remains in the receipt and
fails the final assay verdict, but a later coherent stereo presentation may
satisfy the waiter without consuming the full deadline. Required telemetry
ownership loss, a transport failure, or a terminal runtime state stops before
the next mutation. There are no fixed sleeps, menu queries, client polling
loops, same-cell COCs, or overlapping transitions in the 20-transition
sequence. The per-transition ceiling is 120 seconds, further bounded by the
suite deadline. `timeoutMs` is a post-dispatch ceiling, not a minimum delay; a
satisfied observation returns immediately.

Native AA requires both eyes to present `NativeOriginal`, native dimensions,
and cleared render-scale/foveated vendor flags. An active Hoshipa profile
requires both eyes to present the exact current vendor evaluation, matching
method, generation, epoch, input extent, output extent, and stable physical
contract. Native completes on one coherent stereo cycle; active vendor
presentation requires two consecutive coherent stereo cycles. Intentional
`PresentationStretch` is measured separately and must
recover; vendor-failure stretch and bounds-mismatch fallback are failures.

The assay reports wall time and stable latency in milliseconds and frames. It
also reports count, total, minimum, median, arithmetic mean, sample standard
deviation, coefficient of variation, nearest-rank p95, maximum, and transitions
per minute, both overall and split by destination. Failure rates include a
Wilson 95-percent confidence interval. No samples are discarded as outliers.

Assay 1 passes only when all 20 transitions reach the exact destination and
profile, no transition overlaps or times out, settings do not drift, no device,
OOM, backend, terminal, lifecycle, fidelity, or fallback failure occurs, the
retirement/trim work drains, and both eyes recover. The default bound for an
allowed presentation-stretch episode is two frames. The report includes episode
count, completed and active frames, mean/max frames, and mean/max milliseconds;
an active episode or incomplete two-eye compositor cycle at assay end fails.

CPU telemetry ownership is session-based. Successful
`cpu_performance_start`, `cpu_performance_status`, and
`cpu_performance_stop` responses expose `cpuPerformance.sessionId`. It is
nonzero and monotonically increases for each successful start during the server
lifetime. A successful stop retains the same ID and counters with
`state: stopped`; reset is permitted only while inactive and clears the retained
ID and counters to `sessionId: 0` and `state: reset` without rewinding the ID
allocator. Normal and abort cleanup pass the exact retained ID as
`expectedSessionId`; a different active session fails closed and is not stopped.
`expectedStartFrame` remains a legacy optional secondary check and is not the
primary ownership identity.

While the stress session is active, render-scale status also exposes the
bounded `preparation` trace. Use its per-stage QPC timings to distinguish
shader bytecode compilation, D3D11 shader creation, provider compatibility
inspection, request-to-prepared latency, and prepared-to-creator latency. The
trace is diagnostic only and does not alter qualification or transition
behavior. Its ownership and operation classification are documented in
[VR render-scale preparation telemetry](vr-render-scale-preparation.md).

## Recovery barrier 1

After the COC capture is finalized, DevBench waits exactly 30 seconds. The
barrier is part of the 600-second budget. It then proves the expected exterior
scene, an idle/stable controller, drained lifecycle work, the exterior profile,
two-eye coherence, and unchanged foveated settings before assay 2 begins.

## Assay 2: 25 CS menu transitions

Start one render-scale stress capture and execute the hardware-appropriate
order through the same apply entrypoint used by the Community Shaders menu.
`Native` means render scale off; all other modes mean render scale on. DLSS uses
preset K. The runner records the selected matrix and never substitutes an
unsupported method.

The NVIDIA matrix is:

| Step | Method | Mode              |
| ---: | ------ | ----------------- |
|    1 | DLSS   | Native AA / DLAA  |
|    2 | DLSS   | Hoshipa           |
|    3 | DLSS   | Ultra Quality     |
|    4 | DLSS   | Quality           |
|    5 | DLSS   | Balanced          |
|    6 | DLSS   | Performance       |
|    7 | DLSS   | Ultra Performance |
|    8 | FSR    | Ultra Performance |
|    9 | FSR    | Performance       |
|   10 | FSR    | Balanced          |
|   11 | FSR    | Quality           |
|   12 | FSR    | Ultra Quality     |
|   13 | FSR    | Hoshipa           |
|   14 | FSR    | Native AA         |
|   15 | DLSS   | Native AA / DLAA  |
|   16 | FSR    | Native AA         |
|   17 | FSR    | Hoshipa           |
|   18 | DLSS   | Hoshipa           |
|   19 | DLSS   | Ultra Performance |
|   20 | FSR    | Ultra Performance |
|   21 | FSR    | Native AA         |
|   22 | DLSS   | Native AA / DLAA  |
|   23 | DLSS   | Hoshipa           |
|   24 | FSR    | Native AA         |
|   25 | FSR    | Hoshipa           |

The AMD matrix cannot execute DLSS and therefore uses this FSR-only sequence:

| Step | Method | Mode              |
| ---: | ------ | ----------------- |
|    1 | FSR    | Native AA         |
|    2 | FSR    | Hoshipa           |
|    3 | FSR    | Ultra Quality     |
|    4 | FSR    | Quality           |
|    5 | FSR    | Balanced          |
|    6 | FSR    | Performance       |
|    7 | FSR    | Ultra Performance |
|    8 | FSR    | Native AA         |
|    9 | FSR    | Ultra Performance |
|   10 | FSR    | Native AA         |
|   11 | FSR    | Hoshipa           |
|   12 | FSR    | Native AA         |
|   13 | FSR    | Ultra Quality     |
|   14 | FSR    | Quality           |
|   15 | FSR    | Native AA         |
|   16 | FSR    | Balanced          |
|   17 | FSR    | Native AA         |
|   18 | FSR    | Performance       |
|   19 | FSR    | Native AA         |
|   20 | FSR    | Ultra Performance |
|   21 | FSR    | Hoshipa           |
|   22 | FSR    | Ultra Performance |
|   23 | FSR    | Native AA         |
|   24 | FSR    | Ultra Performance |
|   25 | FSR    | Hoshipa           |

Every apply is bracketed by checked top-level `qualification_begin`,
`qualification_dispatch`, and `qualification_wait` calls. A new apply is not
sent until the prior target is first coherently stable. The capture must contain
exactly 25 accepted requests, 25 complete metrics records, no
duplicate/coalesced/superseded request, no ring loss, no failure/fallback, and
exact terminal profile and dimensions at every step. The existing ordinary and
pressure-protected stability ceilings remain 120 and 3,600 frames respectively.
The foveated settings invariant applies to every observation.

### DLSS dispatch trace

The assay must exercise every diagnostic method added by commit
`b46edeaed14c41ad41225641c3a4943f1db25db6`:

-   `dlss_trace_status` proves there is no inherited active trace;
-   `dlss_trace_reset` clears a stopped trace before each scoped DLSS sample;
-   `dlss_trace_start` starts the bounded, non-blocking trace;
-   `dlss_trace_stop` freezes it immediately after the stable result;
-   `dlss_trace_read` reads an intentionally bounded raw sample (16 records in
    this protocol), exact summary counters, and pinned failures.

The trace correlates constants and evaluations by frame token, resolved
viewport, eye, compositor cycle, thread, dimensions, quality, preset, resources,
and Streamline constants. On NVIDIA, at least one scoped DLSS transition must
contain valid constants and evaluations. On AMD, the same lifecycle is run
while FSR is active and must contain zero DLSS dispatches; this proves cleanup
and the absence of an accidental DLSS call but is not reported as DLSS dispatch
validation. A session mismatch, dropped record, duplicated
constants failure, evaluate failure, non-success result, or invalid stereo/frame
identity fails the assay. Ring overwrite is reported as partial raw-detail
coverage, but is not itself a render failure because the trace retains exact
summary counters and pins the latest failure. Normal and abort cleanup may stop
only the exact trace session ID returned to this runner. A lost start response
is an infrastructure failure; cleanup does not adopt or stop whichever global
trace happens to be active.

Assay 2 reports the same descriptive statistics as assay 1, overall and by
method and render-scale state.

## Recovery barrier 2

Restore the exterior FSR Hoshipa fixture, finalize all captures and traces, and
wait exactly 30 seconds through DevBench. Recheck scene, lifecycle, two-eye
profile, foveated settings, and screenshot readiness before visual capture.

## Assay 3: three one-minute visual captures

Capture three repetitions of the same static exterior FSR Hoshipa scene. Each
repetition uses the asynchronous screenshot service in `hmd_submission` mode,
`useSettings: false`, PNG, SDR sRGB, overwrite `never`, and a wall-clock
schedule of 16 acquisitions at 4,000-millisecond intervals. Ordinals 1, 8, and
16 correspond to the beginning, middle, and end of the one-minute span.
The parent receipt and child `acceptedUtc` timestamps must both attest a span
between 59 and 65 seconds; 16 frames returned immediately are not accepted as
a one-minute sequence.

Each acquisition must produce left eye, right eye, and side-by-side output from
the same HMD submission plus a manifest. Backpressure may skip at most ten
frames while finding an acquisition point, but a valid repetition has 16
acquired and written frames with zero failures or dropped outputs. A fallback
capture source, mismatched eye dimensions, missing hash, overwritten file, or
incomplete manifest fails capture integrity.

Capture integrity is automatic; visual quality is not guessed from file
existence. The same invocation runs six schema-constrained Codex image-model
evaluations: three replicate calls in parallel for each of two sequential,
blinded presentation passes. Every call inspects the original-resolution left,
right, and side-by-side images at ordinals 1, 8, and 16. PR mode supplies the
matching baseline images under neutral `first` and `second` labels, then swaps
their order for the second pass. The model-facing request contains neither
candidate/baseline roles nor identifying source paths.

The model records sharpness, unexpected blur, shimmer, stereo alignment, equal
eye scale, and geometry correspondence. Owner-bound renderer telemetry supplies
the seventh render-scale-latch verdict. Each response, request, prompt, output
schema, image order, source hash, provider version, execution event stream, and
sealed candidate position is hash-bound into the evidence. Low confidence,
indeterminate output, disagreement after normalizing the swapped passes,
missing evidence, or any failed item fails assay 3. Provider, authentication,
timeout, or schema failures fail closed as `INFRASTRUCTURE_ERROR`; no human can
complete or override the review.

## Speed comparison and suite verdict

The absolute 600-second limit is always a hard gate. PR mode also requires a
matching accepted baseline artifact. Candidate and baseline are paired by assay
and transition ordinal. The protocol reports absolute and relative changes for
total, median, mean, p95, and maximum stable latency. Versioned tolerance values
from the protocol manifest decide whether a regression is material; changing a
tolerance changes the protocol hash. Do not compare runs with different fixture
fingerprints or claim a performance gain from an unmatched run.

The suite verdict is `PASS` only when all three assays pass, both recovery
barriers pass, all six automated visual batches and telemetry checks agree, the
deadline passes, required diagnostic artifacts are complete, and the baseline
speed gates pass. Otherwise the verdict is `FAIL` or
`INFRASTRUCTURE_ERROR`; neither state may be rewritten as a pass.

Without `-PrMode`, a successful finalization is `LOCAL_PASS`, uses the
`csx-render-scale-local-v1` report schema, and writes
`qualification-summary.md`. It is not candidate PR evidence, but a finalized
run with the exact same protocol and fixture may serve as the comparison
baseline that bootstraps a new protocol revision.

Run the NVIDIA and AMD matrices on matching hardware when a PR claims universal
behavior. A single-host result is still attributable evidence for that vendor,
but it must not be presented as the missing vendor's pass.

## Required evidence and PR summary

The evidence root contains at least:

```text
run.raw.json
run.json
protocol.json
fixture-manifest.json
pr-summary.md                  # PR mode
qualification-summary.md       # local mode
failures.json
mcp-transcript.json
preflight-retained-diagnostics.json
transitions.json
transitions.csv
coc/scenario.request.json
coc/scenario.result.json
coc/diagnostics.json
coc/transitions.json
coc/transitions.csv
coc/stress-record.json
coc/cpu-record.json
recovery-1.json
menu/scenario.request.json
menu/scenario.result.json
menu/diagnostics.json
menu/transitions.json
menu/transitions.csv
menu/stress-record.json
menu/cpu-record.json
menu/dlss-traces.json
recovery-2.json
visual-index.json
visual-review.json              # generated in the same invocation
visual/diagnostics.json
visual/stress-record.json
visual/cpu-record.json
visual/rep-01/sequence.request.json
visual/rep-01/sequence.terminal.json
visual/rep-01/children.receipts.json
visual/rep-02/...
visual/rep-03/...
visual-review/preflight.json
visual-review/execution.json
visual-review/pass-01/rep-01/request.json
visual-review/pass-01/rep-01/response.json
visual-review/pass-01/rep-01/events.jsonl
visual-review/pass-01/rep-01/stderr.json
visual-review/pass-01/rep-01/receipt.json
visual-review/pass-01/rep-02/...
visual-review/pass-02/...
baseline/...                    # PR mode only
```

The PR summary states the protocol ID/hash, candidate and baseline build IDs,
fixture fingerprint, overall verdict and measured time, 20-COC statistics and
failure/stretch counts, 25-menu statistics and DLSS trace counters, both
recovery results, visual capture/review result, speed deltas, and an artifact
location. Exact failed, skipped, blocked, or pending checks remain visible.
