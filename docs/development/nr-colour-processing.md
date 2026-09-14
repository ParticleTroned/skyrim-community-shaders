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

Modes remain `legacy_raw` (compatibility default), `managed`, and
`preserve_source`. Managed transfers the model residual relative to the actual
quantized prepared input, not an independently recomputed proxy:

`candidate = baseline + inverse(neural) - inverse(prepared)`

Preserve Source adds bounded, edge-weighted high-frequency log-brightness detail
to source RGB. Appearance mix accepts more of the reconstructed neural result,
including tone and chroma, not merely hue. This is an image-space mitigation,
not physical decomposition of reflections/shadows. Source alpha is retained.
The unknown/native-domain brightness metric is not calibrated luminance.

## In-game controls: no editable configuration INI

**Display > Neural Rendering Colour** provides:

-   **Enable colour processing**: retain the chosen mode/sliders while bypassing
    their application. The ordinary NR master switch remains in Upscaling.
-   **Apply neural edit (A/B; inference stays running)**: show the untouched
    baseline or the processed candidate without changing model input/history.
    This is a visual comparison, NOT an NR-off performance measurement.
-   Mode, detail contribution, appearance mix and maximum detail stops.
-   **Capture engine HDR exposure**, separate early/late domain/codec/exposure
    candidates, transport bypass, asynchronous measurements and override reset.
-   **Check installed colour shaders** and structured diagnostics.

Ordinary settings use the existing saved-settings JSON. Assessment profiles,
exposure capture, transport bypass and A/B state are transient. Both UI and
DevBench call the same compare-and-set registry; neither polls a configuration
file or performs GPU work from a DevBench worker thread.

`Shaders/Features/NeuralColor.ini` is ONLY a feature/version manifest, now
**1-2-0**. It is installed by the build; users do not edit it to assess colours.
The separate older `work/face-of-gogh-colour-20260914` experiment used an editable
`NRColor.ini`; that is not this branch's control interface.

## Automatic engine-exposure capture: concrete source evidence

The repository's `package/Shaders/ISHDR.hlsl`, BLEND permutation, reads AvgTex
at pixel-shader slot **t2** and multiplies input colour by **AvgTex.y / AvgTex.x**
when both components are nonzero. The zero-component branch leaves exposure at
one. `Common/FrameBuffer.hlsli` subsequently applies `pow(abs(colour),
FrameParams.x)`; its function name does NOT make that the IEC sRGB curve.
FrameParams is at PS b12 c84 for VR and c42 for flat rendering in this source.

`ExposureCapture` observes the two known cinematic HDR tonemap effects through
PRIMARY `BSShader::RestoreTechnique` slot 3, before chaining the previous hook.
It obtains the concrete effect instances through the pinned CommonLib
ImageSpaceManager API, not guessed module offsets. Installation runs from the
normal render-thread EarlyPrepass. It never replaces a NVIDIA entry point.

Capture accepts a bound scalar 1x1 floating-point AvgTex view with at least two
channels. `ColorExposureCS` writes raw average, raw target, ratio and validity
into a private FP32 texture. Nonfinite input, unsupported resources and missing
bindings are reported. The engine's zero-input unit fallback has a distinct
validity code; it is never presented as a measured unit exposure. Ratios outside
1/256..256 are outside the current experiment's supported range.

Eight bounded records retain source-frame, capture epoch/sequence, resource/
view/output formats and process-local shader/resource identities. Optional
nonblocking readback also records the actual frame-gamma exponent. These are
producer observations, not claims about NVIDIA's expected model domain.

For a captured-exposure profile, preparation snapshots the matching source
world frame into transaction-owned storage. Both eyes and all their regions
reuse the first latched availability/value. An API toggle cannot switch that
value halfway through a pair. Reconstruction uses this same immutable snapshot;
there is no later lookup of a global/latest exposure and no per-face metering.
Manual calibration is multiplied by the captured ratio on the GPU. No render-
thread readback wait or new Flush is introduced.

**Timing qualification is essential.** If this RestoreTechnique boundary has
already lost PS bindings, the capture is unavailable. If the HDR pass happens
after an early NR invocation, that invocation cannot use a future exposure.
The code reports the mismatch rather than borrowing a previous frame. A later
insertion can be assessed separately. Different adaptation sources within one
frame are marked ambiguous. The hook and these route conditions have not been
qualified in a running Skyrim process here.

Captured HDR exposure is NOT automatically DLSS SR's internal exposure, NR
pre-exposure, or proof that the submitted colour has not already been exposed.
The engine formula is now instrumented; model-domain and placement hypotheses
are tested by the assessment runner rather than assumed from texture format.

## Profiles, history and resource ownership

Early `upscaled_center` and late `final_ldr_pre_ui` profiles are independent.
Each declares an explicit domain candidate (`unknown`, `linear`, `srgb`), a
transform (`identity`, `linear_to_srgb`, `reversible_proxy`), a calibration
multiplier and exposure source (`manual`, `captured_hdr`). Identity requires
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

## DevBench API v2

Tool: **communityshaders.nr_color**. Actions: `status`, `configure`,
`reset_experiments`, `assets`. Responses explicitly report `ok` and error codes
so automation/dev can enforce semantic success. Configuration acknowledges
registry state, not completed GPU work; require processed, fresh measurements.

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
python tools/nr-color/assess.py --include-captured
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
checks. Captured candidates require matching source-frame exposure; missing
exposure, invalid inverses and zero-error fallback are not a pass. Optional
shown-edit and hidden-edit capture episodes retain state/frame manifests for
visual review. Those images are separate live frames, not falsely labelled the
exact same frame as a numeric sample. Keep camera/scene stable.

The runner journals requests/responses, pins the returned runtime identity,
uses semantic/performance-neutral guards, and never retries an indeterminate
mutation. It restores only its owned configuration under a revision check;
a concurrent UI/agent change is preserved and recovery evidence is retained.

Output ranks **source RGB drift**, not physical material correctness. Successful
inverse round trips cannot identify a model's expected colour domain: all
consistent invertible candidate pairs can pass. `domainVerified` therefore
remains false and no production profile is silently selected. Use the collected
producer evidence and A/B images to adjudicate the candidates across scenes.
This implements automatic data collection/assessment, not fabricated certainty.

## Shader inventory, deployment and tests

All required shaders are in the colour feature package: ColorPrepareCS,
ColorReconstructCS, ColorMeasureCS, ColorExposureCS, plus ColorCommon.hlsli.
Normal recursive source/feature discovery and shader-copy paths include them.
Install the rebuilt plugin AND feature/shader files. The manifest is 1-2-0.

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
headset presentation. Deployment must retain the `1-2-0` colour package
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
