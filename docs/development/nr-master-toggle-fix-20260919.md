# NR master and dependency controls, 2026-09-19

## Cause and correction

With Full resolution and Restrict to FOV mask selected, disabling or
unconfiguring Upscaling FOV greyed out the unchecked NR master. The same
availability guard could lock Characters only on. This reproduced directly
in the UI source; it did not depend on a world frame or a provider failure.

The master is now always editable. Missing route prerequisites affect
`IsNeuralRenderingRequested()`, not the checkbox. Pending FOV-dependent
requests display an explanation. Selected FOV and character restrictions
remain removable, and Full resolution remains selectable from Foveated.
Child preferences survive toggling the master. NR still disables FOV + TAA;
failed backend retirement still prevents unsafe re-enabling and never
prevents disabling NR. No shader or render-route algorithm changed.

The [menu FOV follow-up](nr-fov-menu-selection-20260919.md) separates valid
configured masks from temporary runtime suppression, so the Foveated mode
and dependent settings remain editable at the startup menu. This original
master correction did not yet resolve that separate mode-selection gate.

DevBench accepts valid pending FOV-dependent configuration, matching the
menu and persistent feature configuration. Readiness still reports
`fov_not_configured`; unsupported runtimes and invalid provider parameters
remain rejected. The registered description documents this distinction.

## Regression coverage and validation

The existing extracted UI harness now includes the production selection
controls and execution gate. Its added matrix covers 96 combinations of
world-state availability, flat/VR runtime, mode, FOV availability, FOV
restriction and character restriction, with four repeated master toggles
per combination. It checks removable restrictions, route escape, preserved
preferences, unloaded-feature rejection and balanced ImGui state. Existing
control tests retain failed-retirement and FOV + TAA coverage.

Passed source-only checks:

-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`
-   `pwsh -File ./tools/cmake.ps1 -DPROJECT_ROOT=. -DOUTPUT_DIRECTORY=build/validation/nr-master-controls/ui -P tests/extract_neural_rendering_ui.cmake`
-   `pwsh -File ./tools/generate-unified-presets.ps1 -Check`
-   Scoped pre-commit checks and `pwsh -File ./tools/git.ps1 diff --check`.
    Clang-format 22.1.4 was applied separately to the changed Upscaling UI
    range and the complete changed bridge/test files. The full Upscaling
    formatter hook was skipped to avoid unrelated macro formatting.

The additional `tests/neural_multi_roi_contract_test.cmake` source check
failed at its old tooltip assertion, `Uses at most two persistent Feature
18 regions per eye`. That text is already absent at parent `09f9defbf`;
the check predates the individual-tooltip rewrite. Its mismatch is not a
passing result and is unrelated to this master-switch correction.

Compiled tests, DLL/shader builds, packaging and live game verification are
not run under the user's standing no-build instruction.

The flat character NR extension is paused separately; this correction does
not enable character rendering on SE/AE.
