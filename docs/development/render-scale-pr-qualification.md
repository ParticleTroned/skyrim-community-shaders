# Render-scale PR qualification

Render-scale changes use the versioned `csx-render-scale-pr-v1` DevBench
protocol. The canonical runner lives in the Skyrim VR automation repository at
`tools/render-scale-qualification/Invoke-CSXRenderScaleQualification.ps1`.
Copy the generated `pr-summary.md` into every PR that changes render-scale
behavior and retain the complete evidence directory with the candidate build.

The suite is deliberately bounded. After binding an already-running runtime,
the runner has 600 seconds for preflight, fixture establishment, all three
assays, both recovery barriers, and final screenshot manifests. It also reports
a narrower performance interval from the first COC dispatch through the third
manifest. Game/MO2 launch and the later offline human or image-capable review
are recorded separately. Preflight time is never hidden by extending the
runner deadline.

Invoke the distributed runner with an exact DevBench runtime, build ID, GPU
matrix, and fixture manifest. PR mode also requires a previously accepted
baseline evidence directory (or its `run.json`):

```powershell
pwsh .\tools\render-scale-qualification\Invoke-CSXRenderScaleQualification.ps1 `
    -EvidenceDirectory C:\Evidence\render-scale-candidate `
    -RuntimePath $env:CSX_DEVBENCH_RUNTIME_PATH `
    -ExpectedBuildId '<64-character CSX build ID>' `
    -GpuVendor NVIDIA `
    -FixtureManifestPath C:\Evidence\render-scale-fixture.json `
    -PrMode `
    -BaselinePath C:\Evidence\render-scale-baseline
```

The automated run ends as `REVIEW_PENDING`. Review the nine selected stereo
samples, save the completed template as `visual-review.json`, and finalize the
same evidence directory:

```powershell
pwsh .\tools\render-scale-qualification\Invoke-CSXRenderScaleQualification.ps1 `
    -EvidenceDirectory C:\Evidence\render-scale-candidate `
    -FinalizeReview
