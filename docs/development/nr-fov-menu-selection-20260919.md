# NR FOV selection while runtime upscaling is paused, 2026-09-19

## Cause

The production VR menu can show configured DLSS and an enabled FOV mask
at 0.60 while the effective runtime method is None. The startup main menu
deliberately suppresses vendor upscaling; queued transitions and native
fallback can also make configured and effective methods differ.

The NR mode selector used effective runtime readiness to disable Foveated
and its dependent settings. This blocked configuration even though the
saved mask was valid. The Upscaling summary showed only "inactive", and
the FOV tab could incorrectly explain this as full visible coverage.

Commit `153af7cbc2717a05a606c75ac785f36b4c8091a2` corrected the master
checkbox but left this mode-selection dependency intact. The subsequent
flat A/C commits did not change the VR runtime-method gate. The reported
screenshots are from Skyrim VR; the earlier flat-runtime explanation does
not apply. No DevBench connection or game mutation was attempted against
the production session. Its precise live suppression flag is unverified.

## Correction and preserved contracts

Advanced and Essential NR settings now use the configured upscaler when
deciding whether FOV choices, character controls and colour controls are
editable. The shared mask predicate still requires VR, a loaded Upscaling
feature, a supported configured vendor, FOV enabled and a sub-full-coverage
centre-only mask. Flat Foveated NR remains unsupported.

The zero-argument FOV prerequisite keeps using the effective runtime
method. `IsNeuralRenderingRequested`, execution admission, DevBench
readiness, startup/native fallback and render-scale transition behavior
therefore retain their existing safeguards. A valid menu selection does
not enable vendor work at the startup menu.

Upscaling and FOV status distinguish a configured mask waiting for runtime
upscaling from disabled/full-coverage FOV. These status checks respect the
selected centre-only or FOV+TAA profile. The NR warning likewise follows
configured readiness, retaining its existing NR/TAA replacement conditions.
No shader, inference, colour filter, source/crop or render-scale allocation
policy changed.

## Validation

The extracted UI test separates configured and effective methods. Twelve
cases cover configured DLSS/FSR, effective None/TAA, and the three NR modes
with FOV restriction. They verify editable mode, character, colour and
master controls while runtime NR remains blocked; restoring the effective
vendor restores the request. Existing tests retain unloaded, FOV-off,
full-coverage and flat-runtime rejection.

Both extracted UI and controls executables passed with MSVC 19.51.36256.0
x64, assertions enabled and no compiler warnings. CTest reported 2/2
passed in 0.08 seconds. The additional profile tests distinguish saved
centre-only full coverage from valid FOV+TAA coverage. Fresh production
extraction hashes matched the compiled headers.

Exact local commands and full results are retained under
`build/validation/nr-fov-menu/host`:

```powershell
pwsh -File build/validation/nr-fov-menu/host/run.ps1
pwsh -File ./tools/cmake.ps1 -P build/validation/nr-fov-menu/host/verify-extraction.cmake
```

Passed source checks:

-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`
-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake`
-   `python tests/neural_color/source_contract_test.py`: 8 tests.
-   Preset generation/check after source fingerprint refresh; settings
    values and contract revision 6 are unchanged.
-   Scoped pre-commit and whitespace checks, with pinned clang-format 22.1.4
    checked separately on changed Upscaling ranges and other changed code
    files to avoid unrelated macro reformatting.

No production DLL/AIO, shader build, deployment, push or in-game validation
is part of this correction. The running production DLL does not yet
contain it.
