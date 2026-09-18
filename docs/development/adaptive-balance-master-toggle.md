# Adaptive Balance master toggle

See [sky and water controls](adaptive-balance-visual-controls.md) for
their profile composition, Waves and Wind grouping, and DevBench updates.

The `Enable` checkbox sits in the shared feature header, before the
settings body, in both Essentials and Advanced views. It uses the same
header path as Foliage Lighting. It controls every adjustment owned by
Adaptive Balance, across Global, time/interior profiles, and location layers. It
uses the existing `AdaptiveBrightness.enabled` saved setting. No new
setting or migration is required: an existing `false` now means fully off.
The Performance Tuning pane also draws this shared control, so its disabled
settings can be re-enabled without leaving the pane.

When disabled:

-   Lighting multipliers become neutral, and Adaptive Balance contributes
    no gamma/color changes to Linear Lighting's own settings.
-   Its Bloom enhancement becomes inactive; native Bloom remains available.
-   Its water appearance scales become neutral, and its wind-driven wave
    modulation stops. Unified Water geometry, flowmaps, displacement, and
    independently configured water behavior remain active.
-   Engine wind is neither disabled nor modified. Other wind consumers keep
    reading the engine state normally.
-   Shared point-light classification needed by Linear Lighting continues.

Saved Global, profile, and location values are preserved. Re-enabling
restores their composition; wind smoothing starts from the current engine
wind value. An individual layer's wind override can still supersede a
broader layer while Adaptive Balance is on, but cannot bypass the master
switch. The performance-cost control also respects the master switch.
Both toggle paths reset local wind smoothing on state changes, including
off/on transitions between water updates. Repeated requests for the same
state leave smoothing intact. Measurement readiness remains independent
of the enable gates so the disabled comparison phase can complete.

The regression originated in `f53e9d29d43f4d8335a95fa27e95422efc46ade8`
(`feat(adaptive-balance): unify setting layers`): Global outputs used a
runtime gate without `settings.enabled`, while only profile composition
checked that setting. All outputs now use one shared enabled-state gate.

## DevBench

`communityshaders.menu` accepts:

```json
{
    "action": "set_adaptive_balance_enabled",
    "enabled": false,
    "expectedBuildId": "<loaded DLL Build ID>"
}
```

The operation runs through the existing main-thread dispatcher and stages
the setting without saving it. Menu `status` reports
`adaptiveBalanceEnabled` (the requested setting) and
`adaptiveBalanceActive` (including gameplay availability and performance
measurement state).

## Validation

`AdaptiveBalanceToggle` compiles production setting types, runtime gates,
profile composition, effective output methods, water-wind smoothing, and
shared point-light requirements against simulated engine state. It checks
Global-only state, blended profiles, both additive and replacement
location layers, off/on restoration, neutral outputs, unchanged engine
wind, independent Linear Lighting, and menu/unloaded/measurement gates.
It also checks that disabled outputs skip profile resolution, measurement
restoration cannot override the saved master setting, and rapid toggle
transitions do not retain stale wind smoothing. That last regression
failed before adding the measurement setter reset and passed afterward.

The focused test and Menu DevBench schema/dispatch contract passed.
The preset source fingerprint was refreshed after reviewing the unchanged
serialization, defaults, loading, saving, and migration contracts. The
generator regression suite and deterministic check passed; all three
preset settings remain identical apart from compatibility metadata.
`AdaptiveBrightness.cpp` and `MenuDevBenchBridge.cpp` compiled successfully with
MSVC `/W4 /WX`, universal SE/AE/VR definitions, and the DevBench bridge
enabled. Compiler responses and results are preserved under the isolated
worktree's `build/adaptive-balance-validation` directory.

No shader source or engine-wind implementation changes are required.
Full DLL linking, deployment, and in-game validation have not run.
