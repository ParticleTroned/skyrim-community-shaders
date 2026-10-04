# VR Depth-Culling Temporal Policy

Skyrim VR renders its GPU OBB occlusion test in one frame and consumes the
result in the next. Head motion between those frames can make an occluded
answer stale and briefly remove geometry that has entered the current view.

CSX keeps the native deferred readback and provides three mutually
exclusive methods:

-   **Advanced** is the default (previously named Balanced). When producer and consumer camera poses fall
    outside a small coherence envelope, CSX derives conservative bounds from
    the native CPU-side OBB transforms, expands them for measured translation
    and rotation, and tests them against the current camera frustum. The motion
    envelope is calculated once per miss. A fixed-capacity heap then selects at
    most 64 occluded objects, prioritizing objects already inside the current
    frustum and then larger native angular coverage.
-   **Legacy** consumes native results without temporal pose capture or
    recovery. The installed dispatch hooks remain as the runtime selector, but
    return without temporal work. This option is off by default and is intended
    for compatibility and A/B comparison rather than as the recommended policy.
-   **Hybrid Hi-Z (experimental)** retains native object registration, OBBs,
    result indices and staging delivery, and replaces depth preparation and
    OBB visibility testing with a conservative stereo depth pyramid. It checks
    exact batch correspondence and each eye's pose before accepting a result.
    Invalidated batches promote every hidden result to visible without the
    Advanced 64-object quota. Unsupported layouts or unavailable resources
    use the native producer with Advanced recovery. See the
    [Hybrid implementation](vr-hybrid-culling.md) for its temporal limitations
    and the unfulfilled performance acceptance requirement.

When native depth culling is disabled, new producer work returns before pose
capture, camera lookup, motion analysis, OBB scanning, or result mutation. The
DevBench status reports that effective state separately from the saved exterior
and interior preferences, so an A/B run can prove that culling was active.
Method changes invalidate the rendering epoch. Any outstanding Hybrid batch
must still be retired safely, promoting hidden results if its epoch is stale.
After culling is enabled, or after switching from Legacy to Advanced,
Advanced waits for one new producer pose before it considers recovery. That
transition frame accepts the native result rather than comparing it with a pose
from an earlier enabled interval.

Advanced recovery uses the OBB data already owned by the engine. It does not
add a GPU readback or replace the native occlusion shader. Its fixed promotion
budget bounds the number of extra objects rendered; their individual draw cost
still depends on scene content. Hybrid replaces GPU visibility production but
does not introduce another GPU-to-CPU readback. The existing native staging map
can still wait for GPU completion.

The Depth Culling group shows independent Exterior and Interior switches,
each with its own Minimum Object Size slider. Both locations are enabled
by default and both thresholds default to 10. The active cell selects the
appropriate switch and threshold on the existing prepass path.

The selector lists Advanced, Hi-Z, and Legacy on one row and is visible
at normal Info logging and in Developer Mode. Each method has a
plain-language tooltip; Hi-Z notes its additional performance cost.
Logging level does not control the active policy.
Save settings to retain the selected method after restarting. Advanced is
the fresh default; changing logging level never resets a saved choice.

Hi-Z shaders are prepared during VR renderer setup, even when Advanced or
Legacy is selected. Switching to Hi-Z reuses that pipeline. Shader reloads
or a graphics-device/context change require preparation again. Depth
textures are allocated when a valid scene needs them. Preparation failure
retains native culling.

Settings store numeric `DepthCullingMethod` (`0` Advanced, `2` Legacy,
`3` Hybrid) and separate
`MinOccludeeBoxExtentExterior` / `MinOccludeeBoxExtentInterior` values.
The compatibility `DepthCullingLegacyMode` boolean remains synchronized with
Legacy. If the method is absent, the boolean initializes it; a present malformed
method falls back to Advanced. Retired numeric value `1` remains unsupported.
Existing shared `MinOccludeeBoxExtent` values initialize both sliders; an
explicit location value takes precedence. Extents are finite and clamped to
0-1000. Configurations containing the old temporal-policy flags but neither
location threshold used an exterior master switch; when that master was
disabled, migration keeps both locations disabled. Older configurations
without temporal-policy flags already used independent switches, so their
saved enable flags are preserved. New configurations can independently keep
interior culling enabled while exterior culling is disabled.

The Performance policy, its setting and its DevBench setter are removed.
Old Performance-only settings fall back to Advanced. A saved Legacy setting
is retained. The `balanced` machine identifier and the surviving numeric
mode values remain stable for historical telemetry consumers; `balanced`
now corresponds to the Advanced menu label.

## Runtime safety

The culler layout and hook offsets are specific to Skyrim VR 1.4.15. Hook
installation fails closed on another runtime or when an expected call
instruction is not present. SE and AE do not install the Hybrid hooks.
Advanced leaves native visibility unchanged when its recovery inputs are
unavailable. Hybrid validates its GPU resource contracts before taking over
production and returns to native production on failure. Invalid Hybrid
depth/bounds produce visible results; stale Hybrid batches are made visible
when a valid native result array is available.

## DevBench

Advanced and Hybrid timing, counters, histogram storage and status/reset controls are
compiled only with `DEVBENCH_BRIDGE_ENABLED`. Production builds retain the
same pose validation, recovery selection and promotion budget without this
diagnostic work. DevBench builds retain the telemetry toggle for controlled
measurements; switching it does not change culling behavior. Telemetry remains
enabled by default in DevBench builds. A DevBench-enabled benchmark AIO therefore
still collects it unless `set_depth_culling_telemetry_enabled` is called with
`enabled: false`.

`communityshaders.menu` exposes the current method and recovery counters in its
status response. Use `set_depth_culling_method` with `method: "balanced"`,
`"legacy"` or `"hybrid"`. The historical `balanced` identifier selects Advanced.
Use `set_depth_culling_legacy_mode` with boolean `enabled`:
true selects Legacy and false selects Advanced. Use
`set_depth_culling_settings` with a nonempty `depthCulling` object containing
any of `exteriorEnabled`, `interiorEnabled`, `exteriorMinExtent` and
`interiorMinExtent` to edit the location controls. The complete request is
validated before application; unknown fields, wrong types, nonfinite values
and extents outside 0-1000 are rejected. Both actions execute on the main
thread, mark settings dirty and require the normal settings save to persist.
These controls do not change the logging level.

Status reports the two enable flags, their configured extents, the selected
mode and the effective culling state. Cumulative miss and promotion counters
are retained across mode changes. `depthCullingTemporal.hybrid` additionally
reports producer state, submitted/accepted/invalidated/fallback batches,
promoted objects, unreadable batches, the last object count, CPU stage
timings, and effective-backend/failure reasons. The existing telemetry
toggle controls both methods, and reset clears their counters together or
returns busy without a partial reset. Operational backend and failure
reasons remain available with telemetry disabled. See the
[telemetry contract](vr-depth-culling-recovery-telemetry.md) for fields,
sample admission and profiler behavior.

See the [evidence record](vr-depth-culling-temporal-evidence.md) for the linked
regression history, live Skyrim VR layout observations, and local validation.
