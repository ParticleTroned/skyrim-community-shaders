# VR Depth-Culling Temporal Policy

Skyrim VR renders its GPU OBB occlusion test in one frame and consumes the
result in the next. Head motion between those frames can make an occluded
answer stale and briefly remove geometry that has entered the current view.

CSX keeps the native asynchronous readback and provides two mutually
exclusive policies:

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

When native depth culling is disabled, both temporal hooks return before pose
capture, camera lookup, motion analysis, OBB scanning, or result mutation. The
DevBench status reports that effective state separately from the saved exterior
and interior preferences, so an A/B run can prove that culling was active.
After culling is enabled, or after switching from Legacy to Advanced,
Advanced waits for one new producer pose before it considers recovery. That
transition frame accepts the native result rather than comparing it with a pose
from an earlier enabled interval.

The recovery uses the OBB data already owned by the engine. It does not add a
GPU readback or replace the native occlusion shader. The fixed promotion budget
bounds the number of extra objects rendered; their individual draw cost still
depends on scene content.

The Depth Culling group shows independent Exterior and Interior switches,
each with its own Minimum Object Size slider. Both locations are enabled
by default and both thresholds default to 10. The active cell selects the
appropriate switch and threshold on the existing prepass path.

The Advanced/Legacy selector is visible only in Developer Mode (Debug or
Trace logging). Its visibility does not control the active policy. A user
can select Legacy, save settings, then return to Info logging: Legacy stays
active and remains selected after restarting. Advanced is the fresh default;
leaving Developer Mode never resets a saved choice.

Settings store `DepthCullingLegacyMode` and separate
`MinOccludeeBoxExtentExterior` / `MinOccludeeBoxExtentInterior` values.
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
installation fails closed on another runtime or when either expected call
instruction is not present. Missing producer poses, invalid motion, invalid
counts or selectors, null engine buffers, and malformed OBB values leave the
native visibility results unchanged.

## DevBench

Recovery timing, counters, histogram storage and status/reset controls are
compiled only with `DEVBENCH_BRIDGE_ENABLED`. Production builds retain the
same pose validation, recovery selection and promotion budget without this
diagnostic work. DevBench builds retain the telemetry toggle for controlled
measurements; switching it does not change culling behavior. Telemetry remains
enabled by default in DevBench builds. A DevBench-enabled benchmark AIO therefore
still collects it unless `set_depth_culling_telemetry_enabled` is called with
`enabled: false`.

`communityshaders.menu` exposes the current policy and recovery counters in its
status response. Use `set_depth_culling_legacy_mode` with boolean `enabled`:
true selects Legacy and false selects Advanced. Use
`set_depth_culling_settings` with a nonempty `depthCulling` object containing
any of `exteriorEnabled`, `interiorEnabled`, `exteriorMinExtent` and
`interiorMinExtent` to edit the location controls. The complete request is
validated before application; unknown fields, wrong types, nonfinite values
and extents outside 0-1000 are rejected. Both actions execute on the main
thread, mark settings dirty and require the normal settings save to persist.
Neither changes the logging level.

Status reports the two enable flags, their configured extents, the selected
mode and the effective culling state. Cumulative miss and promotion counters
are retained across mode changes; last-recovery fields report zero whenever
Advanced recovery is inactive.

See the [evidence record](vr-depth-culling-temporal-evidence.md) for the linked
regression history, live Skyrim VR layout observations, and local validation.
