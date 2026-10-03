# Expanded Performance Tuning controls

The SE/AE Performance Tuning page exposes focused controls and cost
comparisons for Linear Lighting, Cloud Shadows, True PBR, Extended
Materials, and Foliage Lighting. These controls use the existing flat-screen
menu and D3D11 timing path. They do not add a VR runtime, shader permutation,
or dependency.

## State and comparison contracts

-   Linear Lighting is measurable only where its interior/exterior policy lets
    it run.
-   Cloud Shadows requires an exterior full-sky scene with an active climate.
-   True PBR measures only when its master setting is enabled; visible PBR
    content determines the useful workload.
-   Extended Materials snapshots every related integer-backed setting before
    disabling all material paths. Restoration reapplies terrain initialization
    when legacy terrain parallax was enabled.
-   Foliage Lighting has a master switch that preserves the saved tree and
    grass tuning. A cost test is offered only while at least one contribution
    can run.

Every comparison must restore the exact pre-test state on success,
cancellation, timeout, or another measurement failure. Integer-backed
checkboxes are normalized to zero or one at their UI and settings boundaries.

## Runtime validation

Run these checks on both supported SE and AE runtimes after the final build:

1. Save a non-default combination for each feature, restart the game, and
   confirm the Performance Tuning page reproduces it without changing the
   feature's full settings page.
2. In an exterior full-sky scene, run each available cost comparison and
   confirm the current and Off legs complete, then verify every setting is
   restored exactly.
3. Repeat cancellation during the current leg, the comparison leg, and the
   restore wait. Confirm no feature remains disabled and Extended Materials
   restores all subordinate settings.
4. Enter an interior and a location excluded by Linear Lighting. Confirm
   Cloud Shadows and Linear Lighting explain why measurement is unavailable.
5. Disable True PBR while Foliage Lighting ambient boost is selected. Confirm
   the dependent control is unavailable while independent foliage and grass
   controls continue to work.
6. Toggle Foliage Lighting off and on. Confirm its detailed tuning survives
   and its common buffer becomes neutral only while the master switch is off.

The build, shader suite, and host tests verify compilation and shared policy
code, but they do not replace these in-game SE/AE state-restoration checks.
