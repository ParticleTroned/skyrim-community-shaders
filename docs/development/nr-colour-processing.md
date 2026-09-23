# Shared NR colour processing and automated assessment

This follow-up builds on `0a9b44744950677dbe128614219eecf1d0e38f24` on
`work/face-of-gogh-colour-managed-20260914`. The underlying `face-of-gogh` base
is `1a5bda199f0bd8e710c720a77b8c6931b65e8708`. It changes source code only;
Windows compilation, the exposure hook's live timing, WARP and Skyrim/NGX
validation are still required. No NVIDIA binaries or admission checks change.

## What is shared

The existing renderer expands logical eyes into physical single/multi-ROI
requests. `Color::Pipeline` snapshots their baselines, prepares input, reconstructs
ALL successful regions privately, and then uses the existing commit boundary.
Standard NR and character NR use exactly this path. Character selection happens
once downstream; neither the capture nor colour shaders author a character mask.
Existing raw-depth, guide, cluster-history, stereo fallback and final-LDR guards
remain intact. Colour-enabled sequential stereo retains the atomic batch path.

Modes are `legacy_raw` (compatibility default), `managed`, `preserve_source`,
and `neural_lighting`. Managed transfers the model residual relative to the
actual quantized prepared input, not an independently recomputed proxy:

`candidate = baseline + inverse(neural) - inverse(prepared)`

Preserve Source adds bounded, edge-weighted log-brightness changes to source
RGB. Lighting preservation controls how much of the existing local smooth
residual is removed: 100% retains the previous detail-only behaviour, 50%
removes half, and 0% admits the full bounded luminance residual. The 3-by-3
estimate and its spatial scale are unchanged. Appearance mix accepts more of the reconstructed neural result,
including tone and chroma, not merely hue. This is an image-space mitigation,
not physical decomposition of reflections/shadows. Source alpha is retained.
The unknown/native-domain brightness metric is not calibrated luminance.

Neural Lighting is a named use of that same reconstruction path. It admits the
full bounded luminance residual and fixes appearance mix to zero, so the source
RGB ratios and alpha remain authoritative while the model supplies brightness.
Detail strength and maximum gain remain shared controls. Selecting the mode does
not rewrite the saved Preserve Source lighting-preservation or appearance values.
It adds no second shader, colour transport, exposure path or temporal history.

The detail filter uses adjacent, ROI-clamped 3-by-3 samples. Spacing those
samples two pixels apart cancels alternating fine detail before the strength
control can recover it. The [detail investigation](nr-colour-detail-investigation-20260916.md)
records the shader regression, correction and remaining HMD validation.

For R11G11B10 output, the final Preserve Source result is explicitly rounded
to the nearest representable value, ties to even, after range validation.
This avoids one-sided darkening when a packed UAV store truncates tiny edits.
Other storage formats and the existing early-return endpoints are unchanged.
The [packed-rounding correction](nr-colour-packed-rounding-fix-20260917.md)
records the same-frame evidence and WARP/hardware regressions.

The [character stability corrections](nr-character-stability-fix-20260919.md)
address optional feather discontinuities, pre-DLSS mask alignment and
readback-driven single-ROI history churn. They preserve colour reconstruction
and require a new live comparison before claiming the reported flashes are
resolved.

## In-game controls: no editable configuration INI

On `main-vr-nr`, **Display > Neural Rendering** owns rendering,
character and colour controls. See the [port report](main-vr-nr-implementation.md)
for the current routes and validation; the source-build history above is
retained for provenance. Essentials shows Enabled, Rendering mode,
Restrict to FOV mask (where applicable), Characters only, and Faces/Skin/Hair.
The category choices stay visible but are disabled when character selection
is inactive or required FOV setup is unavailable. On SE/AE, use Full
resolution without FOV restriction for face, skin and hair selection.
Enabled is the master preference and never becomes disabled because of a
child setting. A missing FOV prerequisite leaves enabled NR waiting, with
an explicit explanation, and the runtime gate prevents evaluation. Selected
FOV and character restrictions can always be removed; Full resolution
remains selectable to leave an unavailable Foveated route. Disabling NR
retains child preferences and remains accepted even if backend retirement
fails. Re-enabling still requires safe backend retirement.

