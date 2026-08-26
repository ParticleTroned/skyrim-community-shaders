# VR Depth-Culling Temporal Policy

Skyrim VR renders its GPU OBB occlusion test in one frame and consumes the
result in the next. Head motion between those frames can make an occluded
answer stale and briefly remove geometry that has entered the current view.

CSX keeps the native asynchronous readback and provides three mutually
exclusive policies:

-   **Balanced** is the default. When producer and consumer camera poses fall
    outside a small coherence envelope, CSX derives conservative bounds from
    the native CPU-side OBB transforms, expands them for measured translation
    and rotation, and tests them against the current camera frustum. The motion
    envelope is calculated once per miss. A fixed-capacity heap then selects at
    most 64 occluded objects, prioritizing objects already inside the current
    frustum and then larger native angular coverage.
-   **Performance Mode** accepts the native one-frame-late result when the
    envelope is missed. Its consumer hook returns before camera lookup, motion
    analysis, or the bounded recovery scan, but it can expose a one-frame
    missing-object artifact during head motion. Producer-pose capture remains
    active so an in-game switch back to Balanced has a valid prior-frame pose.
-   **Legacy** consumes native results without temporal pose capture or
    recovery. The installed dispatch hooks remain as the runtime selector, but
    return without temporal work. This option is off by default and is intended
    for compatibility and A/B comparison rather than as the recommended policy.

When native depth culling is disabled, both temporal hooks return before pose
capture, camera lookup, motion analysis, OBB scanning, or result mutation. The
DevBench status reports that effective state separately from the saved exterior
and interior preferences, so an A/B run can prove that culling was active.
After culling is enabled, or after switching from Legacy to Balanced,
Balanced waits for one new producer pose before it considers recovery. That
transition frame accepts the native result rather than comparing it with a pose
from an earlier enabled interval.

The recovery uses the OBB data already owned by the engine. It does not add a
GPU readback or replace the native occlusion shader. The fixed promotion budget
bounds the number of extra objects rendered; their individual draw cost still
depends on scene content.

The in-game Depth Culling group keeps the exterior master switch, the interior
switch, a mutually exclusive three-way temporal policy selector, and the
minimum occludee extent together. Balanced is the default and Legacy is
off by default. Interior culling is enabled in fresh defaults; an explicitly
saved disabled value is still respected. The current exterior-master and
interior activation policy remains in force for all three temporal policies.
If a manually edited settings file enables both optional policies, CSX clears
the conflict and falls back to Balanced.

## Runtime safety

The culler layout and hook offsets are specific to Skyrim VR 1.4.15. Hook
installation fails closed on another runtime or when either expected call
instruction is not present. Missing producer poses, invalid motion, invalid
counts or selectors, null engine buffers, and malformed OBB values leave the
native visibility results unchanged.

## DevBench

`communityshaders.menu` exposes the current policy and recovery counters in its
status response. Use `set_depth_culling_performance_mode` or
`set_depth_culling_legacy_mode` with a boolean `enabled` argument to switch
modes on the main thread. The actions are intended for controlled same-build
A/B measurements. Cumulative miss and promotion counters are retained across
mode changes; the last-recovery fields report zero whenever Balanced recovery
is inactive.

See the [evidence record](vr-depth-culling-temporal-evidence.md) for the linked
regression history, live Skyrim VR layout observations, and local validation.
