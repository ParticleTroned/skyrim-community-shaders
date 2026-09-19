# Renderscale NR before DLSS and optional VR FOV, 2026-09-19

The label remains **Renderscale NR before DLSS** and the serialized mode
remains `reduced_resolution` / `2`. Following a reported slowdown after
`20717c138`, the user requested restoring full-eye behavior and exposing
the cropped FOV path as a toggle for comparison. This is not a measured
performance fix or a diagnosis of the reported CPU increase.

## Behavior

VR Renderscale NR exposes **Use FOV mask for Renderscale NR** below the
mode selector, including Essential settings. Its independent persistent
`neuralRenderingRenderscaleFov` boolean defaults to false, including when
loading older settings without this field. It does not inherit or rewrite
the Full resolution `neuralRenderingFovOnly` preference.

-   Off restores full-eye NR followed by DLSS, even without configured FOV.
    Crop offsets become zero, both input/output extents cover the whole eye,
    blending uses full-image coverage and the existing successful full-eye
    path skips its unnecessary periphery underlay.
-   On retains the cropped behavior of `20717c138`: configured VR FOV is
    required, with the same scale, independent eye offsets, reconstruction
    support, feather and spatial periphery. Both NR and DLSS use the crop.
-   Foveated NR continues to use FOV automatically. Full resolution retains
    its independent optional restriction. NR's Enabled switch remains
    editable, and a selected renderscale restriction remains removable when
    FOV becomes unavailable.

Changing the switch uses the existing settings-key, history invalidation
and transition-frame guard. Incompatible resource dimensions are retired
through the existing checked allocation path; no new allocation strategy,
backend defaults or synchronization behavior is introduced.

The internal render-resolution character/ROI composite still uses its
whole private crop. FOV is applied at the outer eye-image composition,
avoiding a second mask in the wrong coordinate grid. Exact character
selection, private candidate publication, source/guide alignment,
colour/Lighting preservation and per-frame NR reset remain unchanged.
DLSS still owns temporal reconstruction.

SE/AE keeps the full-image mono Renderscale NR adapter and ignores the VR
switch. DevBench `nr_configure` accepts `renderscaleFov`, while status and
capture execution metadata retain its value. The registered descriptor,
strict boolean parser and `fovPrerequisite.required` reflect the selected
route. Existing mode names and the Full resolution `fovOnly` control are
unchanged.

## Optional-toggle validation

Eight isolated MSVC CPU targets passed: extracted UI/dispatch admission,
controls/history transitions, pipeline policy, FOV geometry, settings
keys, colour route latching, DevBench request parsing and feature settings.
Geometry checks switch the production planner on and off for 18 VR
comparisons across odd dimensions, edge offsets and saved Full resolution
preferences. Flat coverage ignores both saved VR mask preferences.
Three existing source contracts and eight colour source checks passed.
The registered DevBench JSON descriptor parses and admits the new boolean.
Unified presets retain the off default under settings contract revision 7;
generation and the generator's `-Check` validation passed.

Validation used the configure/build commands below, followed by
`ctest --test-dir build/validation/nr-renderscale-fov/host/build --output-on-failure`.
Production DLL/AIO compilation, shader tests, deployment and live timing or
image comparisons were not run for the toggle. No performance improvement
is claimed. Keep scene, render scale, mask geometry, character selection
and colour settings matched when comparing off versus on in game.

## Adversarial review

The toggle restores the existing full-eye adapter; it does not add an
inference pass, staging texture or GPU copy. Review found and corrected:

-   The full-eye adapter forced dispatch availability even with global FOV
    off. The shared profile now queries configured mask availability, so
    lighting, shadows, GI and other shared-mask consumers cannot infer an
    enabled mask merely from NR's dispatch adapter. Saved, enabled FOV
    remains available to those consumers independently of NR restriction.
-   Pre-DLSS selection copied the complete baseline before overwriting it.
    It now skips that copy when validated, disjoint regions cover the
    entire selection texture. This applies to either toggle state and
    mono/stereo. Partial regions still copy the baseline once to preserve
    untouched pixels; character blending reads the immutable baseline SRV.
    Overlapping or invalid regions fail before allocation or GPU writes.
    Copy telemetry records zero when the copy is skipped.
-   Offline capture joins did not compare the new renderscale FOV field.
    They now reject changed, missing or malformed values across the
    producer, execution and region descriptors. Older captures that omit
    the field consistently remain readable.

The existing crop/guide preparation, private NR publication, selection
composition and final DLSS output commit remain in place. No resource
aliasing or bypass of stereo publication, source/jitter proof, colour
reconstruction or character feathering was introduced. This is a bounded
copy reduction, not a claim that the route is zero-copy.

Nine isolated CPU targets passed, including the extracted production
selection function. Its mock GPU checks cover both eye indices, full and
partial single/two-region coverage, character blending, poisoned prior
output, repeated storage reuse, invalid/overlapping regions, missing masks
and dispatch failure. These verify control flow and copy accounting, not
rendered shader quality. The 27 Python transaction-evidence tests passed. Review also corrected
the source-contract test's stale feature-version assertion to 1-4-0; all
eight colour source checks and three CMake source contracts passed.
Production DLL, shader/runtime and comparative timing validation remain
unrun; no speedup is asserted.

## Original automatic-mask validation

MSVC 19.51.36256.0 x64 compiled six isolated CPU targets with assertions
enabled. CTest passed 6/6: extracted UI, controls, pipeline policy, FOV
geometry, settings keys and colour route latching. The geometry test uses
the production crop planner for 18 comparisons against Foveated, covering
three scales, three signed offsets, both saved restriction values, both
eyes, odd dimensions and screen edges. It also checks complete flat
coverage. Colour tests passed 42 CPU transaction/constant cases; these
are not rendered image comparisons.

Commands:

```powershell
pwsh -File ./tools/cmake.ps1 -S build/validation/nr-renderscale-fov/host -B build/validation/nr-renderscale-fov/host/build -G Ninja -DCMAKE_BUILD_TYPE=Release
pwsh -File ./tools/cmake.ps1 --build build/validation/nr-renderscale-fov/host/build --parallel 6
ctest --test-dir build/validation/nr-renderscale-fov/host/build --output-on-failure -V
pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake
pwsh -File ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake
pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_submit_pair_contract_test.cmake
python tests/neural_color/source_contract_test.py
```

All three source contracts and eight colour source checks passed.
Preset generation and verification refreshed only source fingerprints;
settings values and contract revision 6 are unchanged. Scoped hooks and
clang-format 22.1.4 checks passed; Upscaling formatting was limited to
changed ranges to avoid unrelated macro churn.
Production DLL/AIO compilation, shader tests, game deployment, runtime
visual qualification and performance measurement were not run. They
remain required before claiming in-game image correctness or speed.