DevBench `nr_configure` also accepts valid pending FOV-dependent settings;
`nr_readiness` and `nr_status.fovPrerequisite` distinguish acceptance from
execution readiness. Unsupported runtime configurations and invalid provider
contracts remain rejected. NR still excludes FOV + TAA when enabled.

Both layouts share the same settings and transition handling. Advanced
additionally exposes image tuning, character strengths and colour controls;
Developer Mode controls diagnostic visibility within Advanced.

Every NR toggle, slider, selector and action has its own concise tooltip,
including disabled controls. Faces, Skin and Hair each explain their own
selection and strength; colour sliders explain their endpoints separately.

**Full resolution + Restrict to FOV mask** runs NR at full output resolution
on the final scene before UI, limited to the configured eye masks. Pixels
outside those masks retain the normal scene. **Foveated** runs NR across the
DLSS-upscaled FOV region and blends its edges into the scene; Insertion Point
chooses when that region is processed. FOV restriction selects the region,
not a weaker model setting. Characters only can further limit edited pixels
in either mode.

The Advanced colour section provides:

-   **Enable colour processing**: retain the chosen mode/sliders while bypassing
    their application. The NR master switch is in the same feature panel.
-   Normal colour-mode choices are **Original**, **Preserve source**, and
    **Neural lighting**.
    Colour processing Off selects Original/raw NR without disabling NR.
    **Managed (experimental)** is offered only in Developer Mode
    (Debug/Trace). Existing saved Managed selections remain visibly labelled
    and unchanged at normal log levels; either normal mode can replace them.
    Its API value and saved enum remain supported.
-   **Lighting preservation (0–100%)**, shared by full-resolution
    NR with or without FOV restriction, NR through FOV, and NR before upscaling.
    Character selection uses the same setting. The slider defaults to 100%.
-   The slider remains visible with a notice when inactive. Choose **Preserve
    source** and enable colour processing to use it. Full neural appearance
    mixing or zero detail strength/gain also disables it while retaining its
    value. Other controls remain available to resolve those conditions.
-   Preserve-source detail contribution, neural appearance mix and maximum
    detail gain. Unavailable FOV prerequisites disable dependent NR controls
    with a setup notice; mode selection remains available for recovery.
-   Neural Lighting keeps source colour and alpha while applying the complete
    bounded neural brightness field. Detail contribution and maximum detail
    gain use the same controls; lighting preservation and appearance mix remain
    saved for Preserve Source and are not applied in this mode.

Managed uses the session-only colour/exposure calibration under **Colour
experiments and diagnostics**; it is not a validated production preset.
Default Identity conversion and unit exposure can look like Original.
Lighting preservation and appearance mix affect Preserve Source only. Detail
contribution and maximum detail gain also bound Neural Lighting.

**Debug/Trace** additionally exposes A/B comparison, engine HDR exposure
capture, separate early/late domain and exposure experiments, transport
bypass, asynchronous measurements, asset checks and structured diagnostics.
The A/B control keeps inference running and is not an NR-off performance
measurement. See the [slider source implementation](nr-lighting-preservation-20260917.md)
for its preserved upstream evidence.

Ordinary settings use the existing saved-settings JSON. Assessment profiles,
exposure capture, transport bypass and A/B state are transient. Both UI and
DevBench call the same compare-and-set registry; neither polls a configuration
file or performs GPU work from a DevBench worker thread.

`Shaders/Features/NeuralRendering.ini` is ONLY a feature/version manifest, now
**1-5-0**. It is installed by the build; users do not edit it to assess colours.
The separate older `work/face-of-gogh-colour-20260914` experiment used an editable
`NRColor.ini`; that is not this branch's control interface.

## Automatic engine-exposure capture: concrete source evidence

