# Neural Rendering

Neural Rendering requires `nvngx_dlssnr.dll` at
`Shaders/Upscaling/Streamline/nvngx_dlssnr.dll`, relative to the installed
CSX mod folder. Its controls are disabled when this file is absent.

The menu follows the order in which choices affect the image:

1. Rendering mode, pipeline placement, optional FOV restriction in A/C
   (required in B), and strength application.
2. Shared image settings and colour processing.
3. Category strengths: actor types, materials and five strength sliders.
4. Actors only, followed by its processing area, distance, focus and edge
   controls.

Each toggle and slider has its own plain-language tooltip. Actor-type
toggles share one row; material toggles share another. The essential menu
retains pipeline choices and shared category/coverage controls.

**Adjust categories in scene NR** is opt-in and defaults off, including
when loading older settings. It changes selected materials while ordinary
NR continues elsewhere. For example, select Humans and Other humanoids,
then lower Face Strength to soften their faces while keeping ordinary NR
on the environment. Each material strength applies to every selected actor
type. Face, skin, hair, armour/clothing and weapon strengths blend from
the original image at 0 to full NR at 1. An unchecked material keeps normal
scene NR; a checked material at zero retains its original appearance.

**Actors only** takes precedence and reuses exactly the same selections
and strengths. Unselected surfaces retain their original appearance.
Its distance, focus, face-size, crop and edge controls apply only to this
restricted coverage. Turning it off restores the saved scene-adjustment
preference. Inactive actor-only controls and the saved scene preference
under Actors only do not invalidate NR history. Scene adjustments always
retain visibility rejection, independently of actor-only experiments.
Armour/clothing and weapon strengths default to 1, preserving
older profiles. The player, loose world items and transparent materials
remain outside actor-material detection. Environment material categories
are not introduced by these controls.

All categories share one model preset, intensity and style. There are no
per-category model configurations or extra evaluations per category.
Opting into scene adjustments adds actor detection and mask processing;
lowering a blend strength does not reduce the scene's inference area.
Actors only can still reduce that area through its crop controls. No
performance improvement is claimed without a matched runtime measurement.

Both **CSX compositor** and **NGX UIAlpha** use these strengths in A/B/C
and supported SE/AE mono routes. CSX blends the result directly. UIAlpha
sends the inverse strength mask to NGX and commits the provider result
without applying strength a second time. Scene adjustments retain a full
mask and evaluation rectangle within the configured NR region, including
when no actors are present. Mask generation subtracts selected material
reductions from full strength; zero-strength materials therefore still
participate in detection. Actor-only empty-selection bypass and sparse
mask experiments do not suppress ordinary scene NR.

Execution, actor, runtime and colour diagnostics and experimental controls
require a DevBench-enabled build and Debug or Trace logging (`Advanced` →
`Log Level` 1 or 0). Ordinary Info logging (2) shows rendering controls and
actionable availability notices. Pipeline placement remains visible at
ordinary logging levels.

Colour input experiments use private FP16 processing textures (FP32 when
the caller already uses FP32) in every NR route, including reduced
resolution. The baseline and reconstructed result retain the caller's
format, so the final copy never crosses incompatible texture formats.
Changing an input profile resets the existing input-history epoch.

The reversible colour proxy preserves RGB ratios and encodes brightness
as `log2(1 + maximum) / 32` before sRGB encoding. Its exponential inverse
round-trips pure colour transport across -8 to +8 EV without the former
brightness cutoff or reciprocal singularity. This does not establish
reliable model output across that range. Negative, nonfinite,
overflowing or destination-unrepresentable colours still retain the
original pixel; missing captured exposure still fails closed. Raw colour
uses Identity regardless of the saved experimental profile.

The colour regression checks cover UNORM and packed-float source images,
FP16 processing, both transforms, and -8/0/+8 EV. They verify finite output,
retained edits, source alpha and exact no-edit round trips. The ten focused
colour/settings/exposure checks passed in Release, including D3D11 WARP.
The shader suite also passed on the RTX 4090 and AMD integrated GPU; the
RTX 4090 run compiled without warnings. Existing lighting-preservation
fixtures remained bit-exact under strict and production shader flags.
These standalone checks do not execute NGX; live model checks and their
visual limitations are recorded below.

