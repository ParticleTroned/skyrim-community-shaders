# VR Depth-Culling Recovery Telemetry

Balanced temporal recovery runs only after the native result was produced from
a materially different headset pose. The `communityshaders.menu` status result
reports that exceptional path without changing the depth-culling decision.

The `depthCullingTemporal` object contains lifetime counts for coherence misses,
recovery attempts, objects inspected, invalid transforms or motion envelopes,
frustum tests, eligible objects, and promoted objects. It also reports total and
maximum recovery time in nanoseconds plus a fixed histogram with upper bounds at
1, 2, 4, 8, 16, 32, and 64 microseconds.

`set_depth_culling_telemetry_enabled` disables timing and counter collection
without changing the recovery algorithm. `reset_depth_culling_telemetry` clears
the counters only when no render-depth sample is active; otherwise it returns
`depth_culling_telemetry_busy` and makes no partial reset.

## Headset A/B procedure

Use provenance-matched candidate and baseline builds with the same save, pose,
settings, headset runtime, and capture duration. Select a dense scene that
causes coherence misses, then capture Balanced, Performance, and Legacy/native
runs. Record frame-time distributions, miss rate, objects inspected, eligible
and promoted counts, and the recovery-duration histogram. Review both eyes for
missing-object and illumination discontinuities.

Choose the 90 Hz frame-budget threshold before inspecting the results. A result
is not release evidence unless the baseline and candidate build IDs, fixture,
capture window, settings, and visual review are all retained. No headset A/B
result is asserted by this instrumentation change.
