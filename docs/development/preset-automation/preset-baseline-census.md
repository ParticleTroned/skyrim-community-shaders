# Existing preset census

Status: source census; inherited choices are hypotheses, not accepted preset conclusions

## Inputs and method

The six inherited MGO `SettingsUser.json` files under `MGO-Presets` were flattened into leaf paths and compared as exact values:

- AMD Performance, Balanced, and Quality; and
- NVIDIA Performance, Balanced, and Quality.

Each AMD file contains 604 flattened leaves; each NVIDIA file contains 597. UI layout, diagnostics, and editor state remain serialized alongside rendering controls, so leaf count is not a count of independent preset factors.

## Vendor comparison

Each same-tier AMD/NVIDIA pair has 13 syntactic differences. Seven are absent NVIDIA `Disable at Boot` entries whose AMD values are explicitly `false`. The six substantive differences are all under `Upscaling`:

| Control | AMD | NVIDIA |
| --- | ---: | ---: |
| `upscaleMethod` | 2 | 3 |
| `fsr4RuntimeEnable` | true | false |
| `dlssPreset` | 0 | 1 |
| `dlssSharpener` | 2 | 1 |
| `sharpnessDLSS` | 0.9 | 0.7 |
| `sharpnessFSR` | 0.7 | 0.9 |

No inherited same-tier shader-feature setting outside `Upscaling` differs by vendor. This supports one vendor-neutral shader tier policy with provider/capability selection at the upscaler boundary. It does not prove equal cost or correctness across vendors; that still requires hardware validation.

## Tier comparison

Performance → Balanced changes 18 leaves, Balanced → Quality changes 16, and Performance → Quality changes 20. All rendering differences are concentrated in seven feature families:

| Feature/control | Performance | Balanced | Quality |
| --- | ---: | ---: | ---: |
| Grass Collision `EnableGrassCollision` | false | true | true |
| SSGI `AOInteriorsOnly` | false | true | true |
| SSGI `ResolutionMode` | 1 | 1 | 0 |
| Screen Space Shadows `VRBaseSamplesAtReference` | 16 | 30 | 44 |
| Screen Space Shadows `VRCullDistance` | 20480 | 20480 | 0 |
| Skylighting `MinSpecularVisibility` | 0.05 | 0.10 | 0.10 |
| Skylighting `OcclusionUpdateInterval` | 8 | 6 | 5 |
| Skylighting `ProbeFieldSize` | 10240 | 12970.667 | 15701.333 |
| Skylighting `ProbeGridQuality` | 0 | 1 | 2 |
| Skylighting `ProbeUpdateInterval` | 16 | 13 | 9 |
| Skylighting `StableSliceCount` | 8 | 11 | 13 |
| Terrain Blending `TerrainCullDistance` | 725 | 1024 | 1024 |
| Upscaling `qualityMode` | 4 | 3 | 2 |
| Wetterness `RaindropChance` | 0.60 | 0.70 | 0.80 |
| Wetterness `RaindropFxRangeWorldUnits` | 700 | 1000 | 1400 |
| Wetterness `RaindropGridSize` | 3.60 | 3.25 | 3.00 |
| Wetterness `RaindropInterval` | 0.65 | 0.58 | 0.50 |
| Wetterness `RippleLifetime` | 0.22 | 0.26 | 0.30 |
| Wetterness `SplashesLifetime` | 4.5 | 5.2 | 6.0 |
| Wetterness `WetternessFadeRange` | 5000 | 7500 | 10000 |

Everything else is inherited identically across tiers. That may reflect an intentional invariant, an unmeasured choice, or a missing tier opportunity; the census cannot distinguish those cases.

### Requested versus effective Wetterness range

The serialized Performance value `WetternessFadeRange = 5000` is below Wetterness's current runtime/UI minimum of 100 metres. With `GAME_UNIT_TO_M = 0.01428`, sanitization raises the effective value to approximately `7002.8013` game units. The typed profile API reports both the requested profile definition and this effective normalization. This is a real policy/implementation mismatch: the 5,000-unit inherited value is not authoritative in the renderer. The present screen preserves the existing safety clamp; changing the minimum or the preset definition requires a visual cutoff and performance response test.

## Screening implications

The first response-curve campaign should prioritize the seven tiered families because they already encode the intended tier gradient. On/off ablations should then cover the other loaded Performance Tuning features to identify expensive or perceptually weak invariants. Architectural and hard-correctness review remains necessary for features that cannot be reduced to a safe inner toggle.

The initial factor model is therefore:

1. **Provider boundary:** one upscaling intent with hardware/runtime capability resolution, not separate AMD/NVIDIA shader trees.
2. **Tiered response curves:** Grass Collision, SSGI, Screen Space Shadows, Skylighting, Terrain Blending, upscaling quality, and Wetterness.
3. **Feature ablations:** the other reversible Performance Tuning features, using complete in-memory state snapshots and A/B/A restoration.
4. **Restart blocks:** boot-disabled packages, shader-permutation/resource-profile changes, and any control still lacking a proven live transition.

This collapses hundreds of serialized leaves into a tractable set of semantic factors while retaining the exact inherited values as hypotheses to test.
