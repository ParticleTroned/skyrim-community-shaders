# Upscaling menu navigation

The Upscaling header groups DLSS profile guidance and the current VR FOV
status above the controls. The information uses the configured Subtext
font, wraps to the available width, and stacks labels on narrow panels.

Click **VR > FOV** to open the VR feature directly on its FOV tab. The
link clears the feature search, expands the destination category, reveals
VR's Advanced settings when using Essentials, and scrolls the destination
to the top. The global UI mode and graphics settings stay unchanged.
Navigation is unavailable when the VR feature is unloaded or disabled.

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
