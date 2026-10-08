# Adaptive Balance color, atmosphere, ambient and water controls

Adaptive Balance exposes these controls in Global, time/interior profiles,
and location layers. Sky Saturation is beside Sky Brightness in Lighting's
detailed controls. Clouds now have independent controls. Caustics and Parallax are in Water's detailed controls.
The Water detail checkbox controls visibility; Lighting's detailed-control
checkbox also enables its detailed adjustments, including Sky Saturation.

| Control                   | Neutral | Range         |
| ------------------------- | ------- | ------------- |
| Contrast                  | 1       | 0.5–2         |
| Saturation                | 1       | 0–2           |
| Sky Saturation            | 1       | 0–2           |
| Cloud Brightness          | 1       | 0–2           |
| Cloud Saturation          | 1       | 0–2           |
| Cloud Gamma Offset        | 0       | −1–1          |
| Vanilla Fog Intensity     | 1       | 0–5           |
| Sun Glare Intensity       | 1       | 0–5           |
| Effect Brightness         | 1       | 0–2           |
| Sky Static Brightness     | 1       | 0–2           |
| Sky Static Transparency   | 0       | 0–1           |
| Ambient                   | 1       | 0–5           |
| Caustics Strength         | 1       | 0–2           |
| Caustics Tiling           | 1       | 0.25–4        |
| Caustics Speed            | 1       | 0–3           |
| Caustics Color Dispersion | 1       | 0–2           |
| Parallax Strength         | 1       | 0–2           |
| Parallax Quality          | 16      | 4–64, integer |

Zero Sky Saturation produces a grayscale sky; Cloud Saturation controls
clouds separately. Zero caustics strength disables
caustics, zero speed freezes their animation, and zero dispersion removes
their color separation. Zero parallax strength disables water parallax.
Higher parallax quality increases sampling cost. VR retains its existing
foveated detail reduction. Caustics require Water Effects, and parallax
requires its water-parallax option.

Layers multiply the float adjustments. Parallax Quality composes relative
to its neutral value of 16 and rounds to an integer: Global 16 with a
profile value of 32 gives 32; Global 32 with that profile gives 64. Location
replacement retains Global and replaces the time/interior profile; additive
locations retain both. Transitions interpolate the composed outputs and
round quality to the nearest integer. Values are bounded after composition.
Missing saved fields take neutral defaults, except cloud controls inherit
legacy sky adjustments as described below. Disabling Adaptive Balance
restores neutral outputs without discarding the saved adjustments.

## Color

The **Color** tab exposes Global Contrast and Saturation in Essential and
Advanced views. Advanced view also exposes time/interior profiles and
location layers. These controls work independently of the detailed Lighting
switch and Linear Lighting. Existing settings and presets without these fields load neutral
values. Contrast and Saturation multiply through the same bounded layer
composition and day/night interpolation as the other float adjustments.

