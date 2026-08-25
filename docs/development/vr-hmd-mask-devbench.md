# VR HMD-mask measurement

The versioned `communityshaders.renderscale` DevBench tool controls the
live HMD-mask implementation and exposes its measurement contract. Include
the `expectedBuildId` returned by discovery or status in every call. This
makes an automation run fail closed when MO2 loaded a stale DLL.

The available modes are:

-   `sparse_depth_9tap`: the production default. It checks a thresholded
    radius-2 nine-tap pattern.
-   `robust_depth_5x5`: the 25-tap reference used for controlled comparison
    and verified post-load repair.

The Upscaling diagnostics UI does not expose HAM controls. Use
`ham_set_mode` for a controlled live A/B test. A changed mode requests a
temporal-history reset and logs at Info; Debug or Trace logging is not
required.

## Performance capture

Call `ham_status` first. Its `performance` object identifies the
`communityshaders.profiler_api` tool, the `Upscaling::HAM::` prefix, and
the stable timer names. Capture profiler history only while
`qualityCapture.active` is false. The fidelity audit is a separate GPU pass
and would contaminate timing.

A controlled comparison should:

1. Apply one HAM mode.
2. Wait until the same scene is stable and the intended render scale is
   latched.
3. Keep the headset resolution, quality profile, frame limit, and thermal
   conditions unchanged.
4. Call `ham_reset` immediately before starting a bounded profiler capture.
5. Preserve the capture and `ham_status` counters before changing mode.
6. Repeat the same warm-up and capture for the other mode.

Repeated left- and right-eye passes with one timer name are accumulated into
one profiler frame sample. Sum any active input and final timers when the
tested route uses both phases. Keep `VerifiedRepairRobust` separate because
it represents bounded recovery work, not ordinary-frame cost.

Reject a capture when its producer Build ID, resolution, render-scale state,
resolved-frame count, or dispatch counters do not match the intended
experiment.

## Fidelity capture

Fidelity is a separate bounded GPU audit:

```json
{
    "action": "ham_quality_start",
    "maxFrames": 60,
    "expectedBuildId": "<build-id>"
}
```

Poll `ham_quality_status` until `qualityCapture.complete` is true. The
capture stops after both eyes have been observed for the requested number of
frames. `ham_quality_stop` ends it early. `ham_reset` clears completed
quality results and all HAM dispatch counters.

The audit samples a stable one-in-four display-domain pixel grid and compares
the active decision with the thresholded, two-pixel-dilated 5x5 reference. It
reports:

-   `falseNegatives`: the reference clears but the candidate retains color.
    These can leak hidden-area color into temporal reconstruction.
-   `falsePositives`: the candidate clears where the reference retains color.
    These can remove visible edge pixels.
-   `skippedDispatches`: the requested mode did not execute for an eligible
    dispatch.
-   `sparseMissClassification`: the same samples evaluated as thresholded
    center, 3x3, radius-2 cross, radius-2 nine-tap, and full 5x5 decisions.

The audit has its own
`Upscaling::HAM::DevBenchQualityAudit` timer and never writes presentation
color.

## Minimal automation sequence

For each mode:

```text
ham_set_mode -> stabilize -> ham_reset -> bounded profiler capture
             -> preserve profiler result and counters
             -> ham_quality_start -> poll until complete
             -> preserve fidelity result
```
