# GPU-unified presets

The four `CSX Unified` MGO presets use one settings policy on AMD and NVIDIA:

| Tier | Upscaling quality | SSGI | Skylighting | Wetterness | Grass collision |
| --- | --- | --- | --- | --- | --- |
| Performance | Balanced | Off | Off | Off | Off |
| Balanced | Quality | Off | Low | On | On |
| Quality | Ultra Quality | AO-only, provisional | Medium | On | On |
| Ultra Quality | Hoshipa | AO-only, provisional | High | On | On |

The capability boundary remains vendor-neutral:

- `upscaleMethod=3` requests DLSS when Streamline reports DLSS available;
- `upscaleMethodNoDLSS=2` selects FSR when DLSS is unavailable.

Provider-specific tuning remains in the same generated JSON. DLSS reads its
preset and sharpener values; FSR reads its own sharpness and runtime-provider
settings. The graphics-quality policy is otherwise shared.

## Authoritative inputs

Preset generation has three layers:

1. [`Base.SettingsUser.json`](./unified-preset-templates/Base.SettingsUser.json)
   is one pinned, current-schema, vendor-neutral base.
2. [`unified-preset-policy.json`](./unified-preset-policy.json) defines common
   CSX/MGO policy, the complete allowlist of tier-owned paths, all four tier
   values, guards, and qualification state.
3. [`generate-unified-presets.ps1`](../../tools/generate-unified-presets.ps1)
   composes complete MGO `SettingsUser.json` files and a deterministic evidence
   report.

Every tier must set every `tierOwnedPaths` entry exactly once. Tier overrides
cannot touch operational sections such as Screenshot, Menu, diagnostics,
bindings, or compiler controls. Those values are inherited identically from
the base/common layer. This prevents unnoticed divergence between tiers even
though each MGO package must ultimately contain a complete settings file.

The generator rejects:

- a base whose SHA-256 does not match the policy;
- missing, duplicate, or extra tier-owned paths;
- tier writes into forbidden common/operational sections;
- obsolete Adaptive Balance, screenshot, water, or runtime-derived fields;
- missing current-schema markers and incorrect profile-array lengths;
- vendor names in unified output directories;
- output settings, metadata, or the generated report that are stale.

## Generate and verify

Generate all four packages and the evidence report:

```powershell
pwsh -NoProfile -File tools/generate-unified-presets.ps1
```

Perform the non-writing deterministic check:

```powershell
pwsh -NoProfile -File tools/generate-unified-presets.ps1 -Check
```

The generated candidates remain provisional. Qualification state is recorded
in the policy, emitted into each `meta.ini`, and summarized in
[`generated-unified-preset-report.json`](./generated-unified-preset-report.json).
Outstanding evidence includes native NVIDIA selection, a matched SteamVR/OCU
comparison, recalibration after the exact tiled HMD-mask work, AO-only SSGI
ambient/stereo qualification, and a fresh volumetric-lighting tier comparison.

Shader-cache packing, selective invalidation, and compiler thread/priority
policy are deliberately not graphics-tier settings. Presets keep disk caching
and `Skip Unchanged Shaders` enabled and never request blanket cache clearing.
