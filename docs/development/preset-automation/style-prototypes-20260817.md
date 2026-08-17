# Balanced style prototypes

Status: generated visual hypotheses; not visually or performance qualified

## Result

Three reproducible style prototypes now sit above the same vendor-unified Balanced performance policy:

| Style | Generated `SettingsUser.json` SHA-256 | Declared leaf overrides |
| --- | --- | ---: |
| Natural Reference | `A767C6CF6018F3FEBCE5C3FEB1DAF123E26142F862481D7DB87AD48657902D26` | 0 |
| Mythic High Fantasy | `73AC8F3C5691711027DB702AF3F91B1A560BDA10D0635B18D7E6C19F4B2DB4C8` | 7 |
| Bleak Dark Fantasy | `67D5F1E99ABD4911F3B76FC8CFA6E02F59CC870829430BDD825794ABDC622B21` | 27 |

The generated MO2 folders are:

- `CSX Style Prototype- Natural Reference - Balanced - Press END on PC to Customize`;
- `CSX Style Prototype- Mythic High Fantasy - Balanced - Press END on PC to Customize`; and
- `CSX Style Prototype- Bleak Dark Fantasy - Balanced - Press END on PC to Customize`.

[`style-prototype-policy.json`](./style-prototype-policy.json) is the machine-readable source. [`generate-style-prototypes.ps1`](../../../tools/generate-style-prototypes.ps1) pins the exact unified Balanced base by SHA-256, allows changes only below `Adaptive Balance`, requires every changed leaf to declare its expected base value, enforces the common rendering and GPU-provider guards, and supports a non-writing `-Check` mode.

Natural Reference is deliberately byte-identical to the unified Balanced candidate. It is the A/B control, not a fourth interpretation of “natural.”

## Orthogonal structure

The prototypes establish three separate decisions:

1. **style intent**: Natural Reference, Mythic High Fantasy, or Bleak Dark Fantasy;
2. **performance budget**: Balanced for this first screen, later Performance/Balanced/Quality; and
3. **GPU capability**: DLSS when available and FSR otherwise, using the existing unified provider boundary.

No style forks by AMD or NVIDIA. No quality-tier, shader-enable, sampling, resource, upscaling-quality, logging, or capture settings change between these three files. IBL remains enabled, Volumetric Shadows remain enabled, SSGI remains disabled, Volumetric Lighting remains Medium/Medium, and upscaling remains unified Balanced mode.

## Chosen styling surface

The first prototypes use only the current Adaptive Balance renderer controls:

- five context profiles in serialized order: exterior day, exterior night, interior, dungeon, and dwelling;
- master brightness, which deliberately weights ambient, direct, point, emissive, effect, and gamma response rather than applying one flat multiplier; and
- the existing Bloom enhancement, including the current daylight visibility fix.

This narrow surface makes the first visual comparison attributable. It does not claim that IBL saturation, volumetric colour, cloud shadows, wet-material response, water tint, or grass response are irrelevant to a mature style. Those are withheld until the basic tone and halation hypotheses have controlled captures.

## Prototype intent

### Natural Reference

- Exact unified Balanced settings.
- Bloom enhancement remains off.
- All context brightness profiles remain neutral at `1.0`.
- Purpose: reveal what each style actually changes and expose scene or weather bias in the baseline.

### Mythic High Fantasy

- Enables the built-in warm `Fantasy` Bloom profile: intensity `4.0`, radius `5.0`, full spread, saturation `1.3`, warm tint, and compressed ceiling `0.67`.
- Lifts context brightness moderately: day `1.05`, night `1.08`, interior `1.04`, dungeon `1.02`, and dwelling `1.06`.
- Intended reading: luminous air and materials, heroic highlights, brighter moonlit navigation, and inviting dwellings without changing the Balanced shader budget.
- Primary risks: highlight crowding, excessive daytime halation, pale-sky compression, and bloom discomfort in a physical HMD.

### Bleak Dark Fantasy

- Enables a custom tight, cool, desaturated Default Bloom profile: intensity `1.35`, radius `2.75`, spread `0.55`, saturation `0.45`, tint `[0.68, 0.78, 0.92]`, threshold `0.10`, and ceiling `0.62`.
- Lowers context brightness: day `0.92`, night `0.78`, interior `0.88`, dungeon `0.72`, and dwelling `0.90`.
- Uses advanced night and dungeon controls to reduce ambient visibility while preserving practical lights and gameplay effects.
- At exterior night the composed multipliers are approximately `0.712×` ambient, `0.935×` point light, `1.061×` emissive/glowmap, and `0.949×` effect lighting relative to the neutral base.
- In dungeons they are approximately `0.602×` ambient, `0.932×` point light, `1.128×` emissive/glowmap, and `0.948×` effect lighting.
- Intended reading: materially darker space with local light islands and cold constrained highlights, rather than indiscriminate black crush.
- Primary risks: crushed shadow navigation, adaptation discomfort on context transitions, lost material separation, and over-prominent emissive textures.

The composed multiplier figures describe the renderer-control formula, not measured displayed luminance. Weather, cell lighting, material response, runtime reprojection, headset optics, and the rest of the shader stack still determine the observed image.

## Required first comparison

Run paired captures in this order to minimize uncontrolled factors:

1. Natural → Mythic → Natural at exterior day and exterior night;
2. Natural → Bleak → Natural at dungeon and dwelling anchors;
3. all three at one ordinary interior and one adverse-weather exterior;
4. separate-eye stills first, then a short rotating or continuous-motion physical-HMD sequence; and
5. timing-only passes separately from screenshot/video capture.

Evaluate more than magnitude. Record highlight occupancy and clipping, dark-region visibility, material identity, fog/sky separation, skin and grass response, water readability, eye agreement, transition behavior, temporal stability, and physical-HMD comfort.

Do not tune from one picturesque scene. The first decision is whether the style reads coherently across the five context classes, not whether one screenshot looks dramatic.

## Qualification boundary

These are intentionally conspicuous prototypes, not preset recommendations. The settings are schema-valid and reproducibly generated, but they have not been loaded into the game in this revision. They require controlled visual, stereo, comfort, and timing evidence before promotion. The Bloom daylight fix is present in the current source and unit-tested elsewhere; that does not qualify these particular parameter choices.
