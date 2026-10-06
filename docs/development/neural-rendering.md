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

## Local validation (2026-10-06)

The PR110 implementation is based on
`b6eaeebcb61d8934061b6b57ad1d7d3b345c37d2` in
`codex/pr110-nr-ui-controls`. The local test archive retains its exact
dirty-source identity and patch in the adjacent build receipt.

Twenty-eight focused checks passed in Release after adversarial review:

```powershell
$build = 'D:/Coding/GitHub/skyrim-community-shaders/build/pr110-category-strengths-devbench'
ctest --test-dir $build -C Release -R '^(character_settings|NeuralRenderingUI|NeuralRenderingRequest|NeuralSettingsKey(Bridge)?|NeuralFeatureSettings(_off)?|NeuralProductionPolicy_(on|off)|NeuralReducedSelection|NeuralPreparedSelection(Bridge)?)$' --output-on-failure
ctest --test-dir $build -C Release -R '^(ActorBlendingShader|character_mask_shaders|NeuralRenderingControls)$' --output-on-failure
ctest --test-dir $build -C Release -R '[Nn]eural.*[Cc]ontract' --output-on-failure
ctest --test-dir $build -C Release -R '^(character_region_policy|character_actor_policy|character_mask_work_policy|character_focus|character_crop|character_material|NeuralControllerToggle)$' --output-on-failure
```

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