The repository's `package/Shaders/ISHDR.hlsl`, BLEND permutation, reads AvgTex
at pixel-shader slot **t2** and multiplies input colour by **AvgTex.y / AvgTex.x**
when both components are nonzero. The zero-component branch leaves exposure at
one. `Common/FrameBuffer.hlsli` subsequently applies `pow(abs(colour),
FrameParams.x)`; its function name does NOT make that the IEC sRGB curve.
FrameParams is at PS b12 c84 for VR and c42 for flat rendering in this source.

`ExposureCapture` observes the two known cinematic HDR tonemap effects at
the immediate context's seven direct, instanced, indirect and automatic draw
entries, through the shared draw hooks. Existing ImageSpace Render/Dispatch
wrappers establish an RAII producer scope, installed independently of the
frame-annotation toggle. The live pixel shader must match its owned engine
selection or the exact replacement association;
unrelated draws cannot supply exposure through a stale technique pointer.
Compute dispatches are excluded. It obtains the concrete effect
instances through the pinned CommonLib ImageSpaceManager API during the
normal render-thread EarlyPrepass. Only the two HDR producers require the
effect wrappers when annotations are disabled. Scope-entry counts, last
producer frame and per-draw-form counts distinguish missing effect callbacks
from missing draw observations.
Status reports `producersRegistered`, `captureBoundary` and `lastBinding`
(frame, dimensions, mip, sample/array counts, formats and identities), even
for rejected bindings. The compatibility field `hooksInstalled` is now zero.
`expectedShaderIdentity`, `shaderIdentity` and `viewIdentity` distinguish
missing bindings from a shader mismatch. Actual capture timing still needs
qualification on each running rendering path.
Capture alone does not activate colour reconstruction in Legacy Raw mode.

Capture accepts a bound 1x1 or 2x2 floating-point AvgTex view with at least two
channels and exactly one visible mip. The actual AvgSampler must use ordinary
point/linear/anisotropic filtering and non-border addressing. The GPU reads
every texel and admits a scalar when all average/target components are
identical, or every texel has finite positive `x == y`. The latter proves a
unit ratio under the supported filtering, even if brightness differs across
texels; it reports `measured_unit_ratio`. Other differing fields are rejected
with validity 3 (`non_uniform_avgtex`); no average or selected texel is treated
as the engine's scalar exposure. `ColorExposureCS` writes the scalar summary
followed by four row-major per-texel records into a private 5x1 FP32 texture.
Unused records are invalid. This adds 64 bytes to each transaction snapshot.
`engineCapture.samples` reports the source view dimensions/mip, sampler/view
identities, texels and scalarStatus. `lastBinding` also reports sampler filter,
address modes and visible mip count. Different views, samplers or shaders in
one source frame are ambiguous even if the underlying texture is the same. Nonfinite input, unsupported resources and missing
bindings are reported. The engine's zero-input unit fallback has a distinct
validity code; it is never presented as a measured unit exposure. Ratios outside
1/256..256 are outside the current experiment's supported range.

Eight bounded records retain source-frame, capture epoch/sequence, resource/
view/output formats and process-local shader/resource identities. Optional
nonblocking readback also records the actual frame-gamma exponent. These are
producer observations, not claims about NVIDIA's expected model domain.

For a captured-exposure profile, preparation snapshots the matching source
world frame into transaction-owned storage. Explicit `captured_hdr_previous`
instead requires exactly `sourceWorldFrame - 1`; it retains the actual producer
stamp and reports `exposureAgeFrames = 1`. It cannot accept older frames,
unknown/wrapped source frames, another epoch or an ambiguous source. Both eyes and all their regions
reuse the first latched availability/value. An API toggle cannot switch that
value halfway through a pair. Reconstruction uses this same immutable snapshot;
there is no later lookup of a global/latest exposure and no per-face metering.
Manual calibration is multiplied by the captured ratio on the GPU. No render-
thread readback wait or new Flush is introduced.

