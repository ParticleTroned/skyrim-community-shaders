# VR Renderscale NR dependency

The VR pre-DLSS NR route previously admitted native scene targets when
Render Scale was switched off. A reported symptom was overbright fire;
the user also reported a crash after an off/on cycle, without a crash log.
That crash has not been attributed to a specific failure or reproduced.

Enabled Renderscale NR now protects its Render Scale dependency in both
Upscaling menu surfaces and at the shared profile-transition entry point.
Ordinary disable requests, native AA/DLAA, and methods that cannot scale
are rejected before settings changes or resource-transition publication.
The public API exposes an explicit, non-retryable dependency condition;
DevBench reports the same reason and the NR prerequisite state.

Selecting a scaled preset while this NR mode is enabled also requests
Render Scale, even when automatic linking was previously disabled. Saved
settings with Render Scale off or native AA remain editable: select a
scaled DLSS preset and enable Render Scale to resume NR. The NR master
switch always remains editable, and switching off NR or changing its mode
releases the lock. An unloaded NR feature cannot lock Render Scale.

Runtime admission independently requires requested, latched and active
scaled targets. Thus old configuration files, earlier queued transitions,
main-menu native presentation and runtime fallbacks cannot feed native
scene targets into pre-DLSS VR NR. Existing recovery relatches and explicit
startup-native fallback controls retain their safety behavior; they cannot
make NR run without its runtime prerequisite. Full/Foveated NR and flat
SE/AE are not subject to the new VR dependency. No shader, image copy,
resource-allocation or colour-filter changes are introduced.

The preset values and saved schema are unchanged. The source fingerprint
is refreshed after reviewing the new runtime dependency; NR is disabled in
all three generated unified presets.

## Validation

Regression coverage exercises the production NR gate across requested,
latched and active combinations, every NR mode, VR/flat, and disabled or
unloaded NR. It verifies the master remains editable. API policy coverage
checks direct and environment-transition rejection.

Passed:

-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`:
    descriptor fields and rejection ordering, plus the existing NR contracts.
-   `tests/extract_neural_rendering_ui.cmake` with this worktree as
    `PROJECT_ROOT`: production UI/gate extraction completed.
-   Both modified DevBench descriptors parsed as JSON, including their new
    dependency metadata.
-   `pwsh -File ./tools/generate-unified-presets.ps1 -Check`: all three presets
    verified; their only settings-file difference is the source fingerprint.
-   Changed C++ ranges formatted with clang-format 22.1.4. Scoped pre-commit
    trailing-whitespace, line-ending and Prettier checks passed. The full-file
    clang-format hook was skipped after range formatting; YAML/gersemi hooks
    selected no files. `git diff --check` passed.

Following the user's production-AIO request, the isolated C++23/MSVC host
compiled and passed all 12 focused NR/API tests, including the new runtime
admission matrix and public API rejection policy. Assertions were enabled.
Commands: `pwsh -File ./tools/cmake.ps1 --build build/validation/nr-renderscale-fov/host/build --config Release --parallel 4`
and `ctest --test-dir build/validation/nr-renderscale-fov/host/build -C Release --output-on-failure`.
The first host link omitted `UpscalingContract.cpp`; adding that existing
implementation to the local test host resolved the harness-only failure.
Logs are retained under
`build/production-artifacts/20260919T225304890511Z-nr-scale-guard-efcb70e2/`.

Production build and archive validation are recorded in that directory's
build logs and AIO receipt. Shader execution and in-game validation have not
run. These checks do not reproduce the reported crash or establish its
underlying cause.
