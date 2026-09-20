# Upscaling menu navigation

The Upscaling header groups DLSS profile guidance and the current VR FOV
status above the controls. The information uses the configured Subtext
font, wraps to the available width, and stacks labels on narrow panels.

Click **VR > FOV** to open the VR feature directly on its FOV tab. The
link clears the feature search, expands the destination category, reveals
VR's Advanced settings when using Essentials, and scrolls the destination
to the top. The global UI mode and graphics settings stay unchanged.
Navigation is unavailable when the VR feature is unloaded or disabled.
FOV is selected on the first destination render, including the first visit
and when another VR tab was previously selected.

Render Scale appears below DLSS Profile, or below Upscale Preset for FSR.
The linking option and status remain alongside it. Sharpening controls
follow with three times the normal item spacing.

The Reflex **Use Markers To Optimize** slider and its availability message
appear only under **Backend Diagnostics > Reflex Debug**, with the CS log
level set to Debug or Trace. The full-frame marker coverage requirement
and Frame Generation exclusion still apply on SE, AE, and VR.

## DevBench

Use `communityshaders.menu` with:

```json
{ "action": "open_vr_fov" }
```

This uses the same navigation method as the UI link and opens the menu.
A successful response reports `navigationQueued: true`, `feature: "VR"`
and `tab: "FOV"`; selection completes when the menu next renders. It does
not claim that a queued page has already been displayed. Non-VR runtimes
and unavailable VR settings return `vr_fov_unavailable`. The existing
`expectedBuildId` check can pin the request to the intended DLL.

## Screen-space FOV defaults

Turning **Foveated Upscaling (FOV)** on selects **SSGI FOV** and
**Screen Space Shadows FOV** in both the full VR FOV panel and the
Essentials/performance controls. This changes only their FOV selections;
it does not enable either parent effect. You can untick either child
control afterward. Turning the master off retains those choices, and
turning it on again selects both defaults. Loading saved settings does
not overwrite individual choices.

The child controls are greyed out when their feature is unloaded,
switched off, or the shared FOV mask is inactive. Disable-at-boot changes
apply after restarting; availability reflects the currently loaded effects.
SSGI's existing experimental OCU mode continues to take priority over
SSGI FOV and remains configurable without shared-mask upscaling when
SSGI is active. SE/AE cannot enable the VR FOV switch.

DevBench can exercise the same switch through `communityshaders.menu`:

```json
{ "action": "set_fov_enabled", "enabled": true }
```

This stages the settings without saving them. Repeating `enabled: true`
while FOV is already on preserves individual child choices. The setter's
`fov` result and `status.fov` report the master selection, shared-mask
activity, both child selections and each child control's availability.
Enabling requires loaded Upscaling in VR with DLSS or FSR;
unsupported requests return `vr_fov_unavailable` without changing settings.

The `FovSettings` controller test exercises the production setters and
ImGui controls with simulated feature state. It covers master transitions,
individual opt-outs, inactive effects and masks, pending boot changes,
independent OCU controls, and agreement with DevBench status. This is a
menu integration test; it does not validate rendered headset output.
