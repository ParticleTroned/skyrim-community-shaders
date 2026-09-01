# DLSS Neural Rendering experiments

These branches exercise NVIDIA NGX Feature 18 through an isolated D3D12
interop service. Normal DLSS remains on the existing Streamline D3D11 path.
Feature 18 is not exposed by the public Streamline 2.12 headers, so this code
does not register an invented Streamline feature or tag contract.

This is internal research, not a redistributable or release-ready integration.
The repository does not grant rights to NVIDIA binaries. At the user's explicit
request, these experiment branches can produce an internal AIO that contains a
hash-pinned runtime set from paths supplied through the CMake cache. Do not
commit those DLLs or publish, redistribute, or attach the resulting AIO to a
release.

## Branch contracts

| Branch      | Fixed center-pipeline arrangement              | Failure behavior                                |
| ----------- | ---------------------------------------------- | ----------------------------------------------- |
| `paintball` | normal DLSS, then Feature 18                   | retain the completed DLSS center                |
| `paint`     | Feature 18 at low resolution, then normal DLSS | send the original low-resolution center to DLSS |
| `ball`      | Feature 18 replaces normal DLSS                | run normal DLSS for that center                 |

All three routes remain ahead of the existing feathered center/periphery
composite, sharpening, menu/UI composite, HMD mask, and OpenVR submission.
The arrangement is compiled into each branch; it is not a saved setting.

## Live implementation matrix

The top-level **NVIDIA Neural Rendering** panel appears immediately before
NVIDIA Reflex. The arrangement above remains branch-fixed, while two live,
persisted selectors expose four real execution lanes:

| Index | Implementation ID | Stereo submission | Output commit | Measurement purpose              |
| ----- | ----------------- | ----------------- | ------------- | -------------------------------- |
| 0     | `per_eye_staged`  | Per-eye           | Staged        | Original baseline                |
| 1     | `batched_staged`  | Batched           | Staged        | Isolates stereo-batching benefit |
| 2     | `per_eye_direct`  | Per-eye           | Direct        | Isolates direct-commit benefit   |
| 3     | `batched_direct`  | Batched           | Direct        | Fully optimized path             |

New and reset configurations default to `batched_direct`. Staged mode writes
Feature 18 into private outputs and Resolve selects those outputs only after a
successful stereo transaction. Direct mode writes the private center inputs in
place. On `paint`, any partial direct Feature 18 result restores both centers
from the exact resource, subresource, and source box captured during Stage
before normal DLSS consumes either center.

The **Outer DLSS Base** switch is independent of these lanes. When eligible it
runs an isolated normal-DLSS full-eye stereo base, feathers the completed
NR-to-DLSS centers over it, and then follows the existing sharpening, UI, mask,
and submission stages. A failed or ineligible Outer request retains the
existing spatial/periphery route; it never publishes a partial Outer pair.

## Runtime trust boundary

Normal DLSS stays separate from Neural Rendering. For this internal test, CMake
copies a matched Streamline 2.13 core/plugin set and NVIDIA-signed 310.8
`nvngx_dlss.dll` from the user-supplied local runtime directory. CSX remains
compiled against the Streamline 2.12 headers, so unchanged normal-DLSS operation
is the first runtime compatibility gate. Feature 18 is loaded directly from
`nvngx_dlssnr.dll`; it does not use or require `sl.dlss_nr.dll` or a Streamline
Neural Rendering plugin.

The direct loader accepts exactly three `nvngx_dlssnr.dll` SHA-256 identities:

-   allowlisted signed 310.8 identity:
    `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`
-   allowlisted patched 310.8 identity:
    `8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`
-   alternate allowlisted patched 310.8 identity:
    `CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650`

The hash is computed before `LoadLibraryExW`. All three identities are accepted
at every CSX log level. The patched identities remain restricted to their exact
pinned hashes; changing the log level does not widen the allowlist, enable
Streamline developer mode, sign a DLL, or authenticate a plugin. “Signed”
describes the inspected allowlisted file; this loader pins its hash and does not
perform a new Authenticode trust decision or certify a binary as malware-free.

