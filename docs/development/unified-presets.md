# GPU-unified presets

The three `CSX Unified` MGO presets use one settings policy on AMD and NVIDIA:

| Tier        | Upscaling quality | SSGI                 | Skylighting | Wetterness | Grass collision |
| ----------- | ----------------- | -------------------- | ----------- | ---------- | --------------- |
| Performance | Balanced          | Off                  | Off         | Off        | Off             |
| Balanced    | Quality           | Off                  | Low         | On         | On              |
| Quality     | Ultra Quality     | AO-only, provisional | Medium      | On         | On              |

The capability boundary remains vendor-neutral:

-   `upscaleMethod=3` requests DLSS when Streamline reports DLSS available;
-   `upscaleMethodNoDLSS=2` selects FSR when DLSS is unavailable.

Provider-specific tuning remains in the same generated JSON. DLSS reads its
preset and sharpener values; FSR reads its own sharpness and runtime-provider
settings. The graphics-quality policy is otherwise shared.

## Authoritative inputs

Preset generation has three layers:

1. [`Base.SettingsUser.json`](./unified-preset-templates/Base.SettingsUser.json)
   is one pinned, current-schema, vendor-neutral base.
2. [`unified-preset-policy.json`](./unified-preset-policy.json) defines common
   CSX/MGO policy, the complete allowlist of tier-owned paths, all three tier
   values, guards, qualification state, and a fingerprint of the runtime
   settings implementation.
3. [`generate-unified-presets.ps1`](../../tools/generate-unified-presets.ps1)
   composes complete MGO `SettingsUser.json` files and a deterministic evidence
   report.

Every tier must set every `tierOwnedPaths` entry exactly once. Tier overrides
cannot touch operational sections such as Screenshot, Menu, diagnostics,
bindings, or compiler controls. Those values are inherited identically from
the base/common layer. This prevents unnoticed divergence between tiers even
though each MGO package must ultimately contain a complete settings file.

The generator rejects:

-   a base whose SHA-256 does not match the policy;
-   a change to any pinned runtime settings source;
-   a policy path that is absent, differs only by case, or has the wrong JSON
    value kind in the base;
-   any tier count, name, order, or output directory outside the fixed
    Performance/Balanced/Quality contract;
-   missing, duplicate, or extra tier-owned paths;
-   tier writes into forbidden common/operational sections;
-   obsolete Adaptive Balance, screenshot, water, or runtime-derived fields;
-   missing current-schema markers and incorrect profile-array lengths;
-   vendor names in unified output directories;
-   unmanaged extra `CSX Unified` package directories;
-   output settings, metadata, or the generated report that are stale.

The current base includes the main-VR settings migrations for Adaptive
Balance's unified global profile, separate exterior/interior godray profiles,
wet-grass darkening, locked VR menu placement, depth-culling policy modes, and
opt-in verbose PBR diagnostics. Their retired keys are explicitly rejected so
a package cannot silently fall back through legacy migration on first load.

Generation and `-Check` take the same exclusive publication lock. A normal
generation builds and validates all seven outputs in memory, stages each file
beside its destination, flushes and reads the staged content back, then replaces
the complete package set. A publication error restores the prior complete set.
`-Check` compares expected content without writing repository files. This makes
concurrent writers fail closed and prevents an interrupted run from leaving a
partly updated preset family.

## Generate and verify

Generate all three packages and the evidence report:

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
ambient/stereo qualification, and an interior volumetric-lighting comparison.
The locked-time OCU exterior screen found no reason to split the shared High
volumetric setting, while confirming Skylighting as the strongest measured
tier lever. Rain and character-focused anchors remain necessary for Wetterness,
Subsurface Scattering, and Hair Specular.

Shader-cache packing, selective invalidation, and compiler thread/priority
policy are deliberately not graphics-tier settings. Presets keep disk caching
and `Skip Unchanged Shaders` enabled and never request blanket cache clearing.

[`unified-preset-performance-methodology.md`](./unified-preset-performance-methodology.md)
records the controlled timing model, shared deferred-topology cost, and stop
criteria to use when the three tiers are requalified. The compact results are
also available in
[`unified-preset-measurements.json`](./unified-preset-measurements.json). This
evidence does not add a fourth tier or change the current provisional values.
