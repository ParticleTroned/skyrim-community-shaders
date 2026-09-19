# Renderscale NR before DLSS and automatic VR FOV, 2026-09-19

The user requested the exact label **Renderscale NR before DLSS** and
automatic FOV masking like Foveated NR. The serialized mode remains
`reduced_resolution` / `2`; no saved preference or default is rewritten.

## Behavior

VR Foveated and Renderscale NR now share an automatic-mask policy across
menu selection, execution readiness, settings keys, crop planning, output
composition and DevBench descriptions/status. Both require configured,
enabled FOV. The global FOV switch remains user-controlled. Startup or
transition suppression still gates execution separately from editable
configuration, and Enabled remains an independent master.

The optional restriction checkbox is absent for both automatic VR modes.
Its saved value is retained for Full resolution. Renderscale NR no longer
forces full-eye dispatch when that saved value is false. Its crop retains
the FOV scale, horizontal shape, independent eye offsets and reconstruction
support. The output uses the existing FOV feather and normal periphery.

The internal render-resolution character/ROI composite still uses its
whole private crop. FOV is applied at the outer eye-image composition,
avoiding a second mask in the wrong coordinate grid. Exact character
selection, private candidate publication, source/guide alignment,
colour/Lighting preservation and per-frame NR reset remain unchanged.
DLSS still owns temporal reconstruction.

SE/AE keeps the full-image mono Renderscale NR adapter without automatic
VR FOV. Existing saved unavailable restrictions remain removable. The
DevBench enum and settings schema remain compatible; descriptions and
`fovPrerequisite.required` reflect the automatic VR requirement.

## Validation

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
