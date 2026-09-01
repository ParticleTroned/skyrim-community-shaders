# Native VR menu pointer overlay

VR Menu Mouse Fix makes Skyrim's existing `UI Pointer Geometry` visible.
When Render Scale presents menus at display resolution, their final layer
covers the pointer in the reduced scene. This correction captures only the
engine-owned `UIPointerGeo` and places its colour above the final menu.

The original scene draw always runs, including its native depth and MRT
writes. The additional UI copy intentionally ignores scene depth and scene
colour processing. It can appear through world objects and its colour can
differ from the scene copy. Menu capture, resolution and transaction policy
are unchanged. SE/AE and VR with Render Scale disabled use the native path.

## Ownership and failure behavior

Capture uses the existing accepted-draw geometry scope, including its
suppression and observer-replay guards. A matching name is insufficient.
The currently bound vertex shader must expose the Effect `COLOR0` float-RGB
signature at register 3. Reflection results retain the shader and are cached;
the lookup of replacement bytecode never compiles or schedules shaders.

The replay is synchronous while the native geometry and constants are live.
It preserves all eight render targets, pixel shader and class instances,
blend/depth state, viewports and scissors. It rejects unsupported shader
stages, stream output, predication and graphics UAVs. Depth is disabled only
for the UI copy. Draw-time jitter and viewport/scissor scaling preserve
stereo placement at display resolution.

The private layer is valid only for its frame and render-contract generation.
A rejected capture invalidates that frame. Hidden ancestors and changed
pointer identity suppress presentation. The first final consumer freezes
visibility for both eyes; subsequent draws cannot alter the published layer.
Resource reset releases the capture resources and shader validation state.
Allocation failures are latched for the failed device and size until reset
or a layout change. Exceptions disable capture until resource reset, avoiding
repeated failing shader creation on the render thread. Unsupported or failed
capture leaves native rendering and menu composition available.

## Frame cost and diagnostics

Ordinary geometry fails the identity check before any UI lookup or graphics
inspection. Capture resources and shader permutations are reused. The
pointer is sampled together with the menu in the existing final draw, using
premultiplied alpha, for each eye and the desktop menu path. There is no
additional full-screen composition draw. Sampling clamps the pointer to the
current eye's texel centres to avoid colour leaking across the stereo seam.

The private SRV is bound only at pixel slot 1 by `CompositeBinding`, which
restores the slot after the enclosing compositor restores its output state.
The SRV must never escape into another pipeline or be retained by a caller.
This ownership contract avoids scanning all six shader stages for aliases on
every capture. The pointer still requires a full-resolution texture clear,
one small geometry replay and an extra sample in the final menu pass.

There are no pointer diagnostic counters, pipeline dumps, submission traces
or recurring log messages at Info or Debug. Exceptional capture failures use
the existing warn-once path. Performance savings described here are removed
operations, not a measured headset frametime claim.

## Validation

`VRMenuPointer` tests production admission and presentation functions:
identity, nested scopes, observer isolation, runtime/context rejection,
outside-interface capture, menu closure, ancestor visibility and stereo
decision freezing. `VRMenuPointerCapture` uses D3D11 WARP and the production
shaders to check stereo pixels, depth independence, native MRT preservation,
state restoration, premultiplied menu composition, seam clamping, jitter,
freshness and unsupported-pipeline rejection.

Headset validation on both branches must check both eyes, Render Scale
on/off, affected menus and their closure; `main-vr-nr` also needs NR enabled
and disabled. The earlier
separate-pass build was confirmed visually by the reporter; this combined
pass port still requires its own runtime and frametime evidence.
