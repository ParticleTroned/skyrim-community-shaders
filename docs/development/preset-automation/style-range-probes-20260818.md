# Experimental style range probes

Status: generated extreme visual hypotheses; intentionally unqualified for playability

## Purpose

The original style prototypes proved reproducible separation but remained close to ordinary calibration. These range probes answer a different question: how far can the current CSX styling surface move one fixed scene while preserving the same Balanced performance and vendor-capability policy?

They are deliberately allowed to clip highlights, crush shadows, distort material identity, become uncomfortable in an HMD, and fail ordinary navigation. Those failures are observations, not preset defects. The probes must not be promoted directly into a release tier.

## Generated artifacts

| Probe | `SettingsUser.json` SHA-256 | Declared overrides |
| --- | --- | ---: |
| Natural Control | `A767C6CF6018F3FEBCE5C3FEB1DAF123E26142F862481D7DB87AD48657902D26` | 0 |
| Radiant Mythic Extreme | `56A1DA1E0443ED470FA4BBD75C858EF474BB08496FACF432DE6E2754CFB82ECC` | 83 |
| Abyssal Bleak Extreme | `24BCC78EA85A01A9A771D786F31E8DF7232155CE5E5FE3B88B7890FCE9D4EFE6` | 92 |

Natural Control is byte-identical to the unified Balanced candidate. The two extremes change settings values, not the shader-enable, sampling, quality-tier, logging, capture, render-scale, or GPU-provider decisions.

[`style-range-probe-policy.json`](./style-range-probe-policy.json) is the machine-readable source. Run [`generate-style-range-probes.ps1`](../../../tools/generate-style-range-probes.ps1) to generate the MO2 folders, or add `-Check` for non-writing verification. The shared generator pins the exact base hash, requires every override to state its expected base value, and enforces common runtime guards.

## Radiant Mythic Extreme

This end of the range intentionally combines normally separable excesses:

- maximum IBL scale and saturation;
- maximum volumetric shaft intensity, opacity, and weather-colour saturation, with a strong gold custom colour;
- maximum bloom intensity and saturation, a radius of `14`, full spread, and a raised compression ceiling;
- context brightness from `1.25` in dungeons to `1.65` at night, with lifted ambient, emissive, glowmap, sky, fog, and volumetric response;
- maximum basic-grass brightness and grass translucency;
- strongly brightened and saturated humanoid SSS; and
- an `0.8`-strength cyan water tint.

Expected failure signatures include broad highlight occupancy, gold fog contamination, lost sky detail, emissive crowding, luminous skin, cyan water dominance, and physical-HMD glare.

## Abyssal Bleak Extreme

The opposite end intentionally combines severe darkness with isolated cold landmarks:

- environment IBL scale `0.08`, sky IBL scale `0.15`, and near-zero IBL saturation;
- context brightness from `0.58` in daylight down to the legal `0.25` dungeon minimum;
- ambient multipliers down to `0.02`, while dungeon emissive and glowmap multipliers rise to `5.0`;
- near-maximum positive gamma offsets to crush sky, fog, water, and volumetric midtones;
- fully custom cold-blue volumetric colour at maximum opacity;
- a tight, hard-limited blue halo rather than diffuse bloom;
- nearly black non-translucent grass and severely desaturated, dark humanoid skin; and
- a full-strength near-black marsh water tint.

Expected failure signatures include black crush, floating emissive islands, missing material identity, corpse-like skin, opaque water, abrupt context transitions, and discomfort caused by extreme local contrast.

## Comparison use

Use Natural → extreme → Natural at a fixed pose. Exterior day alone is insufficient: include exterior night, an ordinary interior, a dungeon, a dwelling, strong volumetric structure, vegetation, visible water, and an NPC close-up. Record which controls create useful stylistic direction even when the combined probe is excessive.

The useful product is not either extreme preset. It is a map of perceptual leverage and interaction failures from which less correlated art directions can later be composed.