**Timing qualification is essential.** Missing or unsupported draw bindings
make capture unavailable. If the HDR pass happens
after an early NR invocation, that invocation cannot use a future exposure.
The strict `captured_hdr` mode reports that mismatch. The separate
`captured_hdr_previous` experiment makes one-frame-latency exposure available
for early insertion; this is not current-frame tonemap matching. During rapid
adaptation it may differ from the exposure later applied by HDR. A later
insertion can use the strict current-frame mode. Different adaptation sources within one
frame are marked ambiguous in both binding and readback evidence. The revised
draw boundary and these route conditions still require in-game qualification;
the previous RestoreTechnique observation rejected every tested binding.
See [the null-driver retest](nr-colour-null-driver-retest-20260915.md).

Captured HDR exposure is NOT automatically DLSS SR's internal exposure, NR
pre-exposure, or proof that the submitted colour has not already been exposed.
The engine formula is now instrumented; model-domain and placement hypotheses
are tested by the assessment runner rather than assumed from texture format.

## Profiles, history and resource ownership

Early `upscaled_center` and late `final_ldr_pre_ui` profiles are independent.
Each declares an explicit domain candidate (`unknown`, `linear`, `srgb`), a
transform (`identity`, `linear_to_srgb`, `reversible_proxy`), a calibration
multiplier and exposure source (`manual`, `captured_hdr`,
`captured_hdr_previous`). Identity requires
manual multiplier one. Nonidentity requires the explicit linear candidate.

The proxy uses exposure-scaled RGB / (1 + max RGB), then sRGB encoding. Its
inverse has a guarded denominator. Supported scaled input max is 32; inverse
denominator must be at least 1/64. Invalid input/inverse retains the baseline
and is counted, so a fallback cannot masquerade as a successful zero-error
round trip. This bounded proxy is not lossless HDR coverage. Do not infer a
source domain merely from floating-point storage or the name Final LDR.

Input interpretation changes advance the existing per-insertion history epoch.
A/B visibility and diagnostics do not. Normal variation of captured exposure
does not change configuration revision or reset the network each frame.
Local colour resources reuse 64-pixel-rounded capacities. Shared interop fences
and the renderer's reset/unsafe-detach boundary govern lifetime. The new t3
binding and predication are saved/restored. NR off performs no capture dispatch.

## DevBench API v3

Tool: **communityshaders.nr_color**. Actions: `status`, `configure`,
`reset_experiments`, `assets`. Responses explicitly report `ok` and error codes
so automation/dev can enforce semantic success. Configuration acknowledges
registry state, not completed GPU work; require processed, fresh measurements.

`measurementBatches` publishes up to four complete private reconstruction
batches, newest submission first. Each batch fixes its `measurementBatchId`,
`expectedMeasurementSlotMask`, frame, source-world frame, generation,
revision, insertion point and `atomicColourBatch` before its readbacks are
queued. Every enclosed measurement carries the same identity and membership.
Changing character regions therefore do not borrow another frame's secondary
slot or require a region that was absent from that batch.

The renderer polls pending readbacks for all eight physical slots on each
admitted batch, including currently inactive regions. Polling remains
nonblocking and does not flush or wait on the GPU. A bounded 16-batch assembly
history rejects duplicates, identity conflicts and missing members;
`counts.evictedIncompleteBatches` preserves incomplete-history eviction.
Batch IDs survive resource resets. Legacy `slots` and `measurements` arrays
remain latest-per-slot diagnostics; API-v3 assessment never falls back to
combining those arrays. The 24-float measurement layout remains version 2.

An explicit `--expected-physical-slots` fixture must match the batch membership
exactly. Without that option, the runner reports
`regionCompleteness=immutable_batch_manifest`. It still requires fresh complete
stereo groups and separate outer-renderer evidence. A mono batch or an empty
eye is not silently promoted into measured stereo. Complete private readbacks
do not prove external output commit, inference during transport bypass, or HMD
presentation. API-v2 evidence retains the older conservative grouping rules.

```json
{
    "action": "configure",
    "settings": { "enabled": true, "mode": "preserve_source" },
    "experiments": { "applyModelEdit": true, "diagnostics": true, "captureEngineExposure": true }
}
```

Display-only A/B, retaining real inference:

```json
{ "action": "configure", "experiments": { "applyModelEdit": false } }
```

Frame-matched exposure candidate (NOT an assertion that early input is linear):

