# Performance Tuning measurement sequence

The SE/AE cost test uses the closed-menu sequence and countdown from
`main-VR` at `cd333196ede38eeb48aa05d15f5895ca1d1c8fbc`. This is a
UI/protocol port only: the SE/AE Present, CPU, and D3D11 GPU timing sources
remain unchanged. No VR runtime, OpenXR, or shader dependency is added.

## Sequence

1. Capture the original settings, disable frame generation for the test,
   suppress the automatic idle camera, and close the settings window.
2. Wait at least 10 seconds, then capture the current state for 5 seconds.
3. Apply the feature's inactive comparison state. Wait at least 10 seconds
   (or its longer feature-specific settling requirement), then capture it
   for 5 seconds.
4. Restore the original feature and frame-generation settings. Wait at
   least 1 second and for feature readiness, then reopen the results.

The nominal sequence is 31 seconds. Readiness, fresh-frame requirements,
and bounded delayed-query draining may extend it. The existing SE/AE
failure checks remain: scene changes, loading, focus loss, interrupted
timing, invalid samples, and unexpected feature state stop the test and
attempt restoration. A 45-second overall deadline bounds the run. Each
capture has a six-second deadline; no additional measurement pass is
inserted to retry missing GPU/CPU samples. Restart cooldown is 10 seconds.
Failed restoration remains visible and blocks another run until retried.

Both captures use five one-second blocks. Results are the direct
current-minus-comparison means with the existing coverage, practical-floor,
and block-agreement checks. They do not claim a third capture or linear
drift correction. Matched delayed GPU/CPU samples still belong to the
original captured frames; missing samples are not replaced with zero.

The non-interactive countdown uses `ImGui::GetTime()` for phase waits and
captured frame duration for measurement progress. It stays at one second
until restoration finishes. The panel is centred horizontally, 32 scaled
pixels below the main viewport's work-area top, with the same width,
background opacity, and progress-bar layout as the source implementation.
Reopening settings or entering the editor cancels the measurement. The
idle-camera delay is restored on success, cancellation, and failure.
The editor and cost test share the same suppression lease so overlapping
ownership cannot overwrite the original delay.

Community Shaders must stay enabled and the separate A/B configuration
tester must stay disabled. Either condition prevents starting a cost test
and aborts an active one through the existing restoration path.

## Validation

Build and run the deterministic host tests with an SE/AE-only preset:

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -B build/se-performance-sequence -DZIP_TO_DIST=OFF -DAIO_ZIP_TO_DIST=OFF -DAUTO_PLUGIN_DEPLOYMENT=OFF -DBUILD_SHADER_TESTS=OFF -DBUILD_HOST_TESTS=ON
pwsh ./tools/cmake.ps1 --build build/se-performance-sequence --config Release --target run_host_tests --parallel 4
pwsh ./tools/cmake.ps1 --build build/se-performance-sequence --config Release --target CommunityShaders --parallel 4
```

Host tests cover the five-second windows, delayed sample associations,
two-capture differences, unavailable metrics, and countdown phase boundaries.
They do not substitute for the following in-game checks on SE and AE:

-   In a stable scene, start a feature cost test. Confirm the menu closes,
    the top-centre counter begins at about 31 seconds, two captures run, and
    results reopen after restoration without a third capture.
-   Repeat with Upscaling, including frame generation initially enabled.
    Verify original settings and the idle-camera delay are restored.
-   Reopen the menu during each phase, open the editor, move the camera,
    pause, lose focus, and load another scene. Verify cancellation/failure
    never leaves the comparison state or frame-generation override active.
-   Exercise a delayed/unready feature and unavailable GPU timings. Verify
    bounded waits, the nonzero active countdown, explicit unavailable
    metrics, and the restoration retry path rather than a false success.
-   Change UI scale and resolution. Confirm top-centre placement and that
    the overlay does not capture mouse or keyboard input.
-   Enable the separate A/B tester or disable CS before starting. Confirm
    the cost test is unavailable with an explanation. Toggle CS off during
    each phase and verify cancellation restores the original settings.

## Adversarial review evidence

Reviewed against SE base `c393cd3eb` on 2026-09-21:

-   The CPU/GPU/Present producers, profiler query ownership, rendering
    hooks, shaders, runtime presets, and dependencies are unchanged. No
    VR runtime checks, OpenVR/OpenXR calls, or VR-only resources were added.
-   Menu-close ownership is separate from cancellation. Clearing the
    session during loading cannot reopen the settings menu later. Scene
    changes remain checked during the final restore wait.
-   The existing settings/frame-generation restoration and retry path is
    reused. Idle-camera suppression is shared with the editor and tested
    for overlapping owners, repeated release, and unavailable settings.
-   Both result overloads use the same coverage and statistics code. The
    two-capture path leaves third-capture and drift fields unavailable.
-   A/B configuration swaps and disabling CS prevent or abort a test;
    obsolete baseline/third-capture translation keys were removed.

Validation passed: the SE/AE Release DLL and `performance_tuning_tests`
targets built; the test executable passed **34 cases / 25,055 assertions**;
scoped pre-commit checks, `git diff --check`, and translation ordering passed.
The build used the existing checked-out dependencies and unrelated local
scene-depth changes; it is not a clean production artifact.

The aggregate `run_host_tests` attempt was blocked by the unrelated local
`scene_depth_test` missing Tracy stubs. Repository-wide translation
extraction also reports unrelated pre-existing drift, including the
Terrain Shadows wait string. Neither issue is included in this port.
In-game SE/AE smoke tests above have **not** been run; no runtime performance
claim, deployment, shader-cache package, or VR qualification is made.
