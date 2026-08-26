# VR Depth-Culling Temporal Policy

Skyrim VR renders its GPU OBB occlusion test in one frame and consumes the
result in the next. Head motion between those frames can make an occluded
answer stale and briefly remove geometry that has entered the current view.

CSX keeps the native asynchronous readback and provides two policies:

-   **Balanced** is the default. When producer and consumer camera poses fall
    outside a small coherence envelope, CSX derives conservative bounding
    spheres from the native CPU-side OBB transforms, expands them for measured
    translation and rotation, and tests them against the current frustum. It
    promotes at most 64 high-risk occluded objects, prioritizing objects already
    inside the current frustum and then larger angular coverage.
-   **Performance Mode** accepts the native one-frame-late result when the
    envelope is missed. It avoids the bounded recovery scan but can expose a
    one-frame missing-object artifact during head motion.

The recovery uses the OBB data already owned by the engine. It does not add a
GPU readback or replace the native occlusion shader. The fixed promotion budget
bounds the number of extra objects rendered; their individual draw cost still
depends on scene content.

The in-game Depth Culling group keeps the exterior master switch, the interior
switch, Performance Mode, and the minimum occludee extent together. Interior
culling is enabled in fresh defaults; an explicitly saved disabled value is
still respected.

## Runtime safety

The culler layout and hook offsets are specific to Skyrim VR 1.4.15. Hook
installation fails closed on another runtime or when either expected call
instruction is not present. Invalid counts, buffer selectors, pointers, or OBB
values leave the native visibility results unchanged.

## DevBench

`communityshaders.menu` exposes the current policy and recovery counters in its
status response. Use the `set_depth_culling_performance_mode` action with a
boolean `enabled` argument to switch modes on the main thread. The action is
intended for controlled same-build A/B measurements.

See the [evidence record](vr-depth-culling-temporal-evidence.md) for the linked
regression history, live Skyrim VR layout observations, and local validation.
