# NR Lighting preservation — 2026-09-17

## Change and compatibility

Starting head: d69bdb7ebc899ce24971eac59b007503e0dc08bb, verified clean on
work/face-of-gogh-colour-managed-20260914 after fetching origin (zero commits
ahead/behind). This is a separate local implementation; the existing
capture-owned companion machinery remains intact.

The ordinary saved setting **lightingPreservation** is a finite float in
[0,1], default 1. Legacy saved objects load at 1. Partial live configure
updates retain the current value; saving/loading round-trips it. Restore
defaults selects 1, while reset assessment overrides preserves it. The
appended member preserves existing positional Settings initializers.
Legacy Raw remains the default mode.

Let r be the existing neural log-luminance residual, L its existing
source-weighted adjacent 3-by-3 estimate, and p preservation:

    accepted = r - p * L
    stops = clamp(accepted * detailStrength * edgeWeight, -limit, limit)
    gain = exp2(stops)

The shader and CPU reference explicitly retain r - L at p=1 and r at p=0.
No radius, pass, resource, model-input conversion or exposure policy is
added. Adjacent taps, packed-nearest rounding, source alpha, validity/range
fallbacks, four-pixel ROI fading and downstream character masking remain.

100% suppresses the full local smooth estimate; 50% removes half; 0%
admits the full bounded neural brightness residual. Detail contribution
scales the accepted residual, including broader changes below 100%.
Appearance Mix independently admits the reconstructed candidate, including
colour and appearance; at 1 it bypasses preservation. The stop limit
bounds the preservation branch, not that full-candidate endpoint. This is
image-space processing, not a physical lighting/material decomposition or
proof of the NVIDIA model's colour domain.

## Transaction and evidence

The shared renderer applies this to standard NR, character selection,
single/multiple ROIs, both insertion points and both render routes.
Changing only preservation advances the revision without changing either
inputEpoch or requesting exposure capture. No-op updates retain the
revision. Existing revision mismatch fallbacks remain; inference-history
and model recreation logic are unchanged.

The GPU constant comes from Work.configuration. Observations latch it at
preparation; the renderer also records the retained value for captures
that bypass colour processing. Asynchronous measurements copy that
observation. Existing ObservationEvidenceJson, MeasurementBatchEvidenceJson
and frozen screenshot configuration serialize it without reading the
current menu state.

Python projections retain the optional setting during configuration and
restoration. Assessments require matching values in every available
physical region and capture-owned measurement companion; new colour
captures require both eyes' regions. Comparisons use float32 identity,
matching the runtime setting. Old evidence remains importable and is
labelled **absent_legacy_evidence**; absence is never proof of 100% in a new
slider assessment. Configuration-only evidence is explicitly distinguished.

## GUI and API

Open **Display > Neural Rendering Colour**, enable colour processing and
select **Preserve Source**. **Lighting preservation** is a true 0–100%
widget: its temporary percentage is divided by 100 only on an edit.
Values remain saved when changing mode or disabling colour processing.
The GUI annotates the Appearance Mix=1 bypass without rewriting the value.
Typed percentage input is clamped to the widget's range. The API rejects
out-of-range JSON numbers before float rounding can hide the violation.

Use the existing **communityshaders.nr_color** tool. Read action=status,
then use its current revision:

```json
{
    "action": "configure",
    "expectedRevision": 42,
    "settings": {
        "enabled": true,
        "mode": "preserve_source",
        "lightingPreservation": 0.5
    }
}
```

42 is an example; use the returned revision. This is an additive optional
field; settings schemaVersion 1 and tool API version 3 remain unchanged.

This is a CSX DLL/shader change. DevBench itself does not need rebuilding.
The typed tool schema comes from the running CSX DLL, not a static schema
in the automation plugin. After installing the matching DLL and shader
assets, load the game and fully reload the Codex host to discover
lightingPreservation. Plugin cache rotation alone cannot add a field to
an older producer. Use the exposed direct tool schema; do not bypass it
through an alternate transport. The separately authorized automation
marketplace refresh documents this sequence.

## Build and validation record

Evidence: build/validation/nr-lighting-preservation-20260917/ in this
worktree. The root checkout supplies the cmake.ps1, git.ps1 and
pre-commit.ps1 launchers because this worktree predates them. Build
directories were checked against this worktree before use.

The universal SE/AE/VR Release DLL was built with DevBench ON and
AUTO_PLUGIN_DEPLOYMENT, ZIP_TO_DIST and AIO_ZIP_TO_DIST OFF.

| Check                                                       | Result                                                                                                        |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Universal CommunityShaders Release target                   | Final committed-source build and archive verification recorded in the adjacent AIO receipt                    |
| Full tests/neural_color CTest suite                         | 20/20 passed, 14.32 seconds                                                                                   |
| Actual WARP shader harness                                  | 14,208,936 checks passed                                                                                      |
| Explicit NVIDIA adapter 1, RTX 4090 (10de:2684)             | 14,208,939 checks passed                                                                                      |
| Latest Python assessment/capture/source/asset/runner checks | 144 tests passed, none skipped                                                                                |
| Scoped pre-commit                                           | Passed applicable whitespace, line-ending, clang-format and prettier hooks; YAML/gersemi had no matched files |
| git diff --check                                            | Passed                                                                                                        |
| Rebuilt C++ screenshot/capture suite                        | 6/6 passed, 0.31 seconds                                                                                      |
| Live Skyrim/NGX/HMD visual assessment                       | Not run                                                                                                       |

The user subsequently authorized adversarial review, builds, an AIO and
automation marketplace/cache refresh. Review evidence is retained under
build/validation/nr-lighting-review-20260917/. The review reproduced an API
boundary failure using the next double above 1 and a tiny negative double:
both rounded into range before validation. The fix retains Number() for
type/finiteness checks and validates the original JSON range before
assignment. Regression tests verify atomic rejection. Typed GUI edits now
use the repository's AlwaysClamp pattern.

