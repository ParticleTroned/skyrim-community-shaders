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

## MCP contract

Records use schema `community-shaders.vr-render-scale.iteration` and `schemaVersion: 1`. An automation client should:

1. Reject unknown schema versions.
2. Check `acceptance.accepted` before comparing performance.
3. Use `acceptance.gates` to classify a failed run instead of inferring failure from log text.
4. Compare transition records by `transitionEpoch`, never by array position alone.
5. Prefer lower stable latency and fewer retries only after correctness, fidelity, OOM, device-loss, retirement, memory-recovery, and backend-readiness gates pass.
6. Retain the complete JSON artifact with the candidate commit and scenario identifier.

The event ring retains 128 entries and the transition metrics ring retains 16 transitions. A capture-overflow gate fails if the event ring overwrites data. Keep each iteration within those bounds.

## Acceptance gates

The runtime currently requires:

-   a stopped capture containing at least two accepted requests;
-   an `Active` or `Idle` terminal controller with no transition still in flight;
-   no overwritten capture events, backend failures, OOM, or device loss;
-   no more than 32 retries for one transition;
-   at least one stable transition and no more than 120 frames to stability;
-   zero fidelity invariant mismatches across method, epoch, generation, dimensions, evaluation, and eye symmetry;
-   a drained retirement queue and no outstanding retirement fence;
-   memory recovered below `High` pressure with post-load recovery complete;
-   the active DLSS or FSR backend ready for its contract generation.

These thresholds are part of schema version 1. Change the schema version if their meaning or units change.

## Ghidra correlation

The record lists the principal native symbols under `analysis.symbols`. In Ghidra 12.1.2, correlate regressions with these paths first:

-   `Upscaling::ApplyPendingPerfModeRenderTargetRecreate` for admission, teardown, allocation, and retry behavior;
-   `Upscaling::ApplyPendingPostLoadRuntimeReset` for fast-travel recovery ownership;
-   `Upscaling::ResetVRVendorRuntimeResources` for DLSS/FSR lifetime differences;
-   `Upscaling::TryPromoteVRRenderScaleSubmitStageContract` for stable-presentation latency;
-   `Upscaling::RecordVRRenderScaleFidelityObservation` for both-eye contract failures.

Use Ghidra to validate control flow and ownership against the shipped binary, while using the JSON record as runtime evidence. A candidate should be promoted only when repeated scenario records pass and improve the target metric without regressing another accepted backend or pressure scenario.