The whole-scene controls complement the lighting and bloom controls already
available in Adaptive Balance. The reference
[IMAGINATOR](https://www.nexusmods.com/skyrim/mods/13049) offers general
contrast and saturation through engine image-space adjustments. CSX applies
its own grading in the existing HDR blend shader, using its composed scene
and location-aware settings. No code or assets from IMAGINATOR are used.

Grading runs after tone mapping, bloom and authored image-space color
adjustments, before fades and final display encoding. It reuses
`Color::RGBToLuminance`, `Color::Saturation`, and Linear Lighting's safe
gamma conversions. With Linear Lighting off, only the active grading path
converts to linear light and back. Existing lighting-input gamma controls
remain independent because they operate before scene composition.

Contrast scales RGB by a luminance power curve around linear middle gray
(0.18). This preserves color ratios, keeps black at zero, and avoids the
hard shadow cutoff of an affine contrast adjustment. Values below one
soften contrast; values above one deepen shadows and raise highlights.
Saturation zero makes the composed scene monochrome, including the sky
and bloom. Saturation can clip negative channels when boosted, as in the
shared color helper. Grading uses channel magnitudes to match the existing
absolute-value display encoding and restores authored signs before fades.
This avoids clipping dark image-space colors when a control leaves neutral.
HDR values are not clamped to one. Subsequent engine fades and UI can still
contribute their own colors.

Exactly neutral values bypass grading, including the gamma round trip.
The master/runtime gates emit neutral values. Config loading and every
composed layer bound the controls and replace non-finite inputs with one;
DevBench rejects invalid updates before mutation. Contrast and Saturation remain at offsets 40 and 44 in the Adaptive
Balance buffer. Atmosphere fields append data after them, extending the
buffer to 80 bytes. It adds no resources, sampling, or render passes and uses
the same per-pixel math for SE, AE, and both VR eyes. Runtime cost has not
been measured.

## Atmosphere

The eight atmosphere controls belong to Lighting's detailed controls in
Global, time/interior profiles, and location layers. They share that
layer's detailed-lighting gate. Color's Contrast and Saturation remain
independent of this gate. All new controls have neutral defaults.

Cloud Brightness and Saturation replace the formerly shared sky controls
for `CLOUDS` permutations. Cloud Gamma Offset applies to the active Linear
Lighting sky-gamma baseline, or one when Linear Lighting is off, with the
same Scene Brightness response as sky gamma. Sky and cloud offsets then
compose independently. Effective gamma remains bounded by the existing
gamma limits. The runtime cloud gamma occupies offset 104 in Linear
Lighting's unchanged 112-byte buffer; its base is not a separate saved
Linear Lighting setting.

Older saved Global/profile/location values seed absent cloud brightness,
saturation, and gamma fields from their corresponding sky fields before
source layers merge. Explicit cloud values, including zero, win. Imported
profile and base presets use the same migration. Cloud brightness retains
the legacy sky multiplier's wider saved-value/composition bounds so old
presets keep their appearance; the UI and DevBench range is zero to two.

Vanilla Fog Intensity scales distance-fog opacity after its existing gamma
curve. One preserves the original result exactly; other values are clamped
to valid opacity. This is independent of fog gamma and Volumetric Lighting's
existing Shaft Intensity and Opacity controls. Inventory previews retain
their existing fog.

Effect Brightness and Sky Static Brightness normally scale the current
weather's Effect Lighting and Sky Statics colors after the engine updates
them. One preserves the authored weather colors. The existing Effects
lighting multiplier independently scales directional and point lighting.
Neither control edits weather records. Profile composition, transitions
and location layers use the same bounded multipliers as other controls.

Changes, including disabling Adaptive Balance, take effect on the next
weather-color update. Before that update, the previous adjustment is
restored only if the same live sky still contains our output; later
external color edits are retained. Repeated updates do not accumulate
our scale. Main/loading menus, unloaded Adaptive Balance and performance
measurement bypasses use neutral values. A failed detour installation is
logged and reported as unavailable; other balance controls remain usable.
The SE/AE/VR relocation and update timing still require runtime validation.

Ambient Lighting for Effects and Sky Statics is an optional global switch
under Global > Lighting, independent of detailed lighting controls. It is
off by default and saved as `Adaptive Balance.useAmbientEffectLighting`.
Global and full presets include it; older presets default it to false.
The master toggle, unloaded feature, main/loading menus, missing player
cell and performance-measurement bypass all disable its effective value
without changing the saved selection.

When enabled, in-world lit effects and classified or explicitly flagged
sky-static meshes use combined ambient/IBL and half-strength directional
light in the renderer's existing lighting space. Lit effects retain
Skylighting occlusion and sample scene shadows only when the scene reports
directional shadows, including interior sunlight. Non-lit sky statics use
eight terrain/cloud shadow samples along a bounded view ray, without a
scene shadow-mask dependency; soft effects bound the ray by sampled depth.
Both paths retain ambient balance and per-eye positions. Material colour,
lighting influence, point
lights, fog, transparency and effect multipliers keep their existing roles.
Inventory previews keep their existing lighting. The weather hook remains
available to other consumers; replaced shader lighting does not consume
those weather colours, so brightness is not applied twice.

Effect Brightness and Sky Static Brightness scale the replacement lighting
with their composed global/profile/location values. Their detailed-lighting
gates and bounds remain unchanged. Disabling the switch selects the existing
weather-based paths. The 80-byte buffer reuses padding: a uint switch at
64, transparency at 68, effect brightness at 72 and sky-static brightness
at 76. No resources or render passes are added. Appearance, shader
permutations and runtime cost still require the deferred final validation.

Sky Static Transparency retains the sky-static effect permutation and
material predicate, including mountain-mist meshes. It fades ordinary alpha,
additive, and multiplicative outputs toward each blend mode's neutral value;
one discards the effect. Layered transparency composes as
`1 - (1 - inherited) * (1 - layer)`, so a neutral layer preserves the
inherited fade. Existing alpha testing and effect gamma remain in place.

Sun Glare Intensity scales the engine's existing `DITHER` + `TEX` sky pass
in SE/AE and VR. It cannot restore missing glare or weather lens flares.
Glare visibility remains runtime-unverified; the rejected #733 masking
change was already unnecessary for this branch's shader path. The existing
VR path and SE/AE centered dithering remain intact.

## Ambient

Ambient scales the combined vanilla or image-based ambient contribution.
It applies after IBL's environment/sky mix and DALC matching, including
when DALC Amount is zero. Matching uses the original ambient input, so
turning it on does not apply the balance multiplier twice. Linear Lighting
retains its independent ambient gamma and multiplier.

Global, active profile and location adjustments compose with Scene
Brightness's ambient response before reaching the shader. The final
multiplier is applied once to diffuse ambient and ambient reflections;
linear-space reflections use the corresponding converted multiplier.
A composed value of one preserves the current lighting, and zero removes
that ambient contribution. Direct lights, emissive materials and separately
computed SSGI bounce lighting retain their own controls. Inventory previews
retain their existing lighting.

IBL-derived fog receives the same adjustment before its optional luminance
preservation. Water's refraction decomposition does not reapply Ambient to
the already-lit scene texture.

## Waves and Wind

Water's **Waves and Wind** group puts Base Wave Amplitude, the wind-enable
controls, Calm Wave Scale, and Strong Wind Wave Scale together. It remains
visible in Advanced view even when detailed water controls are collapsed.
Base Wave Amplitude remains editable with wind disabled.

The controls are complementary. For each composed profile branch:

```text
wind scale = interpolate(calm scale, strong-wind scale, smoothed wind)
wave amplitude = clamp(base wave amplitude × wind scale, 0, 2)
```

With wind disabled, only the base applies. Day/night transitions blend the
branch results. Wind Response reads the engine wind without changing it;
interiors use calm wind. Grouping these controls changes their presentation,
not their saved values or composition behavior.

## DevBench

`communityshaders.menu` accepts a partial Global update:

```json
{
    "action": "set_adaptive_balance_visuals",
    "expectedBuildId": "<loaded DLL Build ID>",
    "visuals": {
        "contrast": 1.1,
        "saturation": 0.9,
        "ambient": 0.5,
        "skySaturation": 0.8,
        "cloudSaturation": 1.0,
        "fogIntensity": 0.8,
        "lightingAdvanced": true,
        "causticsStrength": 1.2,
        "parallaxQuality": 24
    }
}
```

`visuals` must be a nonempty object containing only `contrast`, `saturation`,
`ambient`, `skySaturation`, `cloudBrightness`, `cloudSaturation`,
`cloudGammaOffset`, `fogIntensity`, `sunGlareIntensity`, `effectBrightness`,
`skyStaticBrightness`, `skyStaticTransparency`,
`lightingAdvanced`, `causticsStrength`, `causticsTiling`, `causticsSpeed`,
`causticsDispersion`, `parallaxStrength`, or `parallaxQuality`.
Numeric bounds match the table; quality must be an integer and
`lightingAdvanced` must be boolean. Invalid updates are rejected before
mutation. Adaptive Balance must be loaded. Updates run on the main thread,
preserve omitted fields, and stage settings without saving or changing the
master enable state. `lightingAdvanced` changes the existing Global detailed
Lighting switch, so it also governs the other detailed Lighting adjustments.

Status exposes configured Global and composed effective values under
`adaptiveBalanceVisuals`. Effective values include active profile layers
and the master/runtime gate; they do not imply that Water Effects is loaded
or that glare is visible. Effective `cloudGamma` reports the composed gamma,
including the active Linear Lighting baseline; configured `cloudGammaOffset`
reports only the saved Global offset.

`adaptiveBalanceWeatherColorsAvailable` reports successful installation of
the weather-color hook. Configured/effective brightness values describe
the requested adjustment; availability does not establish that a weather
update has applied it. This status is exposed through `communityshaders.menu`.

## Regression coverage

The following executed Color results belong to the unchanged Color base
`41e91ef48a730e073480f5024b5ae11115125c99`, before the atmosphere extension.
They are historical evidence, not validation of the extended buffer or shaders.

The color extension adds executed `AdaptiveBalanceToggle` cases for neutral
defaults, finite bounds, day/night interpolation, layered and replacement
locations, independent detailed-lighting gating, and off/on restoration.
`AdaptiveBalanceColorShader` executes 288 samples on D3D11 WARP across
SE/AE and VR, Linear Lighting on/off, and the minimum, neutral and maximum
settings. It checks exact neutral bypass, black/middle-gray preservation,
contrast direction, hue ratios, monochrome output, finite near-black and
HDR values, negative inputs, color-space agreement, and buffer offsets.
It also executes 384 draws through eight production `ISHDR` blend
permutations, with Adaptive Balance and fades independently on/off, both
tone mappers, Linear Lighting on/off, and colored scene/bloom inputs.
These verify neutral equivalence with the feature compiled out, continuity
near neutral, whole-scene desaturation, unchanged full fades and matching
stereo eyes. All passed with FXC warnings as errors.

Adversarial review reproduced a shadow-clipping regression in the initial
implementation: authored contrast can yield negative intermediate colors,
so clamping them when Contrast moves from 1 to 1.000001 changes the output
abruptly. The production-pass regression failed before the sign-preserving
fix and passed afterward. The review also corrected the Essential-view
documentation; profile and location controls require Advanced view.

The complete production validation exposed an unnecessary preset contract
revision bump: generated presets advertised revision 6 while the runtime
accepts revision 5. These optional, neutral-default fields do not break the
existing settings contract. Keeping revision 5 preserves compatibility with
existing marked presets; the source fingerprint still tracks the new code.
The shipped-preset compatibility test covers all three generated tiers.

The maintained CMake targets `adaptive_balance_color_shader_test` and
`adaptive_balance_toggle_test` were built in Release and both executables
passed. Production packaging must build the DLL and both runtime cache
packs from the final clean source commit; the shared shader-data change
prevents substituting the unchanged 3.19.2 cache packs.

Both changed production C++ translation units compiled with universal
SE/AE/VR definitions and MSVC `/W4 /WX /fp:fast`, using the existing
dependency tree. The standalone compile suppresses existing C4099 and
CommonLib C4245 warnings. Compiler responses and logs are preserved under
the worktree's `build/color-validation/`:

```powershell
pwsh ./tools/run-msvc-command.ps1 cl '@build/color-validation/AdaptiveBrightness.rsp'
pwsh ./tools/run-msvc-command.ps1 cl '@build/color-validation/MenuDevBenchBridge.rsp'
./build/color-validation/adaptive_balance_toggle_test.exe
./build/color-validation/adaptive_balance_color_shader_test.exe
./build/color-validation/visuals_validator_test.exe
```

The extracted production DevBench validator passed 20 valid/invalid update
cases; the registered JSON schema's bounds match those checks. Unified
preset compatibility metadata was refreshed for the additive settings
contract; missing Color fields remain neutral in existing presets.
`pwsh ./tests/unified_preset_generator_test.ps1` and
`pwsh ./tools/generate-unified-presets.ps1 -Check` passed. The generated
preset settings differ only in their compatibility metadata.

Deployment, in-game DevBench/UI validation and headset visual assessment
have not run. Build manifests and local validation receipts record the
separate production DLL and package checks.

`AdaptiveBalanceToggle` includes production-code cases for neutral defaults,
finite bounds, profile/location composition, quality rounding and clamping,
sky saturation gating, master off/on restoration, and base/wind composition.
These cases require building the test target to execute.

`AmbientBalanceShader` executes FXC-compiled HLSL on D3D11 WARP for SE/AE
and VR permutations. It covers vanilla and IBL diffuse ambient, occlusion,
all four DALC modes, matching amounts zero/half/one, Linear Lighting on/off,
interior/exterior inputs, world/reflection/preview gates, and ambient values
zero/half/one/two/five. It also exercises the production dynamic-cubemap
reflection helper with IBL on/off and partial IBL fog blending. Fixtures
must produce nonzero exterior lighting, and shader reflection verifies the
CPU buffer layout. Direct-light and glowmap outputs and DALC matching inputs
must remain independent of the Adaptive Balance ambient value.

The atmosphere extension adds production-code regression cases for cloud
migration before settings merges, explicit-value precedence, detailed and
master gating, independent sky/cloud gamma, profile interpolation, location
replacement/layering, transparency composition, finite bounds, and DevBench
validation. Color and ambient shader reflection expectations now cover the
80-byte buffer and the retained Color offsets; Color reflection also checks
Linear Lighting's cloud gamma offset. These compiled tests and production
shader permutations are deferred until the end of the selective upstream
sync, by user instruction. No atmosphere runtime validation has run.

Weather-color cases extract the production hook and use the production
adjustment helper. They cover repeated updates, fresh weather colors,
external edits, zero-to-neutral recovery, master/detail/runtime gates,
missing weather, failed profile resolution, finite bounds and owner changes.
Composition cases cover day/night and replacement/layered locations;
DevBench validation includes the new field. These additions have not been
compiled or executed. Builds, shader compilation and SE/AE/VR runtime
checks remain deferred until the selective sync ends.

### Ambient effect lighting validation

DevBench `communityshaders.menu` action `set_adaptive_balance_visuals`
accepts `visuals: {"useAmbientEffectLighting": true}` (or false). It stages
the global setting without enabling Adaptive Balance or saving it.
`status.adaptiveBalanceVisuals.global.useAmbientEffectLighting` reports the
saved selection and `.effective.useAmbientEffectLighting` its runtime gate.
Unknown fields and non-boolean toggle values reject the complete update.

The controller fixture covers default-off, explicit enabling/disabling,
master/load/menu/player/performance gates, brightness transport and strict
DevBench boolean validation. The shader-layout fixture checks all three
reused fields and the unchanged 80-byte size. These compiled tests and
SE/AE/VR rendering checks are deferred to the final build, per the sync
workflow; they have not run for this port.