The runtime DLL does not necessarily export the NGX parameter allocator. When
it does, its already allowlisted runtime identity is also the parameter-core
identity. Otherwise, before calling NGX initialization, CSX requires exactly
one loaded exporter whose locked file identity resolves below
`System32\\DriverStore\\FileRepository`, whose basename is `nvngx.dll` or the
DriverStore alias `_nvngx.dll`, and whose Authenticode signature passes an
offline, no-UI verification. Current DriverStore binaries can carry a Microsoft
WHCP signature, so CSX checks the file's NVIDIA product metadata separately:
`CompanyName` must be `NVIDIA Corporation`, `ProductName` must be `NGX`, and
`OriginalFilename` must be `nvngx.dll`. Version metadata is an additional
identity check, not proof of the signer's publisher. CSX retains both the module
reference and a file handle that denies writes and deletion across initialization
and for the complete NGX parameter lifetime. Any sibling `nvngx.dll` or
`_nvngx.dll` beside `nvngx_dlssnr.dll` is rejected before the first vendor
initialization call. The selected core path, SHA-256, trust result, and selection
source are exposed by `nr_status`; ambiguity or any failed check is a closed-gate
failure.

During NGX initialization, feature creation, evaluation, release, and shutdown,
the implementation temporarily replaces `nvngx_dlssnr.dll`'s imported
`GetModuleFileNameW` function. A query that passes the CSX module handle is
reported as the sibling `nvngx.dll` path; other queries call the original
function. The import is restored after each scoped NGX call. This is a
caller-path substitution that can affect vendor caller validation, not a
signature or authentication mechanism. It requires explicit legal and license
review before any distribution or use beyond this internal experiment.

Local runtime staging is disabled by default. When explicitly enabled, it
stages exactly seven files into `Shaders/Upscaling/Streamline`: the six
normal-DLSS runtime modules and the patched 310.8 `nvngx_dlssnr.dll`. Configure
fails if a source is absent or does not match its declared SHA-256. No runtime
is downloaded. Set `CSX_STAGE_LOCAL_DLSS_RUNTIME=ON` and supply
`CSX_LOCAL_DLSS_RUNTIME_DIRECTORY` and `CSX_LOCAL_DLSSNR_RUNTIME_FILE` through
the CMake cache to select the private source files.

The staged NR identity is the malware-screened but modified `8270...206` file
selected by `CSX_LOCAL_DLSSNR_RUNTIME_FILE`. Its embedded NVIDIA signature
reports `HashMismatch`, so admission relies on its exact pinned SHA-256 and
310.8 version at every CSX log level. The signed `E16B...FC8E` identity and
alternate patched identity remain allowlisted for separately selected tests but
are not staged by this build. Hash pinning and prior screening do not certify a
binary as malware-free. Consult the license accompanying each NVIDIA binary.
Do not publish or redistribute the generated AIO.

The first bridge binds color, depth, motion vectors, and output only. Automatic
masking is therefore fixed on and UI correction is fixed off; manual masks and
UI correction fail closed until their ControlMask/UI resources and subrects are
implemented. A Neural Rendering center is committed only when both eyes succeed
in the same stereo transaction. Otherwise both eyes retain the arrangement's
normal-DLSS fallback.

## Working reference and deliberate divergence

The comparison baseline is YtzyFvra's working `feature/dlssnr-vr` branch at
commit `05e037cad2add33a434c09d7b1260d09d331b6a4`. Its VR route runs Feature 18
over an already assembled LDR stereo image immediately before UI, and commit
`3d6748f16` batches both eyes into one D3D12 command list to reduce queue waits.
Its later fixes establish that before-UI ordering and stereo batching are
separate concerns: the latter is a performance change, not the prerequisite
that made the placement correct.

