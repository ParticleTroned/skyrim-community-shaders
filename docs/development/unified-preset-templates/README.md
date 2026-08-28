# Unified preset base

`Base.SettingsUser.json` is the pinned, vendor-neutral settings source used by
the unified preset generator. It is a complete current-schema snapshot, but it
does not own the graphics-quality choices listed in `tierOwnedPaths`; every
tier must set every one of those paths explicitly.

The base owns settings that must remain identical across tiers. This includes
the current Adaptive Balance profile representation, profile-native water and
Bloom values, the screenshot sequence schema, common VR-menu behavior, and
feature-neutral defaults. Runtime-derived compiler thread counts and obsolete
settings blocks are deliberately absent.

Refresh the base only from a settings file written by the intended current CSX
runtime, then review and pin the reported hash in
`unified-preset-policy.json`:

```powershell
pwsh -NoProfile -File tools/generate-unified-presets.ps1 `
  -RefreshBaseFromPath X:\path\to\current\SettingsUser.json
```

Refreshing the base does not generate preset packages. It normalizes the
configured removable paths, applies the neutral tier, validates the current
schema markers, writes the base, and reports its SHA-256. Generation remains
blocked until the policy pins that exact hash.