```

## Fixed fixture

Run a candidate and its baseline with the same save, camera pose, HMD runtime,
refresh rate, eye resolution, GPU and driver, game runtime, Community Shaders
settings, VR FPS Stabilizer rules, and DevBench/automation versions. The runner
rejects a comparison when their fixture fingerprints differ.

The `csx-render-scale-fixture-v1` manifest records a fixture ID; save ID and
SHA-256; camera ID and configuration SHA-256; VR FPS Stabilizer version and
configuration SHA-256; GPU vendor, device ID, and driver; and HMD model,
runtime, runtime version, and refresh rate. The runner adds the observed eye
dimensions, FSR runtime, service capabilities, protocol hash, and bound runtime
identity before hashing the effective fixture. Machine-specific paths are not
part of the manifest. Start from the distributed `fixture.example.json`; the
runner rejects its placeholders and all-zero hashes, so it must be filled with
the real controlled fixture before a run.

Start in `WindhelmExterior01`. The COC route alternates with
`WhiterunDragonsreach`. The VR FPS Stabilizer configuration must select these
exact profiles:

| Location | NVIDIA | AMD |
| --- | --- | --- |
| Interior | DLSS Native AA (DLAA), render scale off | FSR Native AA, render scale off |
| Exterior | FSR Hoshipa, render scale on | FSR Hoshipa, render scale on |

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

Each transition is one server-side DevBench scenario segment:

1. `communityshaders.renderscale qualification_begin` captures the server QPC,
   frame, source cell, profile, stress counters, and diagnostic baselines.
2. DevBench executes the one `coc` command.
3. `communityshaders.renderscale qualification_wait` waits for the exact
   destination, requested/effective/stable profile agreement, complete physical
   resources, clean lifecycle, and a coherent two-eye presentation.

The waiter stops on the first coherent stable observation. DevBench dispatches
the next COC immediately after that result. There are no fixed sleeps, menu
queries, client polling loops, same-cell COCs, or overlapping transitions in
the 20-transition sequence. The per-transition ceiling is 120 seconds, further
bounded by the suite deadline.

Native AA requires both eyes to present `NativeOriginal`, native dimensions,
and cleared render-scale/foveated vendor flags. An active Hoshipa profile
requires both eyes to present the exact current vendor evaluation, matching
method, generation, epoch, input extent, output extent, and stable physical
contract. Intentional `PresentationStretch` is measured separately and must
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

| Step | Method | Mode |
| ---: | --- | --- |
| 1 | DLSS | Native AA / DLAA |
| 2 | DLSS | Hoshipa |
| 3 | DLSS | Ultra Quality |
| 4 | DLSS | Quality |
| 5 | DLSS | Balanced |
| 6 | DLSS | Performance |
| 7 | DLSS | Ultra Performance |
| 8 | FSR | Ultra Performance |
| 9 | FSR | Performance |
| 10 | FSR | Balanced |
| 11 | FSR | Quality |
| 12 | FSR | Ultra Quality |
| 13 | FSR | Hoshipa |
| 14 | FSR | Native AA |
| 15 | DLSS | Native AA / DLAA |
| 16 | FSR | Native AA |
| 17 | FSR | Hoshipa |
| 18 | DLSS | Hoshipa |
| 19 | DLSS | Ultra Performance |
| 20 | FSR | Ultra Performance |
| 21 | FSR | Native AA |
| 22 | DLSS | Native AA / DLAA |
| 23 | DLSS | Hoshipa |
| 24 | FSR | Native AA |
| 25 | FSR | Hoshipa |

The AMD matrix cannot execute DLSS and therefore uses this FSR-only sequence:

| Step | Method | Mode |
| ---: | --- | --- |
| 1 | FSR | Native AA |
| 2 | FSR | Hoshipa |
| 3 | FSR | Ultra Quality |
| 4 | FSR | Quality |
| 5 | FSR | Balanced |
| 6 | FSR | Performance |
| 7 | FSR | Ultra Performance |
| 8 | FSR | Native AA |
| 9 | FSR | Ultra Performance |
| 10 | FSR | Native AA |
| 11 | FSR | Hoshipa |
| 12 | FSR | Native AA |
| 13 | FSR | Ultra Quality |
| 14 | FSR | Quality |
| 15 | FSR | Native AA |
| 16 | FSR | Balanced |
| 17 | FSR | Native AA |
| 18 | FSR | Performance |
| 19 | FSR | Native AA |
| 20 | FSR | Ultra Performance |
| 21 | FSR | Hoshipa |
| 22 | FSR | Ultra Performance |
| 23 | FSR | Native AA |
| 24 | FSR | Ultra Performance |
| 25 | FSR | Hoshipa |

Every apply is bracketed by `qualification_begin` and `qualification_wait` in
one server-side scenario. A new apply is not sent until the prior target is
first coherently stable. The capture must contain exactly 25 accepted requests,
25 complete metrics records, no duplicate/coalesced/superseded request, no ring
loss, no failure/fallback, and exact terminal profile and dimensions at every
step. The existing ordinary and pressure-protected stability ceilings remain
120 and 3,600 frames respectively. The foveated settings invariant applies to
every observation.

### DLSS dispatch trace

The assay must exercise every diagnostic method added by commit
`b46edeaed14c41ad41225641c3a4943f1db25db6`:

- `dlss_trace_status` proves there is no inherited active trace;
- `dlss_trace_reset` clears a stopped trace before each scoped DLSS sample;
- `dlss_trace_start` starts the bounded, non-blocking trace;
- `dlss_trace_stop` freezes it immediately after the stable result;
- `dlss_trace_read` reads an intentionally bounded raw sample (16 records in
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
summary counters and pins the latest failure. The trace must always be stopped
and read during abort cleanup; an active trace at handoff fails the suite.

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
existence. A human or image-capable reviewer must attest all nine selected
side-by-side samples in `visual-review.json`, bound to their SHA-256 hashes. In
PR mode, every candidate hash is paired with the corresponding matching
baseline hash and receives an explicit no-regression verdict. The review
records sharpness, unexpected blur or shimmer, left/right alignment, equal eye
scale, geometry correspondence, and whether render scale remained latched. Any
failed item fails assay 3. Missing review produces `REVIEW_PENDING`, never
`PASS`.

## Speed comparison and suite verdict

The absolute 600-second limit is always a hard gate. PR mode also requires a
matching accepted baseline artifact. Candidate and baseline are paired by assay
and transition ordinal. The protocol reports absolute and relative changes for
total, median, mean, p95, and maximum stable latency. Versioned tolerance values
from the protocol manifest decide whether a regression is material; changing a
tolerance changes the protocol hash. Do not compare runs with different fixture
fingerprints or claim a performance gain from an unmatched run.

The suite verdict is `PASS` only when all three assays pass, both recovery
barriers pass, the visual review is complete, the deadline passes, required
diagnostic artifacts are complete, and the baseline speed gates pass. Otherwise
the verdict is `FAIL`, `REVIEW_PENDING`, or `INFRASTRUCTURE_ERROR`; these states
must not be rewritten as a pass.

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
pr-summary.md
failures.json
mcp-transcript.json
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
visual-review.template.json
visual-review.json              # after offline review
visual/rep-01/sequence.request.json
visual/rep-01/sequence.terminal.json
visual/rep-01/children.receipts.json
visual/rep-02/...
visual/rep-03/...
baseline/...                    # PR mode only
```

The PR summary states the protocol ID/hash, candidate and baseline build IDs,
fixture fingerprint, overall verdict and measured time, 20-COC statistics and
failure/stretch counts, 25-menu statistics and DLSS trace counters, both
recovery results, visual capture/review result, speed deltas, and an artifact
location. Exact failed, skipped, blocked, or pending checks remain visible.
