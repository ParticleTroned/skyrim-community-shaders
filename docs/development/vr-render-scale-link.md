# VR Render Scale preference and upscaling link

The VR **Upscaling > Render Pipeline** controls include **Link Render Scale to
DLSS/FSR Upscaling**, enabled by default. Its persisted JSON key is
`renderScaleLinkedToUpscaling`.

Render Scale has two distinct states: the remembered user preference and the
physical reduced-resolution mode. DLAA (or FSR Native AA) temporarily disables
the physical mode without discarding the preference. Returning to a scaled
DLSS/FSR preset restores the remembered choice, including when the link is off.

| Action                                        | Link on                                                | Link off                                   |
| --------------------------------------------- | ------------------------------------------------------ | ------------------------------------------ |
| Select a scaled DLSS/FSR method or preset     | Request Render Scale on                                | Use the remembered Render Scale preference |
| Select DLAA / Native AA                       | Physical Render Scale off; preserve the preference     | Same                                       |
| Turn the link on while using DLAA / Native AA | Remember Render Scale on; remain at native resolution  | —                                          |
| Turn the link off                             | Keep the current Render Scale preference               | —                                          |
| Manually disable Render Scale                 | Disable the link too, after the transition is accepted | Remember Render Scale off                  |

Enabling the link does not change the selected quality preset. It therefore
does not silently turn DLAA into DLSS Quality. Method and quality changes use
the existing transition controller and its normal safety/admission checks.
Rejected transitions do not disable the link or claim that a requested physical
change succeeded.

If a method change is already queued or deferred during recovery, enabling the
link retains that request's method, quality, DLSS profile, and FSR runtime
selection together. Internal recovery targets do not replace retained user
intent. A pending None/TAA selection remains selected. Public VR FPS Stabilizer API admission
compares physical Render Scale mode, so a native-AA profile with Render Scale
off remains valid when the saved preference is on. Local load synchronization
reconciles the saved preference separately.

Saving settings while an accepted menu change is pending stores that selected
Render Scale preference, quality, and DLSS profile. It does not save the old
physical configuration or force the pending render-target rebuild to run early.

This is a selection convenience, not a global constraint. Loading an existing
settings file does not rewrite its stored `renderScaleMode` merely because the
new link defaults to on. Full-profile API requests, including DevBench's
explicit `enabled` value, remain authoritative. VR FPS Stabilizer applies the
link only for method/quality changes when a profile omits its Render Scale field;
an explicit on/off value still wins. The option is VR-only and is reset/omitted
for non-VR settings.

## DevBench control

The `communityshaders.renderscale` action `set_render_scale_link` accepts a
Boolean `enabled`. It requires Skyrim VR, developer mode, and an active stress
capture, and calls the same setter as the checkbox. Its `accepted` field means
request admission, not physical completion. Save through the normal Community
Shaders settings operation to persist the change.

The status field `renderScaleSelectionPolicy` separates `linkedToUpscaling`,
`rememberedPreference`, `requestedActive`, and `physicallyActive`. Explicit
`apply` profiles do not inherit the link.

## Validation

The policy tests cover remembered intent, native-AA round trips, linked and
unlinked selections, and explicit-profile precedence. The source contract test
checks persistence, both menu variants, accepted-transition ordering, and the
separation between selection defaults and explicit profiles.

The isolated `main-VR` port passed the universal `ALL` Release plugin build
with `DEVBENCH_BRIDGE=ON`, the `controller_tests` build, and all 55 registered
CTest tests. No plugin was deployed or game launched for this PR preparation.

These checks do not establish in-game stability. Live validation should exercise
DLSS Render Scale on → DLAA → scaled DLSS with the link both on and off, manual
Render Scale off followed by preset changes, FSR Native-AA round trips, settings
reload, and an explicit Render Scale-off DevBench/VR FPS Stabilizer profile.

This is a focused forward-port of `72b04290b` onto `main-VR`. It includes the
durable-preference prerequisite but none of the Face of Gogh neural-rendering
changes. It retains the structured transition result, direct-menu admission,
FSR runtime routing, and startup-native-fallback contracts of `main-VR`.

The candidate has not undergone the generated `csx-render-scale-pr-v1` live
qualification. No new hardware timing, visual verdict, or matched baseline is
claimed; historical comparison-ledger measurements do not validate this port.
Keep the PR in draft until the required candidate/baseline evidence is attached.
