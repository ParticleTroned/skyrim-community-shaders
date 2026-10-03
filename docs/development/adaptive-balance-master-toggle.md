# Adaptive Balance master toggle

See [sky and water controls](adaptive-balance-visual-controls.md) for
their profile composition and rendering behavior.

The `Enable` checkbox sits in the shared feature header, before the
settings body, in both Essentials and Advanced views. It uses the same
header path as Foliage Lighting. It controls every adjustment owned by
Adaptive Balance, across Global, time/interior profiles, and location layers. It
uses the existing `AdaptiveBrightness.enabled` saved setting. No new
setting or migration is required: an existing `false` now means fully off.

When disabled:

-   Lighting multipliers become neutral, and Adaptive Balance contributes
    no gamma/color changes to Linear Lighting's own settings.
-   Its Bloom enhancement becomes inactive; native Bloom remains available.
-   Its water appearance scales become neutral. Unified Water geometry,
    flowmaps, displacement, and
    independently configured water behavior remain active.
-   Shared point-light classification needed by Linear Lighting continues.

Saved Global, profile, and location values are preserved. Re-enabling
restores their composition. The centralized setter keeps future runtime
state owned by Adaptive Balance behind the same transition boundary.

## Validation

The existing effective-output methods all use `IsRuntimeEnabled()`, which
includes the saved master setting before resolving profiles. This keeps
disabled lighting, Bloom, and water output neutral and skips profile lookup.
Live SE and AE validation should cover disabling and re-enabling the feature
in exterior and interior cells while retaining edited profile values.
