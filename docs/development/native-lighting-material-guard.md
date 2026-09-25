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
outside that snapshot handler; native draw faults are not swallowed by
the guard. The particle callback retains its separate, existing exception
fallback.
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

Warning formatting and once-per-reason warning state live in a non-inlined
rejection helper. Successful admission retains the same protected reads
and decisions without bringing the warning implementation into its caller.
This is a code-generation optimization candidate, not a measured frame-time
saving. It does not remove the material guard's full sampled CPU cost.

Admission is never cached by material address. The second immediate hook
rechecks through the shared draw entry after particle/terrain callbacks
and between terrain double draws. Deferred terrain replay uses that same
entry. The first and third hooks retain their pre-particle check; they do
not add a second check after that callback. The current particle-light
path handles effect materials and does not mutate lighting materials.
These boundaries do not establish protection against concurrent material
mutation or replace the ownership requirement above.

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
Callback-mutation fixtures verify the second hook's rejection after
particle/terrain callbacks and between its terrain submissions. Concurrent
rejection fixtures exercise the production guard with eight callers and
verify that every malformed draw is rejected while one warning is emitted
for the reason. Valid draws emit no warnings.
Controller evidence does not establish in-game CTD resolution. Reproducing
the inventory interaction and checking valid decal/PBR visuals in Skyrim
VR remain necessary runtime validation.

## Cold rejection-path review (2026-09-16)

The production change only extracts the existing warning body. It keeps
every guard, callback boundary, SEH filter and native call argument intact.
The helper logs copied scalar values without dereferencing rejected
objects. Its atomic reason mask still permits one warning per reason
across callers. Bounds and PBR ownership continue to use the existing
shared helpers; the test extracts production code rather than duplicating
the decision algorithm.

Review corrected an overly broad description of callback revalidation and
added concurrent warning coverage. Callback-mutation cases preserve the
second hook's existing rechecks instead of removing them as redundant.
No material-address admission cache or additional hot-path probe was added.
Frame-time savings and in-game visual/crash behavior remain unmeasured for
this candidate.

Validation of the reviewed working-tree candidate:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target native_lighting_material_guard_test -- /m:1
ctest --test-dir build/ALL -C Release -R '^NativeLightingMaterialGuard$' --output-on-failure --no-tests=error --timeout 300
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300 --output-log build/validation/material-guard-review-20260916-ctest.log
pwsh ./tools/pre-commit.ps1 run --files src/Hooks.cpp tests/native_lighting_material_guard_test.cpp docs/development/native-lighting-material-guard.md
pwsh ./tools/git.ps1 diff --check
```

The universal SE/AE/VR Release DLL and both test groups built successfully.
The focused test passed; full CTest passed 124/124 with no failed or skipped
tests in 60.42 seconds. Scoped formatting and diff checks passed. This was
local build/controller/shader validation, not an in-game performance run.