CSX does not copy that final-composite hook because its foveated pipeline has a
different ownership contract. NR remains inside the center transaction, ahead
of feathering, sharpening, UI, HMD masking, and submission. Both centers use
private resources. Batched mode records
both Feature 18 evaluations in one D3D12 command list; per-eye mode submits one
command transaction per eye. The
stereo route publishes NR only after both evaluations and cross-API
synchronization succeed. A record, synchronization, or partial-eye failure
therefore publishes neither pair and requests a history reset.

On `paint`, Resolve uses an NR output as the normal-DLSS color input only when
the stereo renderer call succeeded and that eye's resources remain ready. NR is
reported as applied only after both eyes consume those inputs successfully. A
partial Resolve result fails the stereo transaction before the feathered
composite; a zero-eye result retains the normal-DLSS fallback.

NR is explicitly suppressed while a Skyrim presentation menu or the Community
Shaders menu is active. A submit-stage pair is reusable only while its source,
frame, generation, settings, and menu admission still match. If a setting or
menu changes after one eye was already presented, the retained pair may complete
only the missing peer eye in that same frame. It cannot start another pair or be
replayed into a later frame. A repeated call for the accepted eye is bypassed
without invalidating the pair needed by its peer. The old textures remain alive
until their compositor cycle retires even after presentation is rejected.
DevBench distinguishes the route's GPU `neuralCommitted` mask from
`presentationAccepted`, which advances only after OpenVR accepts that exact
retained output.

Feature 18 histories are also keyed to the last successful rendered frame. A
menu, fallback, disabled interval, or other skipped evaluation forces a temporal
reset when the route resumes. Both batched and per-eye stereo apply that decision
to both eyes when either history is discontinuous, avoiding asymmetric reset
state. Per-eye modes retain two separate command submissions for the experiment.

### Paint low-resolution quality profile

`paint` deliberately gives Feature 18 fewer pixels and less temporal metadata
than the working after-DLSS reference. Small NR changes can therefore be
attenuated by the following normal-DLSS pass, while aggressive local structure,
skin structure, or full-output sharpening can magnify unstable low-resolution
detail. The four implementation lanes change command batching and output-copy
ownership only; they are not expected to change this image-domain behavior.

The **Conservative Low-Res (NR only)** preset provides a zero-extra-pass starting point:
intensity `1.40`, local tone `1.05`, local structure `0.65`, skin structure
`0.20`, and style `3`. Start validation with DLSS preset K and sharpening Off or
low Luma Unsharp. RCAS above `0.5`, especially the historical `0.9` default, is
expected to make coarse grids and shimmer easier to see. This profile limits
amplification; it does not add missing high-resolution context or prove the
private Feature 18 jitter/HDR contract. It changes NR tuning only and deliberately
does not overwrite the separately controlled DLSS sharpener.

`sourceSignatureProven` verifies resource identity, descriptor, stereo layout,
and source region. It does not prove that the producer freshly wrote both eyes
before the first per-eye submit callback. That content-coherence boundary remains
a separate runtime hypothesis for persistent eye mismatch or moving shadows.
DevBench reports `submitEyeMaskAtDecision` for the retained route and the latest
observed submit-eye mask so tests can identify transactions created after only
one eye callback without adding GPU work.

### Paint temporal and route diagnostics

`paint` exposes a separate diagnostic group for testing the unresolved temporal
and stereo-boundary hypotheses in game. These controls are experiments, not
alternative quality presets:

| Control                    | Default  | Diagnostic contract                                                                        |
| -------------------------- | -------- | ------------------------------------------------------------------------------------------ |
| Main-stage route           | On       | Allows NR in the render-stage stereo route.                                                |
| Submit-stage route         | On       | Allows NR in the retained presentation route.                                              |
| Reset history every frame  | Off      | Sends a synchronized reset to both Feature 18 eyes on every evaluated frame.               |
| Force Feature 18 upscaling | Off      | Sets the private upscaling flag even though the NR input and output remain 1:1.            |
| Motion-vector scale        | Full eye | Tests the existing full-eye pixel scale against a center-region pixel scale.               |
| Submit-pair boundary       | Current  | Selects `Current`, `Observe`, or `Require` admission for the two-eye BSOpenVR source pair. |

