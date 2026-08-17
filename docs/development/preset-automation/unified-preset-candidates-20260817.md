# Unified provisional preset candidates

Status: generated development candidates, not release-qualified

## Result

The first synthesis produces three shader-policy presets rather than six vendor branches:

- `CSX Unified- Performance - Press END on PC to Customize`;
- `CSX Unified- Balanced - Press END on PC to Customize`; and
- `CSX Unified- Quality - Press END on PC to Customize`.

| Tier | Generated `SettingsUser.json` SHA-256 |
| --- | --- |
| Performance | `19E4776D3A76DAC82EE4D77A1BB658A5C81A6342F96BCEABF7C0A3FBEAA2C8BB` |
| Balanced | `D6AB5714F069E58E7F47D5422EC08F7C8EB57FC2D1624FE3DA2AEAB52758CC6A` |
| Quality | `C2F42D54D91F6868843209D2A88C3ADCB77238498AC36E663D8DFF64E19A7B53` |

[`unified-preset-policy.json`](./unified-preset-policy.json) is the machine-readable source. [`generate-unified-presets.ps1`](../../../tools/generate-unified-presets.ps1) pins each inherited AMD tier template by SHA-256, applies the reviewed overrides, enforces hard guards, and supports a non-writing `-Check` mode. The legacy AMD and NVIDIA folders remain unchanged as source evidence and recovery points.

## Vendor-united upscaling boundary

The existing upscaler already resolves two serialized controls:

1. `upscaleMethod=3` requests DLSS when Streamline reports DLSS available;
2. `upscaleMethodNoDLSS=2` supplies FSR when DLSS is unavailable.

One JSON file can therefore serve AMD and NVIDIA. Provider-specific fields remain separate inside that file: FSR keeps `sharpnessFSR=0.7` and FSR4 enabled, while DLSS keeps preset K (`dlssPreset=1`), sharpener 1, and `sharpnessDLSS=0.7`. This merges the active settings from the old AMD and NVIDIA files; it does not ask either vendor to consume the other's implementation.

Provider selection is a narrow capability boundary, not a second shader policy. NVIDIA execution still requires native validation, and the current Open Composite block remains authoritative where the runtime path cannot safely host the upscaler.

## Evidence-backed changes

The inherited tier values remain the base for Grass Collision, Screen Space Shadows, Skylighting, Terrain Blending, Wetterness, and upscaling quality intent. Synthesis adds only these reviewed changes:

| Family | Performance | Balanced | Quality | Status |
| --- | --- | --- | --- | --- |
| IBL | Enabled | Enabled | Enabled | Common measured ambient-replacement baseline |
| Volumetric Shadows | Enabled | Enabled | Enabled | Common low-cost shared provider |
| Volumetric Lighting | Low | Medium | High | Provisional measured cost gradient |
| SSGI runtime | Disabled | Disabled | Disabled | Release-safe fallback |
| Dormant SSGI policy | AO-only, interior, Quarter | AO-only, interior, Half | AO-only, interior, Full | Retained for explicit experimental activation only |
| Upscaler | DLSS when available, otherwise FSR | Same provider rule | Same provider rule | Unified capability selection; tier quality remains 4/3/2 |

The SSGI resolution values are deliberately serialized while `Enabled=false`. The accepted AMD fixed-pose and translation evidence is sufficient to preserve the experimental Quarter/Half/Full hypothesis, but not to turn it on in a normal generated preset.

## Known inherited limitation

Performance still serializes `WetternessFadeRange=5000`, which the runtime normalizes to approximately `7002.801` game units. The generator preserves this inherited request because the distance-boundary screen remains open. The manifest and policy treat the effective value—not 5,000—as the current rendered behavior.

## Release gates

These candidates are suitable for development and controlled comparison. They are not release candidates until the remaining gates in [`preset-policies.md`](./preset-policies.md) are addressed, especially ambient-cluster composition before SSGI activation, rotating/physical-HMD stereo, alternate runtime lanes, and native NVIDIA validation.
