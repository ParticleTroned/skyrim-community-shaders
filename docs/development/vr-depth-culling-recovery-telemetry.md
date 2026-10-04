# VR Depth-Culling Telemetry

Advanced temporal recovery runs only after the native result was produced
from a materially different headset pose. Hybrid Hi-Z also records its
preparation, submission and deferred-result validation. The
`communityshaders.menu` status result reports both methods without changing
the depth-culling decision. Their timing and counter collection is compiled
only with `DEVBENCH_BRIDGE_ENABLED`.

The `depthCullingTemporal` object contains counts since the last reset for
coherence misses, recovery attempts, objects inspected, invalid transforms or motion envelopes,
frustum tests, eligible objects, and promoted objects. It also reports total and
maximum recovery time in nanoseconds plus a fixed histogram with upper bounds at
1, 2, 4, 8, 16, 32, and 64 microseconds.

Each sampled coherence miss contributes one duration observation, including an
early return. Recovery attempts count misses with a valid shared motion envelope;
`objectsInspected` includes objects already marked visible by the native culler.
Invalid motion envelopes count rejected shared envelopes and per-object motion
expansions. `frustumTests` counts calls, so one object can contribute two tests.
Eligible objects are retained when a policy change cancels promotion; promoted
objects count only result writes. Histogram bounds are inclusive, and the final
`null` bound denotes overflow. Status reads are thread-safe, but fields can span
different in-flight samples or a reset; use a quiescent capture for exact counter comparisons.

`set_depth_culling_telemetry_enabled` jointly controls Advanced and Hybrid
timing and counter collection without changing either algorithm.
`reset_depth_culling_telemetry` clears both methods together only when no
participating sample is active; otherwise it returns
`depth_culling_telemetry_busy` and leaves every counter untouched. Both
methods use the same writer gate, including their final publications.
Disabling prevents admission of new samples; an already admitted sample finishes
and publishes normally. Mode or culling-enable changes can still clear the
`last*` diagnostics. Disable telemetry and retry a busy reset before collecting a
fresh window; re-enable it to start that window.

## Hybrid status

`depthCullingTemporal.hybrid` includes submitted, accepted, invalidated,
unreadable and native-fallback batch counts, promoted-object counts, and
the last object count. Unreadable batches identify results whose native
batch contract could not be inspected; they must not be counted as an
accepted visibility result. The `cpuTimings` object contains `prepare`,
`dispatch` and `readback`, each with `samples`, `totalNanoseconds` and
`maximumNanoseconds`. These measure CPU scope durations, including early
returns; they do not measure GPU execution time.

`effectiveBackend`, `fallbackReason` and `historyRejectionReason` describe
operational state independently of telemetry enablement. The effective
backend distinguishes `disabled`, `native`, `pending` and `hybrid`, using
the current method and culling epoch. Switching methods cannot present an
earlier Hybrid submission as the current backend. A reset clears sampled
counters and timings without changing visibility, backend selection, or
the operational failure reasons.

The existing profiler controls collect GPU pass timings for
`VRHybridCulling::BuildHierarchy` and `VRHybridCulling::Visibility`.
Turning off depth-culling telemetry does not change an independently
requested profiler capture. Method changes made through the menu, settings
load, or DevBench share the same Info-level transition log. The DevBench
method setters return the effective method and `persisted: false`; normal
settings save is required for persistence. Their responses retain the
standard producer Build ID and expected-build validation.

## Headset A/B procedure

Use provenance-matched candidate and baseline builds with the same save, pose,
settings, headset runtime, and capture duration. Select a dense scene that
causes coherence misses, then capture Advanced, Hybrid and Legacy/native
runs. Record frame-time distributions, miss rate, objects inspected,
eligible and promoted counts, recovery-duration histograms, Hybrid CPU
stage timings, and fallback/rejection reasons. Review both eyes for
missing-object and illumination discontinuities.

Choose the 90 Hz frame-budget threshold before inspecting the results. A result
is not release evidence unless the baseline and candidate build IDs, fixture,
capture window, settings, and visual review are all retained. No headset A/B
result is asserted by this instrumentation change.