These defaults exactly preserve the current `paint` execution contract. Changing
any diagnostic control invalidates retained presentation outputs and requests a
synchronized two-eye Feature 18 history reset, so one eye cannot continue a
history created under different assumptions.

The route switches isolate where NR executes; they do not change the fixed
NR-before-DLSS arrangement. Disabling both leaves normal center DLSS as the
fallback. `Reset history every frame` deliberately removes useful temporal
accumulation and is therefore evidence-gathering behavior, not a proposed
production fix. The upscaling switch changes only the private Feature 18 flag at
equal input/output dimensions. The motion-vector selector changes only the scale
passed to Feature 18; normal Streamline DLSS retains its existing temporal
constants and motion-vector convention.

The submit boundary modes separate observation from policy:

-   `Current` retains the existing admission decision at the first eligible
    submit callback.
-   `Observe` retains that decision while recording whether each nested eye
    callback belongs to Skyrim's matching outer `BSOpenVR::Submit` scope. A
    completed outer scope with `provenEyeMask == 3` proves that it owned both
    eye callbacks; `incompleteOuterCalls` records scopes that did not.
-   `Require` admits a submit-stage NR pair only when the current nested eye is
    correlated with that engine-owned outer scope. It does not wait for the
    second callback, because doing so would require delaying, duplicating, or
    retroactively replacing the first OpenVR submission.

This BSOpenVR pair scope changes only admission to the retained NR/Outer-DLSS
work. It never assumes ownership of, consumes, reorders, suppresses, duplicates,
or directly performs the OpenVR submit call; presentation remains owned by the
existing per-eye submission path.

Neither the inspected 310.8 runtime parameter strings nor the working reference
provide evidenced private Feature 18 bindings for jitter, camera matrices,
exposure, or HDR metadata. CSX therefore does not invent parameter names for
them. Normal DLSS continues to receive its public Streamline jitter, camera,
motion-vector, and related constants; these diagnostics test the observable
Feature 18 contract that is actually available.

Use an adaptive A/B sequence while holding the scene, NR tuning, DLSS preset,
implementation lane, render scale, and sharpening constant:

1.  Capture the default with both route gates on and `Current` boundary.
2.  Compare Main-only and Submit-only. A symptom confined to Submit points at
    retained-source freshness or callback timing; a symptom in both points
    toward shared low-resolution inputs or the private Feature 18 contract.
3.  On a Submit-sensitive result, select `Observe` first. Confirm completed
    scopes reach `provenEyeMask == 3` without a growing `incompleteOuterCalls`
    count, then repeat with `Require`. An improvement supports the outer
    producer-scope hypothesis; incomplete scopes reject it without changing
    OpenVR call order.
4.  Enable per-frame reset on the affected route. If crawling stops while
    single-frame detail remains coherent, temporal accumulation or reprojection
    is implicated. If it persists, prioritize spatial input, crop, depth, or
    stereo-source hypotheses.
5.  Return reset to Off, then test the equal-size Feature 18 upscaling flag. A
    repeatable change isolates vendor mode selection from resource dimensions.
6.  Return that flag to Off, then compare full-eye and center-region motion-vector
    scales during controlled head and object motion. Improvement only with the
    center scale supports cropped-coordinate interpretation; improvement only
    with full-eye scale supports the current convention.
7.  Combine settings only after one single-variable comparison produces a
    repeatable signal. Recheck the opposite value in the same process before
    attributing the result, because a diagnostic change intentionally resets
    both histories.

## Stereo performance diagnostics

