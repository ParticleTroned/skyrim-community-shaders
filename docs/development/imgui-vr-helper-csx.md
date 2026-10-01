# CSX-only ImGui VR Helper integration

This working branch contains the private CSX metadata foundation for the
optional helper integration. It is based on `main-VR` commit
`dab1874a76fd39175dcefdc52110ba69d7284e12` and corresponds to the helper
foundation at
[`d849ce812e9750cb2892d50167bab7d20f2ccbb5`](https://github.com/ParticleTroned/imgui-vr-helper/commit/d849ce812e9750cb2892d50167bab7d20f2ccbb5).
The existing checkout and its selected branch remain independent of this
worktree.

## Compatibility boundary

The helper change must be isolated to explicitly negotiated CSX hosting.
Module presence, registration, or loading a newer helper must not change
other users' rendering. Existing helper clients keep interfaces `001`
through `005`; Open Shaders and standalone rendering retain their paths.

Neither branch currently exposes or consumes a renderer-host API. CSX does
not probe a guessed interface revision, register a helper client, change
input ownership, install another hook, allocate a helper presentation copy,
or call the new metadata code from a rendering path. There are no new
settings, DevBench actions or per-frame graphics operations in this change.
This is not a subtitle fix or a completed runtime adapter.

## Implemented private contracts

`ImGuiVRHelperScenePacket.h` validates explicit texture-view extents,
positive active depth rectangles, normalized output bounds, depth decoding
and tracking-space projections. Reversed output bounds produce a positive
viewport plus independent orientation flags. No engine resolution ratio is
inferred or applied a second time.

The CPU projection oracle transforms the same tracking-space point into the
depth camera and maps it into the supplied eye rectangle. Out-of-eye or
invalid samples return no comparison value; they are not clamped to another
object or replaced with zero depth. Matrix storage is row-major and uses
row-vector multiplication. Depth comparison uses axial distance in metres.
Native decoding follows the completed-depth camera convention; linear
decoding is separately explicit and does not qualify a CSX vendor source.

`ImGuiVRHelperHostPolicy.h` builds a complete stereo packet by value or
returns a rejection reason and eye. It requires:

-   VR, matching nonzero negotiated/active CSX host tokens and world content.
-   One current native or reconstructed route for both eyes; retained,
    protected, loading, device-lost and unknown candidates are rejected.
-   Exact pair token, compositor cycle, scene frame and thread, using the
    existing `VRRenderScaleFrameBoundaryPolicy::PairIdentity`.
-   Current device and completed resource-publication generation, using
    `OpenVRSubmitLeasePolicy::CanPublish` for the retained Submit payload,
    plus separate occlusion-depth retention with matching resource, device
    and generation identities.
-   Explicitly completed opaque depth with full-eye coverage for both eyes.
-   Single-sample, single-slice, base-mip 2D views; checked dimensions,
    source-unit conversion, camera transforms and axial depth direction.
-   A supported existing `VRSubmitColorContract`, proven RTV capability,
    distinct color/depth resources and disjoint eye rectangles in shared
    textures. Cross-eye color/depth aliasing is also rejected.

One invalid eye rejects the entire world packet before the first eye can be
drawn. Empty/inactive inputs return before metadata validation. No CSX menu
visibility is required. All values are private C++ types, not a proposed
DLL ABI. Numeric validation is protected from MSVC `/fp:fast` assumptions.

The builder validates supplied metadata. It does not query D3D objects,
retain COM references, authenticate a host token, preserve texture pixels,
or establish freshness from an address. The eventual adapter must query
actual view kinds, format/binding support, mip dimensions and devices;
retain the referenced objects; and prove that their contents still match
the scene. The Submit lease retains the original compositor payload; it
cannot prove helper occlusion-depth ownership. That source requires its
own retention evidence even when the OpenVR payload has a depth attachment.
A color-only OpenVR payload remains valid with separately retained
occlusion depth.

Camera values must come from the captured scene, with the precise world
origin/room conversion applied before a later float upload. The adapter
must qualify the double-to-shader representation and reject unsupported
reprojection. Pair attribution must be established from completed producer
evidence, not assigned to arbitrary retained resources at Submit time.

## Verified source boundaries on this main-VR base

-   `Deferred::CopySceneDepth` copies physical main depth into
    `kPOST_ZPREPASS_COPY` and stamps the completed frame.
    `Util::GetCurrentSceneDepthSRV` selects it only when final. This provides
    a candidate source, not a guarantee against subsequent GPU writes.
-   `VRSubmitSourceRegion` depth offsets/extents are expected guide
    coordinates. Validate them against separately queried actual SRV
    dimensions before building a packet.
-   `EncodeTexturesCS.hlsl` writes native depth unchanged. The FSR resource
    named `vrIntermediateLinearDepth` is not linear metres, and foveated
    encoding can update only an ROI. This builder initially admits completed
    opaque full-eye depth rather than inferring vendor coverage.
-   `BSOpenVR_Submit` supplies the native pair boundary. Existing
    `VRSubmitInputFreshnessPolicy::ProducerProof` can corroborate vendor
    inputs, but native hosting must not acquire a motion-vector dependency.
-   `InSceneOverlaySubmitPolicy::ShouldAdmit` includes CSX menu exceptions.
    Its success does not authorize current-world helper quads on retained or
    protected images.
-   The existing central Submit lambda retains the complete OpenVR payload
    and resources and performs presentation accounting. The later adapter
    must preserve it. Current presentation copies are SRV/UAV-only and gated
    on CSX content; safe helper composition needs RTV support and independent
    content gating without changing inactive behavior.

## Remaining runtime work

The helper must first implement its complete opt-in hosting interface,
frozen client geometry/anchors and synchronized texture contents. CSX can
then pin that accepted API-only commit and add lifecycle negotiation,
producer capture, safe RTV/UAV presentation copies and final-eye callbacks.
No ABI number or shape is reserved by this foundation.

Keep native and vendor output eligible only after final selection, preserve
extended OpenVR payloads, and exclude protected/retained/loading routes.
Pair/attempt handling must prevent duplicate blending and discard scratch
images after partial writes. Diagnostic actions and schemas belong with
that runtime adapter.

The reported subtitle scene, true intervening occlusion, both eyes, old
helper/client compatibility, both hook orders, lifecycle recovery and
performance still require qualification. Follow the existing render-scale
qualification and evidence-ledger rules when runtime render-scale behavior
is changed or measured. This branch has no such runtime change or run.

## Validation

The new tests are registered with `controller_tests` through
`tests/imgui_vr_helper.cmake`. On an already configured full checkout:

```powershell
pwsh ./tools/cmake.ps1 --build <build-dir> --config Release --target imgui_vr_helper_scene_packet_test imgui_vr_helper_scene_packet_test_fast imgui_vr_helper_host_policy_test imgui_vr_helper_host_policy_test_fast
ctest --test-dir <build-dir> -C Release -R '^ImGuiVRHelper_' --output-on-failure
```

Executed on 1 October 2026 with MSVC `19.51.36252.0`, Visual Studio 2026
and Windows SDK `10.0.28000.0`. An isolated local CMake harness includes the
same maintained test registration, with `/W4 /WX /EHsc`, C++23 and Release
optimization. It avoids configuring unrelated plugin dependencies.

```powershell
pwsh ./tools/cmake.ps1 -S build/helper-host-tests-source -B build/helper-host-tests -G "Visual Studio 18 2026" -A x64
pwsh ./tools/cmake.ps1 --build build/helper-host-tests --config Release --parallel 4
ctest --test-dir build/helper-host-tests -C Release --output-on-failure
```

All four executables passed: scene geometry/depth and host admission, each
with ordinary floating-point settings and `/fp:fast`. Fixtures cover atlas
versus separate-eye layouts, asymmetric/rotated cameras, reversed bounds,
off-ray versus true occlusion, NaN/Inf/overflow, stale identities, missing
depth, cross-eye aliases, resource reuse and snapshots surviving later
metadata mutation. The first host test run exposed acceptance of the
reserved invalid-frame sentinel; it was corrected and both variants passed.
Review also added regression coverage for dependent matrix rows and
independent occlusion-depth retention. Matrix admission uses a relative
roundoff threshold after row normalization so tiny and huge valid scales
remain admissible while rounding residue from singular matrices is rejected.

Scoped pre-commit checks passed for whitespace, line endings, clang-format
and Prettier. The full-file gersemi hook initially changed unrelated
existing CMake formatting; those changes were removed. Gersemi then passed
for the added `CMakeLists.txt` line with `--line-ranges 2677-2677` and for
the complete new `tests/imgui_vr_helper.cmake`. The other hooks were rerun
with `SKIP=gersemi`; no legacy CMake formatting changes are included.

The full DLL, GPU/HMD rendering, DevBench runtime controls and performance
were not exercised. No binaries were deployed. Passing these CPU contracts
does not establish the screenshot's root cause or runtime compatibility.
