# VR render-scale iteration records

The VR render-scale controller can capture a bounded CS-menu stress session and write a versioned JSON record for an MCP/Ghidra optimization loop. The capture observes user-driven changes; it never changes render-scale settings itself.

## Capture workflow

1. Enable Community Shaders developer mode.
2. Open **Upscaling > Render Pipeline > Render Scale Stress Capture**.
3. Select **Start Capture**.
4. Exercise the same fixed scenario for every candidate build. At minimum, perform two render-scale changes. Include repeated preset changes and a fast-travel cycle when evaluating memory recovery.
5. Wait for the final change to reach a stable in-world presentation, then select **Stop Capture**.
6. Read the new record from `Data/SKSE/Plugins/CommunityShaders/Diagnostics/VRRenderScale/`.

Use identical save, location, CS profile, change order, dwell frames, HMD resolution, backend, and graphics settings when comparing iterations. Run DLSS and FSR as separate scenario series.

## DevBench automation

Step 17 exposes the capture contract through the external devbench host used by
Open Shaders. The bridge is built by default through `DEVBENCH_BRIDGE=ON`, is
inert when the devbench SKSE plugin is absent, and can be omitted completely
with `DEVBENCH_BRIDGE=OFF`.

The registered tool is `communityshaders.renderscale`:

-   `status` returns a compact live snapshot of the controller profiles, VRAM
    pressure, retirement queue, post-load recovery, backend generations,
    current metrics, and both-eye fidelity;
-   `record` returns the complete schema-v2 record without changing capture
    state;
-   `start` begins a new fixed-memory stress capture;
-   `apply` uses the same latest-wins transition entrypoint as a CS-menu change.
    It requires `method` (`dlss` or `fsr`), `enabled`, `qualityMode`, and an
    optional `dlssPreset`;
-   `stop` stops the capture, writes the disk artifact, and returns the complete
    record in the tool response;
-   `reset` clears a stopped capture.

Mutating actions fail closed outside Skyrim VR. `start` and `apply` require
developer mode, and `apply` also requires an active capture so an automation
client cannot make unrecorded benchmark changes. Quality modes are `0` for
native AA/DLAA, `1` Hoshipa, `2` Ultra Quality, `3` Quality, `4` Balanced, `5`
Performance, and `6` Ultra Performance; enabled render scale requires `1..6`.

For one candidate cycle, start capture, apply the fixed scenario profiles, and
poll `status` until each requested epoch reaches `Active` with the stable
profile, exact backend generation, and both eyes valid. Drive the fixed
fast-travel leg through devbench's own console tool when that scenario requires
it. Stop capture only after the final recovery settles, then reject the run if
any returned acceptance gate fails. The record includes the build's git
description so artifacts remain attributable to the exact candidate source.

Performance builds keep `kEnableVRMenuPresentationTraceDiagnostics` false.
Changing it to true creates a dedicated forensic build with high-frequency D3D
menu detours and must not be compared against normal optimization captures.

Step 18 calibrates the acceptance contract against the first live MCP rapid-
switch baseline. A request that reuses the already-active physical contract can
complete on its apply frame without entering vendor stabilization. That path
emits both `Applied` and `Stable`, matching its completed transition metric, so
the stable-latency gate accepts bounded synchronous reuse while retaining the
same event evidence required from a rebuilt DLSS or FSR contract. Session start
and stop events carry zero transition counters rather than inheriting metrics
from work outside the capture boundary.

Step 19 closes the logical-versus-physical convergence gap found by the first
RC97 MCP preflight. An unchanged active CS-menu profile is synchronous only when
the boot latch, quality, exact backend generation, backend resources, and common
vendor textures are all present. If that contract is incomplete and no recovery
is already in flight, the request enters the normal latest-wins controller and
queues an epoch-owned relatch. Zero-generation or resource-free lifecycle state
is not reported as backend-ready, preventing a missing contract from producing
a false `Applied`/`Stable` result.

Step 20 preserves the DLSS teardown result across Streamline, submit-stage, and
vendor-reset boundaries. A D3D11 idle fence that is still pending now records
`WaitingForDrain` and a bounded backend retry instead of a backend failure. Query
or Streamline resource-free errors remain `Failed`, so the acceptance contract
continues to reject genuine teardown faults while allowing expected asynchronous
GPU drain polling during DLSS/FSR handoffs and post-load recovery.

## MCP contract

Records use schema `community-shaders.vr-render-scale.iteration` and `schemaVersion: 2`. An automation client should:

1. Reject unknown schema versions.
2. Check `acceptance.accepted` before comparing performance.
3. Use `acceptance.gates` to classify a failed run instead of inferring failure from log text.
4. Compare transition records by `transitionEpoch`, never by array position alone.
5. Prefer lower stable latency and fewer retries only after correctness, fidelity, OOM, device-loss, retirement, memory-recovery, and backend-readiness gates pass.
6. Retain the complete JSON artifact with the candidate commit and scenario identifier.

The event ring retains 128 entries and the transition metrics ring retains 16 transitions. A capture-overflow gate fails if the event ring overwrites data, and a metrics-coverage gate fails if any captured request epoch has rotated out of the metrics ring. Keep each iteration within both bounds. Retry and failure events retain their normalized kind so pressure, retirement, backend, OOM, and device-loss evidence remains classifiable.

## Acceptance gates

The runtime currently requires:

-   a stopped capture containing at least two accepted requests;
-   an `Active` or `Idle` terminal controller with no transition still in flight;
-   no overwritten capture events and complete per-request metric coverage;
-   no backend failures, OOM, or device loss in either metrics or classified events;
-   no more than 32 retries for one transition;
-   at least one stable transition and no more than 120 frames to stability;
-   zero fidelity invariant mismatches across method, epoch, generation, dimensions, evaluation, and eye symmetry, with finalized vendor evaluation proven for both eyes;
-   a fully drained retirement queue with no deferred cleanup frame, outstanding fence, or capacity block;
-   a valid DXGI memory sample and pressure recovered below `High` with post-load recovery complete;
-   the active DLSS or FSR backend ready with exact requested, runtime, and stable contract generations.

These thresholds are part of schema version 2. Change the schema version if their meaning or units change.

## Ghidra correlation

The record lists the principal native symbols under `analysis.symbols`. In Ghidra 12.1.2, correlate regressions with these paths first:

-   `Upscaling::ApplyPendingPerfModeRenderTargetRecreate` for admission, teardown, allocation, and retry behavior;
-   `Upscaling::ApplyPendingPostLoadRuntimeReset` for fast-travel recovery ownership;
-   `Upscaling::ResetVRVendorRuntimeResources` for DLSS/FSR lifetime differences;
-   `Upscaling::TryPromoteVRRenderScaleSubmitStageContract` for stable-presentation latency;
-   `Upscaling::RecordVRRenderScaleFidelityObservation` for both-eye contract failures.

Use Ghidra to validate control flow and ownership against the shipped binary, while using the JSON record as runtime evidence. A candidate should be promoted only when repeated scenario records pass and improve the target metric without regressing another accepted backend or pressure scenario.
