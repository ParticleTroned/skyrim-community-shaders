# VR HMD-mask A/B measurement

The versioned `communityshaders.renderscale` DevBench tool controls the live
HMD-mask implementation and exposes its measurement contract. Every call should
include the `expectedBuildId` returned by DevBench discovery or status so an
automation run fails closed if MO2 loaded a stale DLL.

The three mode names are:

- `robust_depth_5x5`: current thresholded, two-pixel-dilated depth scrub.
- `legacy_exact_zero`: the inexpensive exact-zero, single-depth-sample kernel.
- `reduced_resolution_mask`: builds the robust mask at vendor-input resolution,
  sanitizes temporal input, then reuses one mask lookup for the final scrub.

Use `ham_set_mode` to change mode live. The change invalidates cached masks,
requests a temporal-history reset, and logs at Info. It does not require Debug or
Trace logging.

## Performance capture

Call `ham_status` first. Its `performance` object identifies the existing
`communityshaders.profiler_api` tool, the `Upscaling::HAM::` prefix, and every
stable timer name. Capture profiler history only while `qualityCapture.active` is
false. Repeated left/right-eye passes with one name are accumulated by the
profiler into one frame sample.

A controlled comparison should:

1. Apply one HAM mode and wait for the same scene, render-scale profile, headset
   resolution, frame limit, and thermal state to settle.
2. Call `ham_reset`.
3. Start a bounded `communityshaders.profiler_api` capture filtered by
   `Upscaling::HAM::`.
4. Preserve that capture before changing mode.
5. Repeat the same warm-up and capture for each mode.

Sum the active input/build and final timers for the complete HAM cost. Keep
`VerifiedRepairRobust` separate: it represents robustness work during verified
post-load repair, not an ordinary-frame regression. The dispatch and fallback
counters in the `ham_status` response's `status.robustness` object establish whether the requested candidate
actually ran throughout a timing sample.

## Fidelity capture

Fidelity is a separate bounded GPU audit and must not share a run with performance
measurement:

```json
{"action":"ham_quality_start","maxFrames":60,"expectedBuildId":"<build-id>"}
```

Poll `ham_quality_status` until `qualityCapture.complete` is true. The capture
automatically stops after both eyes have been observed for the requested number
of frames. `ham_quality_stop` ends it early. `ham_reset` clears the completed
quality result and all HAM robustness counters.

The audit samples a stable one-in-four display-domain pixel grid and compares the
candidate clear decision with the production robust reference. It reports:

- `falseNegatives`: robust would clear but the candidate would retain color;
  these are the primary hidden-area leakage and temporal-ghosting risk.
- `falsePositives`: candidate would clear where robust would retain color;
  these are potential visible-edge loss.
- `invalidMaskLookups`: reduced-mask contract or bounds failures.
- `skippedDispatches`: requested mode did not execute, normally because the
  candidate fell back to the robust implementation.

The quality audit has its own `Upscaling::HAM::DevBenchQualityAudit` timer and
explicitly reports that it contaminates performance timing. This separation is
intentional: the audit recomputes the robust 5x5 reference without altering the
presented color.

## Minimal automation sequence

For each mode, the recommended order is:

```text
ham_set_mode -> warm up -> ham_reset -> profiler capture -> preserve profiler result
             -> ham_quality_start -> poll ham_quality_status -> preserve HAM result
```

Reject a run if the producer Build ID, resolution/profile conditions, effective
dispatch counters, or fallback counts differ from the intended experiment.
