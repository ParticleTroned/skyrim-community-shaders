# Deferred G-buffer performance evidence

## Observed boundary

A controlled RX 7900 XT null-HMD comparison on 2026-08-30 localized a large
GPU-time difference to the interval in which Skyrim geometry rendered with a
fourth CSX deferred pixel output active.

| Treatment            | Geometry mean (ms) |
| -------------------- | -----------------: |
| Compact core, 4 RTVs |             4.1303 |
| Compact core, 3 RTVs |             2.7548 |

The 1.3756 ms delta is runtime timing evidence for the complete fourth-output
path. It is not proof of pure bandwidth or render-target export cost: omitting
an output can also remove its producer instructions and data dependencies.
Changing the fourth target's transport between integer and UNORM did not
materially change the interval, and simplifying its packing recovered only
about 0.043 ms.

The authoritative host evidence set is named
`deferred-gbuffer-performance-20260830`. Its summaries contain 120 retained
samples after five warm-up frames per treatment. They predate the current
mandatory context fingerprint: source commit, dirty state, compiler,
shader-cache ABI, warm-up, and sample count are present, but the artifact hash
is empty and manifest verification was delegated. The timing is therefore a
controlled diagnostic sketch rather than a provenance-complete performance
qualification. A public evidence package should replace the host-local set
with a content-addressed manifest reference.

## Mapping interpretation

The engine geometry draws do not own the additional target. CSX installs an
output-merger state and modified pixel-output signature before those draws.
The useful relationship is therefore:

```text
active deferred consumers
  -> CSX deferred topology decision
  -> exact OM target set and blend/write-mask state
  -> modified Skyrim/CSX pixel-shader output signature
  -> engine geometry draws
  -> measured Deferred::GeometryInterval
```

Do not promote the measured delta into the static Skyrim engine map. The static
map can identify render boundaries and shader families; target count, consumer
demand, shader bytecode, state, and timing are capture-local observations.

## Adjacent capture requirements

A bounded structural capture adjacent to each profiler treatment should retain:

-   deferred demand mask and named reasons;
-   required, configured, and active RTV counts;
-   exact RTV resource/view identities, formats, dimensions, and subresources;
-   output-merger blend state and per-target write masks;
-   pixel-shader bytecode hash and output-signature identity;
-   draw count, shader family, technique, and representative coverage;
-   shader compile-context and cache provenance;
-   the treatment's build, scene, runtime route, resolution, and frame-pacing
    context; and
-   the corresponding `Deferred::GeometryInterval` capture identity.

The structural capture should run immediately before or after the profiler
sample, not concurrently with it. High-density render-map collection can alter
CPU and frame-pacing conditions even when its structural observations remain
valid.

## Next discriminating observations

The next matrix keeps the target bound while changing constant versus material
dependent output, then compares write-mask, compatible width/format, and
output-signature presence. The optional fifth SSGI target is measured
separately. These observations distinguish producer cost, output export/state,
and topology specialization before any change is described as an optimization.