In native-resolution VR, A/B consume the prepared NR result at the final
scene boundary, before the engine draws its fade overlay and submits the
headset image. Hidden-area cleanup follows that overlay. The desktop
interface callback cannot consume VR NR work. SE/AE retain their existing
interface boundary; scaled submission and mode C retain their existing
routes. Both strength-application methods share this corrected boundary.
The VR call site is signature-checked before installation. A conflict
preserves ordinary rendering and reports the missing NR hook instead of
running a late desktop-only evaluation.

DevBench `nr_configure` exposes `characterArmorStrength` and
`characterWeaponsStrength` as finite numbers in [0, 1]. `nr_status` reports
them as `characterRendering.settings.armorStrength` and `weaponsStrength`.
Persisted rendering settings use `neuralCharacterArmorStrength` and
`neuralCharacterWeaponsStrength`.

`characterSceneStrengthsEnabled` enables scene adjustments independently
of `characterEnabled` (Actors only). It persists as
`neuralCharacterSceneStrengthsEnabled`. `nr_status` reports
`characterRendering.settings.sceneStrengthsEnabled` and `scope`
(`off`, `scene_adjustments`, or `actors_only`). Existing actor-type,
material and strength arguments are shared by both scopes. Invalid values
are rejected before mutation, and edits use the existing frame-boundary
history/reset contract. Missing or mismatched source capture retains the
existing baseline/failure behavior rather than using stale category data.

## Implementation consolidation (2026-10-06)

NR colour and depth-copy shader sources now live together under
`features/Neural Rendering/Shaders/Upscaling/NeuralRendering/`. Their
installed paths are unchanged. The ordinary depth copy retains output
coordinates; the diagnostic compact copy starts at the destination origin.
They remain separate shaders because these operations differ. Asset
verification covers both depth shaders and their renderer references.

One shared compute-state guard replaces the renderer, actor and colour
implementations. Renderer/actor callers retain bindings on entry and do
not alter predication; colour callers retain their existing entry-time
unbind and predication isolation. Each caller captures the same resource
slots as before. The existing `Utils/ComputeState.h` requires a D3D11.1
context and has a fixed three-SRV range, so it does not cover these D3D11
callers with up to five SRVs without broadening an unrelated contract.

The unused source-transition helper and its disconnected test assertions
are removed; capacity-fallback tests remain. Private mask-tile definitions
and actor empty-selection evidence move into their owning files, and the
single transport-submission method joins `D3D12Interop.cpp`. This removes
three headers and one implementation file while retaining the renderer,
interop, colour, actor-policy and test boundaries. The cleanup is separate
from the diagnostic colour fix and makes no performance claim.

Validation evidence is under `build/pr110-nr-consolidation/`:

-   The maintained shader verifier passed four permutations for each depth
    shader before relocation. Explicit old-path/new-path compilation then
    confirmed identical DXBC for all eight flat/VR and HDR combinations.
-   Two WARP graphics checks, eight controller checks, six source contracts
    and five colour/asset checks passed (20 distinct CTest cases). The guard
    test checks non-null shader restoration, constant buffers, resource-slot
    boundaries, both predication policies and null-context handling.
-   The universal SE/AE/VR Release DLL built with DevBench on, Tracy off and
    auto-deployment off. Producer base is `bf1a1cd22` plus `producer.patch`
    and the two preserved relocated shaders. Formatting hooks passed.
-   Build ID: `fcd8cfe83b2acb869858199aebb1b678b34030a70dae3e6552f083697db4916f`.
    DLL SHA-256: `6852c4a2ccaad064548ca15df6892aefe1f0eaf1281526f440e1be30c0f2ed00`
    (31,985,664 bytes). The adjacent manifest matches the linked DLL.
-   The existing AIO and running game were not replaced. This consolidation
    has no new in-game/HMD qualification or production-DLL build; the
    earlier live results describe their original producer builds.

