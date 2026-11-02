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
The shared confirmation dialog saves the warning preference only on
confirmation. Cancel and the close button leave output and that preference
unchanged. Defaults apply the detected output mode immediately.
Brightness controls retain their ranges and enforce paper white below peak
brightness. Configuration loads validate calibration, including non-finite
UI brightness, before updating the output. Output documents Windows HDR and borderless-window requirements;
Brightness explains the limits of reported display metadata; Interface
explains the separate main-menu and loading-screen brightness behavior.

## Validation

`python tests/se_hdr_ui_test.py` compiles the production HDR UI methods with
observable widgets. It checks detection, forced-output confirmation and
cancellation, output updates, retained calibration, brightness bounds,
invalid configuration values, defaults and balanced UI scopes. It also
checks that 10,000 idle UI frames issue no GPU updates, colour-space changes
or popup requests. These checks do not measure in-game frame times.
CTest supplies the compiler and JSON dependency paths. For a direct run,
set `CXX=cl` in a Visual Studio developer shell and `CSX_JSON_INCLUDE` to
the include directory containing `nlohmann/json.hpp`.

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
