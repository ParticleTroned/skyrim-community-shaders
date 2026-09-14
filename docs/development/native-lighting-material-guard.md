# Native VR lighting-material draw guard

The guard rejects VR lighting draws that could send invalid diffuse target
indexes to native material setup. Native setup accepts the diffuse texture
sentinel `-1` and target indexes from zero through
`Util::GetRenderTargetCount() - 1`. Implausible material addresses and
access violations while reading the narrow pass/material snapshot are
rejected for both native and custom PBR setup.

All three existing `RenderPassImmediately` call-site hooks check before
particle-light or terrain callbacks. The shared `DrawRenderPassImmediately`
entry also checks before interior-sun state changes or native rendering,
covering deferred terrain replay. Rejection skips the entire intercepted
draw and leaves the material untouched. It does not leave a draw running
after an incomplete `SetupMaterial` call.

Invalid target indexes are rejected when the draw needs native material
setup. Custom PBR setup keeps its existing bounds-checked diffuse-texture
fallback, and its terrain paths may ignore this field. The guard and
actual setup share `TruePBR::UsesCustomMaterialSetup`: the feature must be
loaded and enabled, the raw technique must carry the PBR flag, and the
technique must not be `LODLand` or `LODLandNoise`.

Admission derives that raw technique from the incoming draw technique and
the engine's lighting technique base. The shader's current technique can
still belong to the previous draw. A technique below the base cannot
authorize the PBR exception. Pointer and readability failures remain
rejected for PBR draws. Deferred terrain replay checks current ownership
again, so disabling PBR after queuing cannot bypass native bounds checks.

Valid materials and non-lighting passes retain their existing routing.
SE and AE bypass the guard, including the extra native target slots used
by recent AE versions. This change does not alter the shared render-target
counts or any render-scale policy.

The shared pointer predicate checks address plausibility and alignment;
it does not prove object ownership or lifetime. The guarded read handles
access violations only. Native rendering and feature callbacks execute
outside that exception handler, so unrelated faults remain visible.
Scene/property ownership must still keep the material alive after the
snapshot. This guard contains malformed inputs on the intercepted paths;
it does not identify or repair the writer or lifetime error that produced
them, or cover external callers bypassing those paths.

Warnings use the `[LightingMaterial]` prefix and are limited to one per
rejection reason per process. They contain raw pass/material addresses,
technique, index, whether the index was read, and runtime target count.
No material strings, texture links or virtual methods are read to log a
failure. Reason bits are: invalid pass `1`, shader `2`, property `4`,
material `8`, target index `16`, and unreadable snapshot `32`.

The motivating crash at `SkyrimVR.exe+1338D13` contains
`RAX = RDX = 1861746551` and `RCX = 6 * 1861746551`. The faulting
eight-byte-scaled lookup therefore uses a 48-byte target stride and
addresses the VR target table's SRV field. The log also contains an
unaligned material candidate; stack-scanned decal/PBR objects do not prove
which object caused the corruption. The installed executable is packed,
so the preceding native load instruction has not been independently
verified from its on-disk bytes.

The focused `NativeLightingMaterialGuard` controller test extracts the
production guard, PBR ownership predicate and all four draw entrypoints.
Its fixtures match the accessed engine offsets. It checks bounds, PBR
ownership, pointers whose accessed fields cross a protected-page boundary,
unchanged materials, callback ordering, replay and runtime isolation.
It also checks that unrelated structured exceptions and faults in native
callbacks remain visible outside the narrow snapshot handler.
Controller evidence does not establish in-game CTD resolution. Reproducing
the inventory interaction and checking valid decal/PBR visuals in Skyrim
VR remain necessary runtime validation.
