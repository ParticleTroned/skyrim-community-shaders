# Main SE settings UI

The settings navigation follows `origin/main-VR` at
`da73f53d8f6429d2d968cabde3c3eec9f3a04ab8`. Home opens on Welcome with
the existing sloth artwork, credits and community links. Its GitHub button
opens [main-SE](https://github.com/ParticleTroned/skyrim-community-shaders/tree/main-SE).
General, Advanced, Profiling and the complete feature list are available
without an interface-mode selector. Old `PerformanceUiMode` configuration
entries are ignored; rendering settings are preserved.

Feature master switches use the shared top-right `Enabled` control. The
Performance section uses the same header control. Component options such
as reflections, weather overrides and screenshot modes remain in their
own sections.

HDR Display uses Overview, Output, Brightness and Interface sections.
The header switch retains display detection, forced-output confirmation
and immediate colour-space updates. Disabling output retains calibration.
Output reports the applied HDR state separately from the requested setting
and Windows display detection. It distinguishes active HDR, disabled HDR,
an enable request without active output, and output that remains HDR after
disabling. The footer names the D3D11 or D3D12 presentation path; HDR is
supported on both paths.
The shared confirmation dialog saves the warning preference only on
confirmation. Cancel and the close button leave output and that preference
unchanged. Defaults apply the detected output mode immediately.
Brightness controls retain their ranges and enforce paper white below peak
brightness. Configuration loads validate calibration, including non-finite
UI brightness, before updating the output. Output documents Windows HDR and borderless-window requirements;
Brightness explains the limits of reported display metadata; Interface
explains the separate main-menu and loading-screen brightness behavior.

Upscaling uses dropdowns for Method, Upscale Preset and DLSS Profile,
matching the VR interface while retaining the SE resolution behavior.
Frame generation and Reflex are in the Frame Generation tab. On NVIDIA,
the AMD FSR preference is available before enabling frame generation for
the first time; a missing or blocked FSR runtime disables that preference
with an explanation. Save settings and restart after enabling frame
generation or changing its preferred provider.

## Validation

`python tests/se_hdr_ui_test.py` compiles the production HDR UI methods with
observable widgets. It checks detection, forced-output confirmation and
cancellation, output updates, retained calibration, brightness bounds,
invalid configuration values, defaults and balanced UI scopes. It also
checks output messages against requested and applied states on both
presentation paths, and that 10,000 idle UI frames issue no GPU updates, colour-space changes
or popup requests. These checks do not measure in-game frame times.
CTest supplies the compiler and JSON dependency paths. For a direct run,
set `CXX=cl` in a Visual Studio developer shell and `CSX_JSON_INCLUDE` to
the include directory containing `nlohmann/json.hpp`.

`python tests/se_upscaling_ui_test.py` exercises the production selectors
and frame-generation controls. It checks each preset/profile mapping,
method restrictions, the NVIDIA FSR preference before DLSS-G starts,
missing/blocked runtimes, tab routing and balanced widget scopes. It also
checks that closed preset dropdowns do not populate their option lists.

For live SE and AE validation, open CSX and confirm Welcome shows the sloth
and that GitHub opens main-SE. Check all navigation pages with a previously
saved Essentials configuration. Open HDR Display and verify one Enabled
switch in its header on every section, including Performance where offered.
On an HDR display, enable output and adjust both brightness sliders and UI
brightness. Verify paper white remains below peak brightness and disabling
and re-enabling retains values. With Windows HDR off, exercise Cancel and
Force Enable HDR in the warning, then turn output off. Check that cancelling
after selecting the warning preference still asks on the next enable
attempt. Restore Defaults with HDR both on and off, checking actual output
and the header state together. Verify exclusive
fullscreen and missing-display warnings in Output. Use an SDR display only
to check cancellation and disabling an existing saved override.

In Upscaling, exercise Method, Upscale Preset and DLSS Profile dropdowns
and confirm the selected values survive Save and Load. On NVIDIA with
frame generation initially off, open Frame Generation and verify the
AMD preference is available when the FSR runtime is loaded. Enable frame
generation, choose the preferred provider, save and restart. Verify the
reported active provider and its pacing/multiplier controls. Repeat with
the other provider. Confirm HDR reports active output on D3D11 as well as
D3D12; the footer reports the presentation path independently.