`nr_status` distinguishes routing from vendor work. A `prepared` eye has valid
arguments and private resources staged for the batch; it does not prove that
the renderer or NGX ran. An `attempted` eye reached the exact NVIDIA Feature 18
evaluation call, and the renderer's `featureEvaluations` counter counts those
same per-eye calls. An `evaluated` eye belongs to a renderer transaction that
completed successfully, while `applied` means the arrangement consumed that
result and `neuralCommitted` means the complete pair reached the route's
irreversible output commit.

The D3D11 preparation and output-commit microseconds are CPU enqueue durations:
they measure host time spent issuing copies and dispatches, not GPU execution.
Feature GPU microseconds come from asynchronous D3D12 timestamps around the
Feature 18 command range. Main samples accept any non-empty subset of slots 0
and 1 (`0b0001`, `0b0010`, or `0b0011`); submit samples accept the corresponding
subset of slots 2 and 3 (`0b0100`, `0b1000`, or `0b1100`). At successful queue
submission, zero or cross-route masks increment the unexpected-mask counter.
Total command submissions and GPU
samples/time are split by main and submit route, while stereo-only submission
counters continue to isolate batching. Submission metadata remains available
when timestamp queries are not; timestamp readback failures are counted.
Backpressure microseconds measure the bounded CPU wait for a reusable
command context.

`discontinuousHistoryResets` counts Feature 18 eye evaluations reset because a
slot did not run in the immediately preceding rendered frame. In any stereo pair,
one discontinuous eye resets both evaluations and therefore increments the
counter twice.

An `attempted` eye is vendor-call evidence, not stereo-submission or commit
evidence. For the comparison branches,
`ball` directly replaces normal DLSS for the center only, while `paint` runs NR
before the normal center DLSS resolve.

`nr_configure` is an atomic partial patch for every control in the NR panel:
`enabled`, `outerDLSS`, the two implementation axes (or canonical
`implementationMode`), route gates, per-frame reset, equal-size Feature 18
upscaling flag, motion-vector scale, submit-pair boundary, preset, four tuning
strengths, style, and the fixed safe contract fields. Presets apply first;
explicit tuning or style in the same call applies second and selects Custom.
Contradictory implementation selectors and unsupported `useAutoMask=false` or
`uiCorrection=true` requests reject the whole call without mutation. Numeric
tuning outside 0..2, presets outside 0..5, and styles outside 0..3 are rejected
rather than silently clamped. `nr_cycle_modes` advances
exactly one lane in the table order per call, or selects `matrixIndex` 0..3
explicitly; it returns the selected lane but does not claim execution until
fresh main/submit route telemetry observes it. `nr_reset` remains the bounded
runtime reset and history-reset control.

## First runtime validation

Use `communityshaders.renderscale` Neural Rendering status and reset actions to
preserve the exact runtime and parameter-core paths, versions, hashes and trust
decisions; load/init/create/evaluate/rollback stages; NGX and HRESULT results;
proxy hits; D3D formats; per-slot dimensions; stereo eye masks; current-frame
and current-cycle freshness; submit admission reason; and fallback counters. A
run with no Feature 18 attempt is a routing or deployment failure, not evidence
of a signature rejection.

Validate in this order:

1. Confirm unchanged normal foveated DLSS with Neural Rendering disabled.
2. Enable Neural Rendering and prove both eye/role slots reach runtime probe.
3. Prove D3D11-to-D3D12 shared copies and Feature 18 evaluation without a
   fallback or device-removal signal.
4. Confirm both centers complete in the same compositor cycle and remain ahead
   of feathering, sharpening, UI, HMD masking, and submission.
5. Exercise camera cuts, loads, menus, render-scale transitions, quality/preset
   changes, enable/disable, and backend reset.

Feature 18 and its `DLSSNR.*` parameter names are private, version-specific
contracts. A successful build proves only API compatibility; a controlled VR
run is required before calling any arrangement working.