The review also tightened the new analytic GPU checks from fixed absolute
tolerances to channel-magnitude and format-dependent storage bounds.
Both WARP and RTX 4090 pass these tighter checks. No shader arithmetic,
filter radius, mask policy, history policy, or capture retention code was
changed during review. The final AIO receipt records the clean source
commit, producer Build ID, DLL hash and complete extracted payload hashes.

The GPU tests compare p=1 bit-for-bit against immutable pre-change
production DXBC: 180 cases per compiler configuration, 360 per device.
An additional 36 invalid-range comparisons per device retain the old
fallback. The final logs print the 180 p=1 comparisons per configuration
separately from the invalid-range checks.
Source hashes and compiler flags are retained in
tests/neural_color/fixtures/preserve-source-1-2-0/baseline.json.
There is no second production shader implementation.

Coverage includes uniform doubling/halving and constant residuals,
smooth ramps plus checker detail, existing stripe/checker regressions,
selected-domain common RGB gain, ROI edges and untouched surroundings,
identity, zero contribution, stop limits, hidden edit, full appearance,
transport, near-black/invalid/range fallbacks, alpha, actual FP32, FP16,
R11G11B10 and UNORM UAV stores, and exhaustive packed rounding.
Compatibility comparisons are exact. Analytic comparisons allow documented
storage error; sRGB decode/encode plus FP16 stores need not reproduce
source bits even in the pre-change shader.

The production parser, configure handler, registry, save/load/default
methods, latching assignments and serializers compile into the standalone
settings test using the repository's source-extraction pattern. Tests cover
atomic rejection, old saves, partial settings, revision guards/no-op,
unchanged epochs, no exposure request and delayed evidence.
Game/UI and exposure-device endpoints are test doubles; this does not
replace live stereo/NGX validation.

Exact commands run (paths shortened with shell variables):

```powershell
$root = 'D:/Coding/GitHub/skyrim-community-shaders'
$work = "$root/.tmp/worktrees/face-of-gogh-colour-managed-20260914"
$env:PYTHONPATH = "$work/build/validation/hmd-python-deps"
& "$root/tools/cmake.ps1" -S "$work/tests/neural_color" -B "$root/.tmp/hmd916-tests-color" "-Dnlohmann_json_DIR=$root/.tmp/hmd916/vcpkg_installed/x64-windows-static-md/share/nlohmann_json"
& "$root/tools/cmake.ps1" --build "$root/.tmp/hmd916-tests-color" --config Release
ctest --test-dir "$root/.tmp/hmd916-tests-color" -C Release --output-on-failure
& "$root/.tmp/hmd916-tests-color/Release/nr_color_shader_gpu_test.exe" "$work/features/Neural Rendering Colour/Shaders/Upscaling/NeuralRendering"
& "$root/.tmp/hmd916-tests-color/Release/nr_color_shader_gpu_test.exe" "$work/features/Neural Rendering Colour/Shaders/Upscaling/NeuralRendering" --hardware 1
& "$root/tools/cmake.ps1" --build "$root/.tmp/hmd916" --config Release --target CommunityShaders
& "$root/tools/cmake.ps1" --build "$root/.tmp/hmd916-tests-devbench" --config Release
ctest --test-dir "$root/.tmp/hmd916-tests-devbench" -C Release -R Screenshot --output-on-failure
# CPU-only final checks, run after the pause:
foreach ($test in @('assessment_test.py', 'test_hmd_capture.py',
    'test_hmd_assess.py', 'source_contract_test.py', 'asset_verifier_test.py',
    'adversarial_assessment_test.py', 'runner_test.py')) {
  python "$work/tests/neural_color/$test"
}
```

Initial failures were corrected before the successful runs: Windows
FileTracker path length in the baseline generator (rebuilt in a shorter
ignored directory), FP32 fixture initialization, overly strict new sRGB
fallback assertions, and the asset verifier's old feature version.
Original compatibility tolerances were not relaxed. Baseline and updated
shader compilation report existing X3571 pow/negative-input and X4000
EffectiveExposure warnings. The plugin reports the existing FidelityFX
CMake CMP0116 deprecation warning; no new C++ warning appeared.

Formatting was scoped to changed files. ColorCommon.hlsli used
clang-format --style=file --lines=15:20 --dry-run --Werror to retain
unrelated legacy formatting; its only changes are the appended CB fields.

## Deployment and remaining live test

Feature manifest, checked-in registry and normally generated metadata are
1-3-0. The shared NRColorCB is 64 bytes: existing offsets remain unchanged,
exposure is at 32, lighting preservation at 48, and three padding floats
are initialized to zero. All test mirrors match. The separate exposure
producer constant buffer is unchanged.

The requested AIO is built with DEVBENCH_BRIDGE ON, automatic deployment
OFF, and without a FOMOD or compiled shader cache. Later deploy its CSX
DLL, feature manifest and complete colour shader set together through the
normal feature-version/cache mechanism. Do not mix 48-byte and 64-byte
contracts. Packaging does not deploy, restart the game or push the branch.

Then use the same approved scene, input profile and ROI policy:
Detail contribution=1, Appearance Mix=0 and the existing stop limit.
Compare Lighting preservation=100%, 50%, 0%, each with a matched hidden
edit control, exact capture-owned companions and both native eyes.
Sequential live screenshots are not same-input tests. No frozen replay
machinery was added. The earlier 156-pair review does not validate this
control; these tests establish mathematical, storage and integration
contracts, not improved visual quality or measured runtime cost.