```json
{
    "action": "configure",
    "settings": { "enabled": true, "mode": "managed" },
    "experiments": {
        "upscaled_center": {
            "domain": "linear",
            "transform": "reversible_proxy",
            "exposureSource": "captured_hdr",
            "exposureMultiplier": 1.0
        },
        "applyModelEdit": true,
        "diagnostics": true
    }
}
```

Use `expectedRevision` from status for automatic changes. Read-only provenance
fields in profile status are not accepted as configure fields. The supplied
runner projects only editable fields when restoring settings.

Measurements retain the previous first sixteen values and append two float4s:

| Indices | Meaning                                                                                       |
| ------- | --------------------------------------------------------------------------------------------- |
| 0..3    | Baseline mean RGB; valid baseline/result comparisons                                          |
| 4..7    | Result mean RGB; mean absolute RGB difference                                                 |
| 8..11   | Max absolute difference; result near-black count; result >=1 count; raw NR outside 0..1 count |
| 12..15  | Nonfinite baseline/NR/result counts; total samples                                            |
| 16..19  | Invalid forward/inverse samples; effective exposure; effective-exposure-valid flag            |
| 20..23  | Captured average/target/ratio/validity (0 invalid, 1 ratio, 2 unit fallback)                  |

Above-one is not necessarily HDR clipping. CPU enqueue measurements are not GPU
timing. Optional capture/readback and colour passes have overhead to measure
locally. Old samples remain visible with their original revision/frame; never
accept them just because they occupy the latest array slot.

## Automated assessment using skyrim-vr-automation/dev

Reviewed automation/dev revision:
`a4ab2cf6ea6c853926918e5626b8d17f15cf5d79`.
`tools/nr-color/assess.py` uses that repo's existing **Invoke-DevBenchControl.ps1**
and optional **Invoke-CaptureInteraction.ps1**. It does not implement another
HTTP client, bypass runtime identity checks, edit MO2 profiles, launch Skyrim,
move the camera, change cells or execute tfc1.

First prepare an existing isolated test workspace, load a static scene, enable
NR and choose its insertion point using your normal automation/UI. The script
checks the expected cell/upscaling barrier; it does not set up or mutate the
scene for you. Plan-only is the default:

```powershell
python tools/nr-color/assess.py --include-captured --include-captured-previous
```

Live example; supply real paths/cell for the prepared workspace:

```powershell
python tools/nr-color/assess.py --live --confirm-static-scene --include-captured --capture-episodes --automation-root D:\Coding\GitHub\skyrim-vr-automation --runtime C:\YourTestWorkspace\runtime.json --evidence-dir C:\YourTestWorkspace\evidence\nr-colour-run-01 --expected-cell YourLoadedCell --insertion upscaled_center
```

The evidence directory must be NEW. Optional `--deployed-data` verifies shader
hashes first. The controller's artifact/workspace proof can be supplied with
`--artifact-path`, `--workspace-manifest`, `--expected-build-id` and
`--expected-artifact-sha256`. No bypass permission flags are used.

Each candidate gets a transport test and real-inference comparison, bounded
warm-up/polling, fresh same-revision stereo/region measurements and validity
checks. `--include-captured-previous` explicitly adds two one-frame-history
candidates; `--include-captured` retains its strict current-frame candidates.
Captured candidates require their declared exact source-frame age; missing
exposure, invalid inverses and zero-error fallback are not a pass. Optional
shown-edit and hidden-edit capture episodes retain state/frame manifests for
visual review. Those images are separate live frames, not falsely labelled the
exact same frame as a numeric sample. Keep camera/scene stable.

The runner journals requests/responses, pins the returned runtime identity,
uses semantic/performance-neutral guards, and never retries an indeterminate
mutation. It restores only its owned configuration under a revision check;
a concurrent UI/agent change is preserved and recovery evidence is retained.

