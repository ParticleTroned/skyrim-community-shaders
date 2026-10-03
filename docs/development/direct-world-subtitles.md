# Direct world subtitles in VR

CSX exposes `csx.vr.world_overlays`, major version 1, for the
[FloatingSubtitles VR - CSX fork](https://github.com/ParticleTroned/FloatingSubtitles-VR-CSX).
The subtitle plugin owns dialogue, actor tracking, settings and private
Dear ImGui text rasterization. CSX owns world projection, depth testing and
final eye composition. ImGui VR Helper is not part of this path.

## Source and scope

The CSX branch `codex/csx-world-subtitles` starts at main-VR
`43bc45b7ea9296ef23ccd8c7c45fd2b7345a5d35`. The paired producer branch
`codex/csx-direct-world-subtitles` starts at the Alandtse VR port
`56707e779fa0a57929cfd80baa3dff6afa2fa18a`. Both use separate worktrees.
The existing checkout and earlier helper experiment remain untouched.

The service is registered only in VR. SE/AE retain their existing CSX
rendering path, and the subtitle plugin retains its flat-screen backend.
No additional compositor hook, helper UI, controller system or client
callback participates in composition. The service uses the existing SKSE
registry and leaves legacy CSX interfaces unchanged.

## Ownership and lifecycle

The MIT-licensed `include/VRAPI/CSworldoverlayapi.h` defines a packed Windows
x64 function table. Four clients may publish up to 64 ordered quads each.
Atlas dimensions are bounded to 4096 per axis and 8,388,608 pixels. Each
client has at most three lazily allocated RGBA8 pages, at most 96 MiB at
the largest size. Only a page outside the committed and frozen-pair roles
can be leased for a complete redraw. A metadata-only publication reuses
the committed content revision; it does not copy or rerasterize the atlas.

Begin/update/publish/cancel run on the scene render thread after completed
opaque rendering. The producer uses HUDMenu::PostDisplay and restores its
private ImGui context and D3D state. Clear invalidates a publication while
retaining any outstanding write lease until cancellation. Unregister
returns Busy while a write lease exists. An already frozen pair may retire
after Clear, but cannot restore the cleared client's presentation receipt.

World epoch, render-target generation, completed world frame and compositor
cycle constrain admission. Loading, new/load-game events and resource
recreation invalidate content. Unrefreshed batches expire after two scene
frames. One immutable metadata/atlas snapshot covers both eye submissions.
Both accepted eye draws are required for the producer's presentation receipt;
only a recent receipt permits its normal HUD suppression policy. Missing
service, failed resources or stale content leave vanilla fallback available
when Skyrim's subtitle preferences enable it. A submission receipt is not
proof that a human can see the pixels.

## Presentation and depth audit

The local route audit covered `SubmitVRUpscaledFrame`, its temporal snapshot
and input-proof producers, completed-depth copying, and the final native and
vendor branches in `InSceneOverlay.cpp`.

-   Native None/TAA and main-pass reconstruction use the original packed eye
    image when render-scale mode is not latched and existing admission allows it.
-   Submit-stage vendor output uses its final per-eye image, including the
    reconstructed full-frustum foveated output. Its input proof must match the
    captured world frame, compositor cycle, vendor generation and depth extent.
-   Retained recovery, loading, stretch, quarantined, protected and unproven
    reduced-resolution fallback routes omit the external batch. Existing
    compositor acceptance and release guards are retained. Extended submit
    payloads bypass this composition path.

Camera metadata comes from CSX's uploaded VR frame buffer. The completed
opaque copy uses `kPOST_ZPREPASS_COPY`, after terrain/decal contributions and
before water, rather than a late arbitrary global depth lookup. The native
forward-Z copy, active packed-eye rectangles and its producing camera are
frozen together. Per-eye camera-relative positions subtract the captured
origin before float conversion. A normalized common billboard basis keeps
physical size consistent between eyes.

Reconstructed/TAA color uses the unjittered projection; depth samples use the
producing jittered projection, active rectangle and inverse projection.
The shader compares linear view-space depth with a 0.5 Skyrim-unit bias.
Each eye reads its own packed depth region. Background/invalid samples do
not occlude. Transparent geometry absent from this opaque depth copy does
not occlude subtitles. OpenVR tracking-space poses are not used to project
Skyrim-world coordinates.

World quads render into the existing private submission copy before the CSX
menu and capture indicator. Premultiplied blending preserves publication
order. Protected vendor history and recovery images are not modified.
With the menu closed, active subtitles can require one additional color
copy per eye. `VR::WorldOverlayCopy` and `VR::WorldOverlays` expose that work;
no performance or zero-copy claim is made. No-client/no-content presentation uses an atomic presence check and performs
no world-overlay copies or draws. A connected producer may retain its
bounded atlas allocation while idle.

A pending screenshot may allocate a separate per-eye capture surface and
copy the world-composed image before CSX menus/indicators. The existing
accepted-submit and resource-generation guards still gate capture. A capture
waits if the world stage is not yet available; it does not silently capture
a clean image while claiming to include subtitles. This optional extra copy
is reported separately as `VR::WorldOverlayCaptureCopy`.

## DevBench

A bridge-enabled build registers `communityshaders.world_overlays`:

```json
{ "action": "status" }
```

Status includes epoch, generation, scene frame/cycle, clients, quads, atlas
bytes, leases, pixel updates, publications, eye draws, accepted stereo pairs,
rejections and composition/capture copies. Counters are diagnostics, not
visual validation. The synthetic action exercises the same service without
requiring a dialogue trigger; center and dimensions use Skyrim units:

```json
{
    "action": "synthetic",
    "enabled": true,
    "center": [0, 0, 100],
    "width": 50,
    "height": 15,
    "depth": true
}
```

Choose a position in the current loaded scene. Compare `depth:false` only
as a diagnostic, then disable with `{"action":"synthetic","enabled":false}`.
`expectedBuildId` can pin all actions to the running producer.

## Offline validation and deferred runtime work

The Release universal CSX DLL and the paired universal subtitle DLL compile
with Visual Studio 18 2026. CSX enables DEVBENCH_BRIDGE and the new WARP GPU
test; shader/runtime distribution downloads are skipped in this development
build. These DLL outputs are not a complete AIO installer.

The focused commands from the CSX worktree are:

```powershell
& ./tools/cmake.ps1 --build build/direct-subtitles --config Release --target CommunityShaders world_overlays_test overlay_policy_test vr_submit_temporal_snapshot_test vr_submit_input_freshness_policy_test vr_submit_input_freshness_composition_test --parallel 8
ctest --test-dir build/direct-subtitles -C Release --output-on-failure -R '^(world_overlays|OverlayPolicy|VRSubmitTemporalSnapshot|VRSubmitInputFreshnessPolicy|VRSubmitInputFreshnessComposition|VRSubmitInputFreshnessContract)$'
```

On October 3, 2026, the DLL/test build completed successfully and the
CTest selection above passed **6/6** tests (0.57 seconds). Scoped C++ and
document hooks passed. The root CMake gersemi check is excluded because it
rewrites unrelated baseline formatting; the added `world_overlays.cmake`
fragment passes `gersemi --check`, with an unknown-custom-command warning.
CMake configuration/build validates the new registration. The repository
network doctor reports zero failures and zero warnings. No package HLSL is
changed; a full unrelated shader suite was not run.

The WARP fixture compiles the production embedded HLSL with strict checks and
requires the D3D11 debug layer. It exercises bounded/invalid API inputs,
frame expiry/wrap, asymmetric eye occlusion, mismatched color/depth pixel
locations, depth bypass, premultiplied opacity, ordered overlap, reversed
bounds, background depth and context-state restoration. Existing submit
policy and temporal/input-freshness tests cover integration guard behavior.
The adjacent `CSX.BuildManifest.json` is authoritative for a produced DLL's
source identity and Build ID; offline build receipts retain binary hashes.

Live Skyrim, physical-HMD visibility, NPC dialogue, moving actors, loading,
menus, screenshots and vendor-mode transitions remain untested by explicit
user instruction. SE/AE runtime testing is also not performed. Neither
render-scale qualification nor runtime performance measurement is claimed.
Before an upstream PR, run the repository's applicable render-scale
qualification and retain comparable timing/visual evidence. No measured
ledger rows are created for these offline implementation checks.

## Adversarial review

The review covered service admission/lifetime, stereo and depth ownership,
render failures, HUD fallback, SE/AE boundaries and duplicate policy logic.
It found and corrected these implementation issues before folding the
changes into the original feature commit:

| Finding                                                                                         | Correction and evidence                                                                                                                                                               |
| ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Clear followed by republish could revive an old pair receipt.                                   | A frozen clear serial must match before acknowledgement. Production publication-state tests cover clear/republish, retained write/frozen leases, expiry and generation/epoch changes. |
| Drawing could use state invalidated between the preliminary check and lock acquisition.         | Recheck the shared draw predicate under the drawing lock. The preliminary predicate remains only an optimization.                                                                     |
| Any-thread diagnostics/capabilities read mutable engine rendering fields.                       | Inspect the mutex-protected completed-scene snapshot. Begin/publish check thread identity before reading current engine admission.                                                    |
| RTV failure could take down existing compute menu composition.                                  | Retain the original UAV copy path when RTV support/allocation fails; skip only world rendering.                                                                                       |
| A prior world draw could select a screenshot source for a different submitted texture.          | Require identity with the actual private submitted texture before selecting the world capture.                                                                                        |
| Depth extent conversion and derived inverse matrices lacked final bounds checks.                | Check extents before unsigned conversion and validate the derived inverse. Admit the supported R32 float/stencil depth view as well.                                                  |
| Concurrent synthetic controls could register duplicate clients or republish after disable/load. | Serialize controls, check the captured epoch and recheck enablement after leasing. A subsequent clear invalidates the lease.                                                          |

Publication reset, acknowledgement and lease-page admission now share one
production policy, exercised by the WARP fixture. Shared ABI constants keep
provider/producer geometry limits aligned, and compile-time checks tie the
CPU quad layout to the embedded shader. The paired producer has separate
freshness/finite-input and actual text-atlas WARP tests. The latter verifies
glyph pixels, callback cleanup and rejection of an injected early backend
return. It does not claim an actual driver allocation failure was induced.

Both Release DLL builds and the eight focused tests are the review checks:
six host tests listed above, plus `CSXPublicationPolicy` and `CSXTextAtlas`
in the producer worktree. Re-run those commands after any further source
change. Manual/in-game and physical-HMD checks remain deferred. D3D driver
allocation failures, engine-thread scheduling and actual compositor retries
are reviewed failure paths, not live-tested scenarios.