### Adversarial consolidation review (2026-10-06)

The follow-up review found two asset-verification gaps: unexpected shader
paths outside the NR directory were silently ignored, and competing
feature/package copies could overwrite the consolidated shader owner.
Runtime references now match each producer's explicit inventory, including
escaped Windows paths; only the renderer's known actor-protection shader
is exempt from the NR mapping. Duplicate providers fail even when their
bytes match. The inventory also supplies the fixture and asset lists,
removing positional assumptions about which producer owns each shader.

The shared compute-state guard retained the original caller behavior.
Its WARP regression now exercises all six actual binding layouts, both
predicate values and normal/exception exits (24 cases), with distinct
shader bytecode and untouched resource-slot sentinels. No game code or
shader source changes were needed by this review.

Evidence is under `build/pr110-nr-refactor-review/`:

-   Before the corrections, the new asset fixtures reproduced 11 failed
    assertions covering unknown paths, wrong producers and duplicate
    providers. All 18 fixtures passed after correction.
-   `cmake --build build/pr110-nr-hmd-devbench --config Release --target neural_compute_state_guard_test --parallel 6`
    built successfully through `tools/cmake.ps1`.
-   `ctest --test-dir build/pr110-nr-hmd-devbench -C Release -R '^NeuralComputeStateGuard$' --output-on-failure`
    passed; the corresponding colour-suite selection
    `^NRColor(AssetVerifierFixtures|SourceContracts|AssetInventory)$`
    passed all three tests in `build/pr110-nr-hmd-gpu`.
-   The initial `prepare_shaders` attempt found the target unavailable
    because AIO packaging was disabled. Temporarily enabling
    `AIO_ZIP_TO_DIST`, while keeping auto-deployment off, allowed the
    maintained staging target to pass. Asset verification with
    `--deployed-data build/pr110-nr-hmd-devbench/aio` matched all eight
    NR assets by SHA-256, including both relocated depth shaders. The
    original packaging configuration was then restored.
-   This follow-up changes validation and documentation only. It does not
    rebuild the DLL, create a release archive, deploy to the game or add
    live SE/AE/VR qualification. Earlier producer identities and the
    unresolved stability observation remain unchanged.

### Final adversarial consolidation review (2026-10-06)

The final pass reproduced a constant-buffer binding defect retained from
the original guards: restoring b0 with the base D3D11 API changed a valid
`firstConstant=16, numConstants=16` window to `0, 4096`. The buffer object
was restored, but its shader-visible contents changed. The shared guard
now captures the D3D11.1 offset and range when that interface is available.
Whole-buffer, absent-buffer and older D3D11 contexts retain the base API;
partial bindings use the windowed restoration API. This follows the
existing optional-context pattern in `ScreenSpaceGI.cpp` without adding
another helper file or widening `Utils/ComputeState.h`.
The [D3D11 constant-buffer API contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-cssetconstantbuffers1)
defines the distinction between whole-buffer and windowed bindings.

The runtime shader inventory also accepted references inside comments
and rejected unrelated paths inside comments. Its literal scan now
handles line/block comments, C++ line splicing and quoted text together.
Four new failure assertions reproduced those incorrect classifications
before correction. This remains a source inventory, not a claim of C++
or shader compilation.

Evidence is under `build/pr110-nr-final-review/`:

-   `NeuralComputeStateGuard` passed all 72 WARP scenarios: six actual
    caller layouts, both predicate values, normal/exception exits, and
    absent/whole/windowed constant buffers. Its pre-fix failure records
    the observed `16/16` to `0/4096` change.
-   All 20 asset fixtures, colour source contracts and source inventory
    passed. The existing shader stage still matches all eight NR assets
    by SHA-256. No shader source or installed path changed.
-   Seven rendering contracts/actor shader checks and
    `NRColorExposureWARP` passed. The guard and exposure test targets were
    rebuilt through `tools/cmake.ps1` before their executable checks.