Before any scene barrier or colour call, catalog preflight requires the NR
tool, profiler, readiness services and a complete verified producer/artifact
identity. The public upscaling barrier is used where available; this branch
instead exposes `renderscale` action `nr_readiness` for a fixed prepared NR
scene. It checks loading, compilation, target publication, device health,
resource transitions and scaled profile/fidelity state. The runner also
checks menus, the exact loaded cell and five advancing frames with unchanged
target identity. This limited check does not qualify render-scale behaviour
or headset presentation. Capture episodes also require the screenshot,
recording and input tools. `--live --preflight-only` checks these prerequisites
without requiring a static-scene assertion or changing settings. Supply the
automation, runtime, evidence and artifact arguments as above; add
`--capture-episodes` to include its catalog requirements. The preserved
`preflight.json` lists absent services and identity fields. The nested
controller identity is projected into the flat expected-identity contract
before subsequent calls.

Output ranks **private-buffer source RGB drift**, not HMD image quality or
physical material correctness. Successful inverse round trips cannot identify
a model's expected colour domain: all consistent invertible candidate pairs
can pass. `domainVerified` therefore remains false and no production profile
is silently selected. Complete the separate
[automated HMD image assessment](nr-colour-hmd-assessment.md) using the existing
VR automation capture and blinded visual-review tools. It requires distinct
NR-off, Raw, Managed identity, conversion, Preserve Source and display-only
references, calibrated regional measurements, and actual per-eye image review.
The user is not required to decide which mode looks better. The numeric runner
and its optional capture episodes alone do not complete that assessment.

## Shader inventory, deployment and tests

All required shaders are in the colour feature package: ColorPrepareCS,
ColorReconstructCS, ColorMeasureCS, ColorExposureCS, plus ColorCommon.hlsli.
Normal recursive source/feature discovery and shader-copy paths include them.
Install the rebuilt plugin AND feature/shader files. The current manifest is
1-5-0; the shared colour constant buffer is 64 bytes. Deploy the DLL and
all colour shader assets together and invalidate the old feature shader cache.

```powershell
python tools/nr-color/verify_assets.py
python tools/nr-color/verify_assets.py --deployed-data C:\YourTestWorkspace\Data
cmake -S tests/neural_color -B build/nr-color-tests
cmake --build build/nr-color-tests --config Release
ctest --test-dir build/nr-color-tests -C Release --output-on-failure
```

The verifier checks include closure, runtime shader paths and optional exact
source/deployed SHA-256 parity. File presence alone is not shader compilation
or proof of the winning MO2/VFS files; select the actual test Data root.

Authoring validation: portable colour/exposure policy executables, Python
assessment fixtures and source asset inventory were run. Two existing local
source-contract subtests can run independently; the full old renderer/feature
contract suite needs a complete checkout. Windows targets retain the original
WARP suite and add production exposure/t3/A-B/96-byte statistics coverage.
Windows plugin compilation, both WARP executions, PowerShell live orchestration,
actual engine-hook timing and Skyrim image/performance testing are NOT claimed.

## Windows build follow-up (2026-09-14)

The complete checkout of `977fc12a904f1a6898cd1e5d68efe15fbf3456e3`
exposed an FXC error: the measurement shader used the reserved HLSL
interpolation keyword `linear` as a local identifier. Renaming it to
`pixelIndex` preserves the sampling calculation and allows both WARP
executables to compile and run the production shaders.

Validation after that correction:

-   Universal SE/AE/VR Release plugin compile and link passed with MSVC
    19.51.36252.0 and Windows SDK 10.0.28000.0. The
    `Internal-DLSSNR-AIO` configuration enables DevBench and stages the
    existing fingerprinted local NR runtime; automatic deployment is off.
-   `python tools/nr-color/verify_assets.py` passed the source inventory.
-   `cmake -S tests/neural_color -B build/nr-color-tests` and
    `cmake --build build/nr-color-tests --config Release` passed.
-   `ctest --test-dir build/nr-color-tests -C Release --output-on-failure`
    passed all 12 tests, including both WARP targets and the full-checkout
    source contracts. The colour WARP target reported 197,989 checks.
-   `python tools/nr-color/assess.py --include-captured` produced its
    plan-only output. No live assessment was performed by that command.

