# CSX-only ImGui VR Helper integration

This branch implements optional CSX render hosting for ImGui VR Helper API
`006`. Its CSX base is `main-VR` commit
`dab1874a76fd39175dcefdc52110ba69d7284e12`. The API-only dependency is pinned
to helper implementation
[`ceb7991f499f3335318aedd3bdc0e8735d7b8380`](https://github.com/ParticleTroned/imgui-vr-helper/commit/ceb7991f499f3335318aedd3bdc0e8735d7b8380).

The producer, adapter and helper interface are implemented. CPU tests and
universal DLL builds with and without DevBench passed. No game, HMD, runtime DevBench or
performance qualification has run, and no binaries have been deployed.

## Compatibility and ownership

CSX negotiates interface `006` after SKSE listeners are available, registers
an explicit host token, and changes hosting ownership at an outer native
stereo boundary. Helper module presence alone does not activate hosting.
Missing helpers or helpers without `006` leave their existing rendering
path and CSX submission behavior intact. Existing helper client interfaces
`001` through `005`, client registration and input ownership remain intact.
Other helper hosts and non-VR CSX runtimes do not use this adapter.

Content demand comes from the registered helper. An absent, disabled or
empty host performs no depth-copy or presentation-copy graphics work.
Activation is independent of the CSX menu's visibility.

For Floating Subtitles, users co-install the paired CSX and ImGui VR Helper
builds alongside Floating Subtitles and its normal dependencies. No helper
menu, toggle, focus request, demo or setup step is required. The helper's
own settings UI remains optional and retains its existing behavior.

CSX refreshes hosted content demand and installs the required Submit hook
from its unconditional VR Present lifecycle before drawing any menu.
Skipping CSX overlay rendering therefore does not stop subtitle updates or
depth-capture requests. External helper callbacks and world quads do not
require the helper's own settings UI to be visible.

The existing CSX OpenVR Submit path remains authoritative for the selected
payload, retained resources, guard decisions and presentation accounting.
Hosting substitutes a successfully prepared color texture at eligible
final-output paths. Extended payloads, unsupported bounds and unsupported
texture contracts retain their original path.

## Captured scene and depth

`Globals::CacheFramebuffer` records the engine's source camera when its
per-frame constants are uploaded. While content is requested, the capture
freezes both eyes, room origin/rotation/scale, near/far planes, dynamic
active extent and temporal route. `Deferred::CopySceneDepth` then retains
its completed opaque native depth in a named, separately owned texture.
New world/opaque passes invalidate prior publication. A bounded surface
pool never overwrites pixels retained by an outstanding snapshot.

The snapshot is acquired only for its exact frame, render thread, device
and completed resource-publication generation. Texture dimensions, view
kind, mip, sample count and source device are queried at the producer
boundary. Full-eye active rectangles refer to the actual retained atlas;
vendor guide textures and foveated ROI depth are not used as occlusion
sources. In particular, `vrIntermediateLinearDepth` contains native device
depth despite its name.

Camera conversion uses double precision, row-major matrices and row-vector
multiplication. Shader column-vector matrices are transposed at capture.
Room origin is retained separately and subtracted before transforming
world anchors. Skyrim units are converted through the captured room scale
to tracking-space metres. The source engine camera already contains VRIK
camera motion, so capture does not apply a second VRIK offset.

Depth always uses the captured jittered source projection. Reconstructed
and TAA output uses the captured unjittered projection; raw native output
uses the source projection. The depth comparison uses positive axial
metres, with native forward-Z coefficients derived from the same near/far
planes and unit conversion. Invalid projections, singular transforms,
out-of-eye coordinates and unsupported depth views fail closed.

Projection validation derives the source projection from the frozen
inverse-view and view-projection matrices, matching CSX's existing
reprojection convention. The redundant raw `CameraProj` field does not
determine admission; it can differ from the projection that produced the
captured scene. View/inverse-view consistency and native depth coefficients
remain validated.

## Stereo composition and failure behavior

`ImGuiVRHelperHostPolicy` admits a complete current world pair or rejects
it before either eye is written. It checks the pair token, compositor
cycle, frame, thread, resource generation, device, color contract, camera
geometry and both depth rectangles. Occlusion-depth retention is separate
from the OpenVR payload lease; a color-only payload can have independently
retained helper depth. Color/depth aliasing, inconsistent shared views and
overlapping shared eye rectangles are rejected.

Native output must match the outer native submit source. Classic
DLAA/DLSS/FSR output additionally requires the existing successful
main-pass vendor frame and known final texture identity. Render-scale
vendor output is composed only after both eyes have current completed
vendor evidence. The peer is evaluated through the existing vendor cache
before either scratch image is copied, with save/load, protected and
unproven world routes excluded. Successful vendor-frame evidence is
available without a DevBench build.

Both eyes are composed into private RTV-capable scratch textures before
either texture is exposed to Submit. A failed eye discards the pair.
Matched outer-boundary checks isolate nested submits, and submitting an
undecorated eye prevents a later eye from starting composition. Resource
generation, source identity and bounds are checked again before using a
prepared eye. Helper rendering uses deterministic D3D11 state restoration.

Missing scene evidence never restores standalone world rendering while
CSX hosting is enabled. A UI-only fallback freezes the current OpenVR
display camera for both eyes, sets `worldLayerEnabled = 0`, and supplies no
depth source. It permits eligible HUD/panel composition without asserting
world-depth provenance. If that camera or final target is unavailable,
the original selected output is preserved. Existing load, keepalive,
device-loss and submission guards remain authoritative.

Production builds record `[ImGuiVRHelperHost]` status samples in
`CommunityShaders.log`. Samples include connection and ownership, content
layers, pair identity, composition/rejection counts, capture status and
the latest composition result. Changes are sampled at most once every
five seconds, with a thirty-second heartbeat while content is requested.
`awaiting_current_scene_or_display_camera` means no usable camera reached
the host; `awaiting_composition` means a camera was acquired but the submit
path did not complete composition. `composed_ui_only` does not mean world
subtitles were drawn. Capture status is recorded alongside the pair's
result so a later producer update does not change its interpretation.

## DevBench session controls

The `communityshaders.imgui_vr_helper` tool is registered in DevBench
builds. These controls are session-only; they do not add persistent user
settings.

```json
{ "action": "status" }
```

Status includes connection/activation, requested controls, content layers,
pair/frame/cycle/generation, composition/rejection counts, the latest
reason/result and producer capture status.

```json
{ "action": "configure", "enabled": false }
```

`enabled` defaults to `true`. Changes take effect at the next stereo
boundary. Setting it to `false` releases explicit hosting and restores
the helper's ordinary standalone behavior.

```json
{ "action": "configure", "enabled": true, "depthComparison": false }
```

`depthComparison` defaults to `true`. Setting it to `false` disables world
depth discard for diagnosis while retaining scene admission and stereo
composition. Restore it to `true` after the comparison.

## Validation evidence

### Automatic startup with menus closed, 2 October 2026

The original adapter called its demand tick only from `SubmitOverlayFrame`,
which is skipped when CSX has no UI to draw. That could prevent startup of
scene capture and hook installation with menus closed. The tick and
helper-required hook installation now run in the unconditional VR Present
lifecycle, independently of menu drawing.

`ImGuiVRHelper_render_lifecycle` extracts and executes the actual Present
lifecycle with a closed-menu fixture. It verifies demand changes and hook
installation before the skipped UI, reopening without duplicate lifecycle
work, helper absence, missing D3D context and non-VR isolation. The ten
focused tests pass. Production package receipts record the exact build
and archive identity; headset verification remains outstanding.

### Subtitle camera regression, 2 October 2026

A tester reported invisible floating subtitles with HUD fallback still
visible when facing away from the speaker. No logs or captured matrices
from that other system were available for this correction; the exact
runtime trigger remains unconfirmed. The source review found a redundant
raw-projection admission requirement inconsistent with CSX's established
camera derivation. No helper-side change was indicated by the paired audit.

The new regression first failed in both ordinary and `/fp:fast` builds
with `Redundant projection rejected the rendered camera`. After deriving
projection from inverse-view and view-projection, both pass. Another case
uses the captured engine float-precision matrices already exercised by
`CameraReprojection`; synthetic stereo, jitter, room-scale and malformed
depth cases remain covered.

```powershell
pwsh ./tools/cmake.ps1 --build build/helper-host-tests --config Release --parallel 4
ctest --test-dir build/helper-host-tests -C Release --output-on-failure
```

All nine tests passed. Logs are preserved under
`build/validation/imgui-vr-helper-subtitle-fix/`. The revised production
package's manifest and receipt record its DLL build and archive checks
separately. No headset or game verification of this correction has run.

### Original implementation

Executed on 1 October 2026 with MSVC, Visual Studio 2026 and Windows SDK
`10.0.28000.0`. The isolated local harness includes the maintained
`tests/imgui_vr_helper.cmake` registration and the existing scene-depth and
native-boundary source-extraction fixtures. It uses C++23, `/W4 /WX /EHsc`
and Release optimization.

```powershell
pwsh ./tools/cmake.ps1 -S build/helper-host-tests-source -B build/helper-host-tests
pwsh ./tools/cmake.ps1 --build build/helper-host-tests --config Release --parallel 4
ctest --test-dir build/helper-host-tests -C Release --output-on-failure
```

All nine tests passed:

| Test                                   | Result |
| -------------------------------------- | ------ |
| `ImGuiVRHelper_scene_packet`           | Passed |
| `ImGuiVRHelper_scene_packet_FastMath`  | Passed |
| `ImGuiVRHelper_host_policy`            | Passed |
| `ImGuiVRHelper_host_policy_FastMath`   | Passed |
| `ImGuiVRHelper_scene_capture`          | Passed |
| `ImGuiVRHelper_scene_capture_FastMath` | Passed |
| `SceneDepth_plain`                     | Passed |
| `SceneDepth_devbench`                  | Passed |
| `VRRelatchNativeBoundary`              | Passed |

The same nine targets were also built through the full `build/ALL`
configuration with `BUILD_CONTROLLER_TESTS=ON`. The maintained registration
passed independently of the local harness:

```powershell
ctest --test-dir build/ALL -C Release -R '^(ImGuiVRHelper_|SceneDepth_|VRRelatchNativeBoundary)' --output-on-failure
```

The configure, build and result logs are retained under
`build/validation/imgui-vr-helper/registered-tests-{configure,build,results}.log`.

The capture fixture extracts the actual producer math and checks it
against analytic stereo projections, asymmetric frusta, jitter, rotated
rooms, room scales 0.5/1/2, large origins, axial metre depth and malformed
producer metadata. Admission fixtures cover stale identities, invalid
frame sentinels, aliases, independent depth retention, singular matrices
and immutable packet values. Native-boundary tests check nested, null and
throwing calls against outer helper ownership. The ordinary and
`/fp:fast` variants both pass.

Universal DLL builds passed with DevBench enabled and disabled:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders --parallel 8
```

Both used deployment and packaging disabled and runtime downloads skipped.
The production build fetched the published API pin without a local source
override, using `DEVBENCH_BRIDGE=OFF`. Its evidence is under
`build/validation/imgui-vr-helper/production-{configure,build}.log` and
`build/ALL/Release/CSX.BuildManifest.json`. The DevBench build's DLL,
manifest and log are preserved under
`build/validation/imgui-vr-helper/devbench/`; that build predates the final
SRV mip-count normalization and compositor shader-resource binding flag.

| Production linked artifact | Value                                                                   |
| -------------------------- | ----------------------------------------------------------------------- |
| Build ID                   | `ad2ccface062acb4ba263de1a61c89e4f6f6daab12454c852d0cdf79931c68a8`      |
| DLL SHA-256                | `b65eb2fc83f90a2f351ca505c06f1b0d55a1186284720ccce04a672a35362ab0`      |
| DLL size                   | 23,622,656 bytes                                                        |
| Producer source commit     | `656e717adbb95745b022294b28f60ac93415cb1c` plus implementation worktree |
| Producer dirty digest      | `3404fbf11a47c84fe620cd1ebc58de09f3c4e1cd617c980770fed745dee6ed7d`      |

The production result includes the UI-only fallback, production vendor
provenance and final resource-view hardening. A pre-existing MSVC C4456
warning in `ScreenshotApi.cpp` prevented the first universal build; a
nested lookup variable was renamed without changing behavior.

Scoped whitespace, line-ending, clang-format, Prettier and `git diff
--check` checks passed. Gersemi passed for the added dependency lines and
changed test CMake files; its existing unknown-custom-command warnings are
retained in the validation log. Unrelated legacy CMake formatting was
preserved.

## Runtime qualification still required

No runtime behavior is established by the CPU tests or successful link.
Qualification must cover the reported subtitle scene, intervening
occluders, both eyes, movement/VRIK/world scale, jitter and active-depth
alignment, native/TAA and current vendor routes, UI-only menus, old-helper
compatibility, both hook orders, lifecycle recovery and disabled/empty
host cost. The display-camera fallback also requires runtime validation.

Follow [render-scale PR qualification](render-scale-pr-qualification.md)
and the [render-scale ledger](vr-render-scale-ledger.md) requirements for
this runtime integration and any resulting measurements. No qualification
run, performance result or release-readiness claim is recorded here.

The inspected MO2 profile belongs to a different task and has neither
ImGui VR Helper nor FloatingSubtitles enabled. The automation configuration
has no maintained source profile for creating an isolated workspace.
Runtime deployment is therefore pending the test-profile selection and
subtitle fixture; the existing profile, running MO2 and SteamVR session
were left untouched.