-   `cmake --build build/pr110-nr-hmd-devbench --config Release --target CommunityShaders --parallel 6`
    passed through `tools/cmake.ps1`. The universal DLL enables SE/AE/VR,
    with DevBench on, Tracy off and auto-deployment off. Its producer is
    `47b78a331d9861a5a4b7d476b6210b36ef97f48e` plus the saved
    `producer.patch`; subsequent documentation is not part of that build.
-   Build ID: `5c5fd9b4790f90a06e1cc4f113bdd90ab94f34f5da69979db40c75a1427d6a1b`.
    DLL SHA-256: `e51223dbfa34050faf251cd9f370cda557333e91ac7ac21d63769ef58603d02e`
    (31,986,176 bytes). The adjacent manifest matches the linked DLL.
    The previous DLL and manifest were preserved in `prior-producer/`.
-   No new AIO, deployment, production-only DLL build or live SE/AE/VR
    qualification was performed. The constant-buffer reproduction does
    not establish the cause of the earlier GPU hangs; that observation
    remains unresolved, and the diagnostic colour fix stays separate.

## Local validation (2026-10-06)

The PR110 implementation is based on
`b6eaeebcb61d8934061b6b57ad1d7d3b345c37d2` in
`codex/pr110-nr-ui-controls`. The local test archive retains its exact
dirty-source identity and patch in the adjacent build receipt.

Twenty-eight focused checks passed in Release after adversarial review.

The mask checks execute production HLSL on D3D11 WARP, including all 32
material selections at zero, fractional and full strengths in both
coverage scopes, empty scenes, occlusion and eye separation. Production
and diagnostic mask variants must produce identical pixels. Another 48
flat/VR cases exercise UIAlpha inversion and both compositor paths;
provider output is simulated, so these do not qualify the proprietary NGX
evaluation. Menu checks exercise the actual shared controls and require
individual, nonempty tooltips for every actor/material toggle and strength
slider. Settings, migration, request validation, history keys and retained
selection contracts are covered separately.

Adversarial review reproduced three failures before correction: isolated
equipment edges did not feather, inactive scope preferences changed the
history key, and typed material strengths exceeded slider limits. The
reviewed build fixes all three and checks mixed independent strengths,
invalid category IDs, preserved actor preferences and occlusion policy.
Material selection and slider drawing use shared implementations while
every control retains its own tooltip. Scene diagnostics report
`scene_full_region`, without claiming an actor-crop fallback;
`actorEnclosureCountsAvailable=false` identifies unmeasured enclosure
counts. History-key tests cover production and DevBench builds. Colour,
actor and shared model sliders also clamp typed input to their displayed
limits. Review evidence is preserved under `build/pr110-category-review/`.

The category-review archive predates live headset validation. The
subsequent headset fix and its runtime checks are documented separately.
SE/AE runtime testing and matched performance measurements remain unrun.

## Headset output and control validation (2026-10-06)

The tested universal Release DLL has DevBench enabled and Tracy disabled:

-   Producer source: `b6eaeebcb61d8934061b6b57ad1d7d3b345c37d2` plus the
    preserved dirty patch, not a later commit identity.
-   Build ID: `71d9915b02084e5349de2942745132c1f25765fdc96b8764c60aa4e23228240a`.
-   DLL SHA-256: `2e3581e95e369fca962ce1e02b379fdceecc7ca90aac2b6d2ddc20719b4604bf`.
-   Producer evidence: `build/nr-visible-20261006-1791293982506/`.

The Release DLL build, four focused controller checks, six NR contracts
and `NRFramebufferWARP` passed. The presentation-hook test exercises the
actual hook body and verifies target filtering and finalization ordering.

All exposed NR toggles were functionally exercised through DevBench in
the control sweep: 109 checks, 144 verified setting readbacks, no readback
mismatches, and 29 native stereo capture pairs. A/B/C, both strength
applications, scene adjustments, Actors only, all five materials, shared
model/colour settings and coverage controls were exercised. Principal
mode, strength and colour changes were visibly confirmed in both eyes.
Evidence: `build/nr-controls-review-20261006T143917200Z/resumed-20261006T150205917Z/review-summary.json`.