The first plugin build failed in FidelityFX shader generation under the
deeply nested worktree path. Reconfiguring the same checkout through a
short source junction and a short binary directory allowed that generation
and the complete plugin build to pass. The CMake launcher requires separate
`-D` and value arguments for values containing a Windows drive colon.

The standalone shader compiler still reports X3571 for conditional sRGB
power expressions and X4000 in `EffectiveExposure`. The WARP tests pass,
but this is not a warning-free shader validation. The plugin compile/link
log contains no C++ warnings or errors; configure retains the dependency's
CMP0116 deprecation warning.

These checks establish buildability and the exercised policy/shader
behaviour. They do not establish the correct NVIDIA NR colour treatment,
live exposure-hook timing, actual NGX inference, winning MO2/VFS files, or
headset presentation. That deployment retained the `1-2-0` colour package
with the rebuilt DLL before those separate live checks.

### Runtime feature registration correction

The AIO built from `165e0841f` contained the colour INI and shaders, but
the checked-in `include/FeatureVersions.h` omitted `NeuralColor`. That
header takes precedence over CMake's generated registry, so the feature
loader classified the installed `1-2-0` feature as unknown. The earlier
asset and WARP checks did not exercise this registration requirement.

`NeuralColor` is now registered at minimum version `1.2.0` and included in
`FEATURE_CORE_NAMES`. Its existing `CORE` marker and
`Shaders/Features/NeuralColor.ini` remain the package metadata.
`CMakeLists.txt` invokes `cmake/ValidateNeuralColor.cmake` to require the
INI, CORE marker, runtime registry entry, matching version, and core
registration. Those inputs also trigger CMake reconfiguration when changed.

The same guard is exercised by `NRColorFeatureRegistration` in the
standalone suite. Six fixture tests cover complete registration, missing
runtime/core entries, mismatched versions, missing required files, and an
invalid INI. A generated header cannot hide a missing runtime entry. The
guard rejected the previous source with `NeuralColor is missing from
include/FeatureVersions.h`; all 13 standalone tests pass after correction.
This verifies the registration contract without claiming a new live game
or headset assessment.

### Live DevBench preflight (2026-09-14)

DevBench answered from SkyrimVR PID 36796, started at
`2026-09-14T22:27:00.0957015Z`, with a loaded player in
`WhiterunDragonsreach`. The selected MO2 task profile enabled AIO
`CSX_AIO-CommunityShaders-f6335a034-20260914T221648Z`. Its physical DLL
was 24,002,560 bytes with SHA-256
`829ad286b604a3402828e45bb9c59bf1aabb595e2f4a3d0d81d760213f470ee1`,
matching the preserved build receipt. All six deployed colour assets matched
source. An exact loose-provider check found no competing enabled mod,
Overwrite or unmanaged Data provider for the DLL, colour INI or measurement
shader.

The authoritative catalog exposed only `communityshaders.nr_color` and
`communityshaders.renderscale` from CSX. That installed build has no producer registry,
public upscaling API, versioned screenshot service or profiler bridge. Its
AIO also lacks `CSX.BuildManifest.json`. The controller verified the process
and physical DLL hash but correctly withheld complete producer identity;
the NR status request was rejected before dispatch. No colour settings were
changed, captures started, or performance measurements taken.

This exposed an independent runner defect: its process double used a minimal
identity unlike the real controller receipt. The runner now pins the required
flat process/build/artifact fields and the fixture exercises that contract.
Catalog preflight reports the missing services before any colour mutation.
The runner workflow passes 39 tests and the adversarial assessment passes 10
tests. Both are included in the 13-target standalone suite. These fixtures
do not establish live rendering.

Raw evidence is retained locally under
`build/validation/nr-live-20260914`, including the catalog, scene/registrant
receipts, exact providers and `campaign-preflight/report.json`. The toolkit's
classification of observational NR status as mutation-capable is separately
recorded as `AUTO-20260914-225819913-3E698F52`. No exposure timing, correct NR
colour domain, stereo output commitment, headset presentation or performance
result is established by this blocked preflight. Those tests require a build
with the missing services and a subsequent game restart.

### NR branch service backport

