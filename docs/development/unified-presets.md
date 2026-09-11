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
-   a settings-owning feature source that is not present in the pinned runtime
    inventory;
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

## CSX compatibility contract

The generated packages target CSX 3.19-VR only. Each `SettingsUser.json`
contains a versioned `Preset Compatibility` object with a stable preset ID,
package version, VR runtime, inclusive minimum `3.19`, exclusive maximum
`3.20`, and the settings-contract fingerprint used to generate it.

CSX validates marked settings before canonicalization, migration, or merge.
Malformed metadata, an unsupported compatibility-contract version, the wrong
runtime, or a CSX version outside the declared range rejects the complete user
layer without rewriting it. Defaults remain active, saving is blocked to
protect the rejected file, and the decision is recorded in the log and exposed
through the Feature DevBench API's `preset_compatibility` action. Unmarked
legacy and user-authored settings remain accepted because strict metadata
cannot be added retroactively.

The three generated presets never hard-disable a feature: every `Disable at
Boot` value is false. Tier exclusions use feature-owned live/soft settings.
CS Editor is not present in the boot-disable map, and Weather Picker remains
enabled because both are operational tools rather than shader tiers.
Wand pointing is likewise a guarded common VR interaction default, so every
tier enables it independently of shader-quality choices.

Generation and `-Check` take one physical-repository publication lock,
independent of command-line paths. Before writing, the generator resolves path
aliases and proves that all outputs are distinct from the policy, base,
generator, focused test, workflow, documentation, refresh source, and complete
runtime-source inventory.

A normal generation records a durable transaction journal before staging,
backs up every existing target, publishes all seven outputs, verifies their
hashes, and only then records the `committed` boundary. The generated report is
the final output and contains the hashes by which a consumer accepts the
generation. A process stopped before that boundary is rolled back on the next
locked run. A process stopped after it is committed finishes cleanup without
rolling back valid outputs. Failed recovery preserves the journal and every
remaining recovery artifact for diagnosis. `-Check` performs recovery first,
then compares expected content without rewriting a valid generation.

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

The requested Hair Specular and water-appearance settings are shared appearance
baselines rather than tier levers. The water baseline includes the blue tint,
0.15 tint strength, 15-unit shore fade, 0.5 wave amplitude, 0.90 Fresnel maximum,
and 1.25 global reflection amount recorded in the policy.

Shader-cache packing, selective invalidation, and compiler thread/priority
policy are deliberately not graphics-tier settings. Presets keep disk caching
and `Skip Unchanged Shaders` enabled and never request blanket cache clearing.

[`unified-preset-performance-methodology.md`](./unified-preset-performance-methodology.md)
records the controlled timing model, shared deferred-topology cost, and stop
criteria to use when the three tiers are requalified. The compact results are
also available in
[`unified-preset-measurements.json`](./unified-preset-measurements.json). This
evidence does not add a fourth tier or change the current provisional values.
