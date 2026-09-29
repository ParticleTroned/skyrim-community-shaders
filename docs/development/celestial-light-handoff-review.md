# Celestial light handoff and sunlight dimming

Source review and implementation dated 2026-09-29. No build, automated
test, game launch or runtime visual validation was performed.

## Evidence and scope

Main-VR base: `dab1874a76fd39175dcefdc52110ba69d7284e12`.
SE comparison: `cs-1.7-PL-SE` at
`734435e158c91438812ff96314645f2c69a8b8db`, also checked against cached
`origin/cs-1.7-PL-SE` at `d438b51738da787f45c49013f5659a0a4326c963`.
Their Sky Sync implementations match.

The main-VR sky-update and caster-selection paths are shared by SE, AE
and VR. They previously admitted moons at the offset sunset end minus
one hour and retained them until the offset sunrise begin plus one hour.
`ShadowFader::SetLighting` changes the shared light's direction, while
its intensity result is consumed by volumetric lighting. It does not
replace the engine's direct-light diffuse colour. This exposes all three
runtimes to the reported direction/colour mismatch; only the flat visual
symptom was reported by the tester.

SE gates moon selection on the climate's unshifted night interval. Its
`OnSkyUpdateColors` sunlight-dimming method has no caller in either SE
reference: repository-wide searches find its declaration, definition and
comment, but no invocation or registered hook. The earlier claim that SE
actively dims direct sunlight was therefore unsupported. Its separate
volumetric dimming path is connected.

## Implementation

-   Caster eligibility uses climate sunset end and sunrise begin, without
    the visual sun-path offsets. Moon fading stays on the night side of
    those boundaries. At dawn the active caster is also changed to the sun,
    so an outgoing moon cannot retain the newly warm directional light.
-   Climate timings refresh on each valid sky update, covering climate
    changes and enabling Sky Sync after a climate was already loaded.
    The redundant climate-change hook is removed.
-   Direct-light dimming runs after the existing engine sky update. It
    scales the sun light's diffuse colour through `GetLightRuntimeData`,
    whose layout handles SE/AE/VR. Weather colour and relative RGB values
    remain authoritative; no fixed blue moonlight is introduced. The scale
    is applied before the existing directional-light update.
-   Dimming defaults to enabled. `HorizonFadeHours` defaults to 0.7 game
    hours and is bounded to 0..1.5, following the SE settings. With the
    default duration, dusk fades from the climate's sunset midpoint to zero
    at sunset end, then recovers at night. Dawn mirrors this around sunrise
    begin. Zero duration removes the night-side ramp and permits an abrupt
    brightness change at the boundary; disabling dimming leaves the handoff
    correction on.
-   `WeatherColorAdjustment` restores the previous owned result before the
    engine refreshes or reuses light data. It supports an exact zero factor,
    prevents cumulative darkening and preserves later external colour edits.
    A retained `NiPointer` keeps the exact adjusted light alive until it is
    restored, including sun/light replacement and loss of the sky pointer.
    Disabled/inapplicable Sky Sync restores its owned adjustment.
-   Main-VR's moon choice, phase handling, lens-flare handling, volumetric
    machinery and unconstrained terrain-shadow direction remain in place.
    No additional engine hook or runtime-specific implementation was added.

## DevBench

`communityshaders.menu` now supports `set_sky_sync_sunlight` for loaded
Sky Sync in SE/AE/VR. Required `enabled` controls sunlight dimming, not the
Sky Sync master switch. Optional `fadeHours` must be finite and within
0..1.5; omission preserves the current duration. Invalid requests leave
settings unchanged. Changes are staged on the main thread without saving
and take effect on the next sky update.

The existing `status` action includes `skySyncSunlight`: load state,
master enable state, dimming settings and the last applied factor. The
registered action list, description and input schema include the setter.

## Adversarial review and corrections

| Finding                                                                                                                                               | Correction                                                                                                                                                                                                                           |
| ----------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Restoration looked up the current light and could abandon an adjusted, still-live previous light after replacement.                                   | Retain the adjusted light with `NiPointer`, restore that object, then release it. The existing colour-ownership utility still preserves external edits.                                                                              |
| Direct colour was changed after the light's scene-graph update.                                                                                       | Apply dimming before the existing light update so it observes the final diffuse colour and direction together. Engine/render integration still needs runtime confirmation.                                                           |
| Moon fade windows used linear hours and failed across midnight; prioritizing the dusk ramp over dawn could introduce a discontinuity in short nights. | Share one nearest-night-boundary calculation between moon fading and direct dimming. It wraps midnight and combines overlapping ramps continuously.                                                                                  |
| Raw malformed climate intervals or invalid clock/settings values could reach the new timing path.                                                     | Reject unordered/out-of-day climate intervals and invalid game hours; treat 24:00 as 00:00. Use the existing finite-clamp utility for loaded lighting/path settings. Empty individual sunrise/sunset intervals never divide by zero. |
| An unloaded/conflicted feature could still enter the shared update if its enable setting was restored.                                                | Gate the update on both `loaded` and `Enabled`, restoring any owned dimming on exit.                                                                                                                                                 |
| Timing refresh was duplicated between the per-frame path and the climate-change hook.                                                                 | Keep per-update refresh as the single path and remove the redundant hook.                                                                                                                                                            |

Scope remains the shared celestial handoff and dimming policy, its settings,
DevBench access and this record. The SE branch provides the timing and fade
reference; its entire fader, moon APIs and transition machinery are not
ported. The original SE fade contribution is attributed to doodlum, verified
from commit `3aff2cdcae875fc77546476d470e4d79cc72ee2b`.

## Validation limits

Source review covered climate versus visual timing, the outgoing dawn
caster, dimming at zero, midnight and overlapping night ramps, update
ordering, light replacement, colour ownership, runtime data access, invalid
duration inputs and the DevBench dispatch/schema path.
The implementation uses the existing colour-adjustment utility and sky
hook rather than duplicating either mechanism.

`pwsh ./tools/git.ps1 diff --check` passed. Scoped pre-commit whitespace,
line-ending, clang-format and prettier hooks passed for the three changed
source files and this report. Hooks with no matching files were skipped.
Builds and tests remain unrun at the user's request. The implementation
still needs in-game confirmation through dusk and dawn in flat and VR,
including weather transitions, time jumps, nonzero sun-path offsets and
disable/re-enable. No runtime success or performance improvement is claimed.

This worktree changes main-VR's shared implementation. The separate
`cs-1.7-PL-SE` branch is not modified by this change.