The branch now includes the maintained screenshot coordinator, profiler and
canonical build-provenance generator from `main-VR`, adapted to this branch's
interfaces without merging its renderer history. The producer registry is
`communityshaders.build_api`; capture and profiling use
`communityshaders.screenshot` and `communityshaders.profiler`. The colour and
render-scale receipts also identify their producing build.

The DLL embeds a Build ID over source commit, dirty working-tree content,
dependencies and build options. Its adjacent `CSX.BuildManifest.json` binds
that identity to the linked DLL's SHA-256 and size. Packaging copies both.
Runtime startup does not open or hash the virtual MO2 DLL path; the harness
checks the physical enabled mod against the manifest and runtime producer.
An uncommitted build is explicitly marked dirty; its commit alone is not
sufficient to identify the compiled source.

Stereo capture retains each submitted texture, observes only successful
OpenVR calls, excludes post-load keepalive submissions, and stages only while
its render-target generation and device remain current. Eye pairs must share
one compositor cycle, publication generation and image contract. Target
recreation and screenshot source retention/staging share a mutex; the hook
releases it around the OpenVR call. Readback uses the immediate-context
protection helper and bounded asynchronous capture/encoding cleanup. The VR
capture indicator overlay is not included in this backport; capture state is
available in the CS menu and API. Successful submissions remain evidence of
compositor acceptance, not verified headset presentation.

The profiler preserves this branch's GPU scope callers and supplies captured
frame identity, slot counters, inclusive GPU totals and CPU self time. No
colour algorithm, NVIDIA admission policy, model runtime or rendering default
is changed. That backport used `NeuralColor` version `1-2-0`. The lighting-preservation
extension requires `1-3-0`; its INI and core registration remain checked by
CMake and asset verification. Shader caches and FOMOD are
excluded from the test AIO; existing installed caches are preserved.

Focused support tests are available independently of the plugin build:

```powershell
pwsh ./tools/cmake.ps1 -S tests/devbench_support -B build/nr-devbench-tests `
  -D 'nlohmann_json_DIR:PATH=<existing-vcpkg>/share/nlohmann_json'
pwsh ./tools/cmake.ps1 --build build/nr-devbench-tests --config Release
ctest --test-dir build/nr-devbench-tests -C Release --output-on-failure
```

These exercise main-thread cancellation/admission, screenshot pairing policy,
manifest snapshots, cancellation dispatch, Present cleanup, WARP profiler
timing, WARP context protection and build provenance. Run the original
13-target `tests/neural_color` suite and asset verifier as well. A new game
process must load the complete rebuilt package before the live identity,
capture and profiler smoke checks can establish availability.

### Live control findings and follow-up

The rebuilt service backport passed all 13 colour tests and all nine support
tests on Windows. Its live producer Build ID was
`170e37a498dd1854cc0100032a275a9cad4a43c3ca32b500e414e3d8d5e25519`.
The enabled AIO's physical DLL, adjacent manifest and eight package inputs
matched the preserved build receipt. Build identity and required service
discovery now pass in the loaded Dragonsreach session.

The automatic control sweep exposed three reporting gaps. Native resolution
can have a settled `Active` controller, so `nr_readiness` accepts native
`Idle` or `Active` while retaining its loading, compilation, publication,
device and resource-transition checks. The NR status, configuration, mode
cycle and reset actions report explicit `ok` results. The automatic-mask
contract always reports `requiredValue: true`, including character isolation.

`nr_configure` also exposes the existing session-only `experimentalMultiRoi`
boolean. It uses the menu's retirement/history transition even when the NR
master or character switch hides that experiment from the rendering cache
key. The default remains disabled and the setting is not persisted.

After the user enabled FOV during the sweep, NR evaluation and output-commit
counters advanced together with no observed latched backend failure. Earlier
cases with FOV disabled prove configuration/readback only and require a
repeat for active rendering coverage. These observations do not establish
correct colour treatment, exposure-hook timing, image quality, physical
headset presentation or performance. The observed null-HMD standing pose is
valid at 1.73 m, but the managed pose provider is unavailable, so the complete
automated presentation/performance prerequisites remain unmet.