The debug sweep also exercised mask overrides, preview selection and
sampling, deterministic composition, depth testing, crop mode, all four
per-eye/batched and staged/direct A combinations, model-edit visibility,
transport bypass, exposure capture, asynchronous measurements, frame
evidence, stop/reset actions and colour experiments. It found two colour
processing defects: reduced-resolution transformed input was rejected,
and the +8 EV proxy fell back on many samples. Evidence:
`build/nr-debug-review-20261006T153141650Z/debug-review-summary.json`.

Functional verification is not an exhaustive visual or stability pass.
Only humans and one other humanoid were present; absent actor types were
checked for setting acceptance/exclusion. Physical menu clicking, hover
tooltips and in-menu preview textures were not separately inspected.
Frozen actors do not qualify temporal selection or moving occlusion, and
head-pose drift prevents exact pixel-equivalence claims. API-only
`experimentalCurrentContext`, `experimentalGpuMaskSupport` and
`compactInputs` were outside the visible debug-control sweep. Earlier
input-handling crashes interrupted two sessions; the completed sweep
reported zero renderer failures. SE/AE live testing remains unrun.

## Diagnostic colour fix and live retest (2026-10-06)

This change keeps the private processing-format correction and reversible
proxy correction in one separate commit for independent revert/bisection.
Managed colour and nondefault experiments are development-only. Raw
colour retains identity processing; production rejects the experimental
mode and overrides. Experiments are session-only and are not saved.

-   Producer source: `b6eaeebcb61d8934061b6b57ad1d7d3b345c37d2` with dirty
    digest `f2b31a6eb7896f807825ab0f3811db29c142d5830b194848e3898deac3cc7361`.
-   Build ID: `51b6352b33b4287d18563d35b4b24d36cea5090b59070e261507488a2a7188ca`.
-   DLL SHA-256: `232d06ad059a4a64d2a7764004bc54d2e3b946379cf383d9810ae15ea6115f6a`.
-   Build and offline evidence: `build/pr110-nr-colour-fixes-20261006T160739059Z/`.
-   Live evidence: `build/nr-debug-bundled-test-24536/summary.json` and
    `restoration-verification.json` in the same directory.

The DevBench-on, Tracy-off universal Release build and ten focused colour
tests passed. `nr_color_shader_gpu_test.exe` passed 18,558,823 checks on
each of the RTX 4090 and AMD integrated GPU. Archive integrity passed;
408 extracted FOMOD files matched and all 12 retained scene-cache files
were unchanged. The existing test AIO retains its dirty-source producer
identity; these commits do not relabel or rebuild that binary.

PID 24536 completed ten live conditions and 20 native HMD images with
zero renderer failures or device removals. Reduced-resolution
linear-to-sRGB now succeeds with both CSX and NGX UIAlpha. The +8 EV proxy
no longer has widespread forward rejection in A/C; a C CSX capture still
had sparse inverse guards (1/4095 left, 3/4096 right). Model-edit off/on
was checked in A. Both-eye inference/output and sampled finite values
were verified, and starting NR/colour settings were restored exactly.
Each condition used a verified noon reset and at least five seconds of
settling. These are targeted checks, not long-term stability qualification.

**Known diagnostic-only limitation, deferred:** Managed colour with the
reversible proxy at manual -8 EV produces severe green speckling on
selected actors in both A (Full resolution) and C (Renderscale), in both
eyes. This configuration is unavailable in production builds and is not
a production-setting defect. The DevBench test AIO can enable it. Finite
samples and successful evaluation do not constitute a visual pass.
The exact cause is unconfirmed; future work should establish a reliable
model-input brightness range, retain original pixels when reconstruction
is unstable, and validate real stereo images throughout that range.

**Separate unresolved GPU-hang observation:** two earlier launches of
this build (PIDs 16556 and 20336) reported `DXGI_ERROR_DEVICE_HUNG`
(`0x887A0006`) at `color_input_copy` before test mutations, with Raw colour
and no experimental input processing. The previous build's comparison
session and the final new-build retest did not reproduce it. Causation
remains unresolved; neither the successful retest nor this isolated
commit establishes or rules out a regression.
