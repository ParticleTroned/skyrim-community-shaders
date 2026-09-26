# Adaptive Balance color, ambient, sky and water controls

Adaptive Balance exposes these controls in Global, time/interior profiles,
and location layers. Sky Saturation is beside Sky Brightness in Lighting's
detailed controls. Caustics and Parallax are in Water's detailed controls.
The Water detail checkbox controls visibility; Lighting's detailed-control
checkbox also enables its detailed adjustments, including Sky Saturation.

| Control                   | Neutral | Range         |
| ------------------------- | ------- | ------------- |
| Contrast                  | 1       | 0.5–2         |
| Saturation                | 1       | 0–2           |
| Sky Saturation            | 1       | 0–2           |
| Ambient                   | 1       | 0–5           |
| Caustics Strength         | 1       | 0–2           |
| Caustics Tiling           | 1       | 0.25–4        |
| Caustics Speed            | 1       | 0–3           |
| Caustics Color Dispersion | 1       | 0–2           |
| Parallax Strength         | 1       | 0–2           |
| Parallax Quality          | 16      | 4–64, integer |

Zero saturation produces a grayscale sky. Zero caustics strength disables
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
Missing saved fields take neutral defaults. Disabling Adaptive Balance
restores neutral outputs without discarding the saved adjustments.

## Color

The **Color** tab exposes Contrast and Saturation in Essential and Advanced
views for Global, time/interior profiles, and location layers. These
controls work independently of the detailed Lighting switch and Linear
Lighting. Existing settings and presets without these fields load neutral
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
shared color helper. HDR values are not clamped to one. Subsequent engine
fades and UI can still contribute their own colors.

Exactly neutral values bypass grading, including the gamma round trip.
The master/runtime gates emit neutral values. Config loading and every
composed layer bound the controls and replace non-finite inputs with one;
DevBench rejects invalid updates before mutation. The implementation fills
two existing padding slots at offsets 40 and 44 in the 48-byte Adaptive
Balance buffer. It adds no resources, sampling, or render passes and uses
the same per-pixel math for SE, AE, and both VR eyes. Runtime cost has not
been measured.

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
        "lightingAdvanced": true,
        "causticsStrength": 1.2,
        "parallaxQuality": 24
    }
}
```

`visuals` must be a nonempty object containing only `contrast`, `saturation`,
`ambient`, `skySaturation`,
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
and the master/runtime gate; they do not imply that Water Effects is loaded.

## Regression coverage

The color extension adds executed `AdaptiveBalanceToggle` cases for neutral
defaults, finite bounds, day/night interpolation, layered and replacement
locations, independent detailed-lighting gating, and off/on restoration.
`AdaptiveBalanceColorShader` executes 288 samples on D3D11 WARP across
SE/AE and VR, Linear Lighting on/off, and the minimum, neutral and maximum
settings. It checks exact neutral bypass, black/middle-gray preservation,
contrast direction, hue ratios, monochrome output, finite near-black and
HDR values, negative inputs, color-space agreement, and buffer offsets.
It also compiles eight production `ISHDR` blend permutations with Adaptive
Balance and fades independently on/off. All passed with FXC warnings as
errors.

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

Full DLL linking, deployment, in-game DevBench/UI validation and visual
assessment have not run.

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
