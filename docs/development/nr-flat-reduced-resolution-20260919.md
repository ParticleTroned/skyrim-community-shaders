# Flat reduced-resolution Neural Rendering, 2026-09-19

## Supported configuration

SE/AE now exposes two NR modes: Full resolution on the final scene before
UI, and Reduced resolution at the current render resolution before DLSS.
Reduced resolution requires NVIDIA DLSS; selecting it with another upscaler
keeps the setting pending and the master switch editable. Foveated and
stereo scheduling remain VR-only. A saved FOV restriction can be removed on
flat; it cannot silently enable an unsupported FOV path.

Both flat modes use current face, skin and hair selection, independent
strengths, ROI/Multi-ROI and colour/Lighting preservation. Reduced mode
composites selected pixels before DLSS reconstructs the image. It therefore
does not promise the final-pixel isolation of Full resolution. Provider
auto-mask remains enabled and the exact CSX mask remains composite-only.
No new defaults, model-size slider or independent NR downsample were added.

## Source comparison and implementation

Revalidated the actual SE branches, including:

-   `origin/Vincent-se`: `9d34c271cb9f0dc0440b6b6c0b575873e02c54a2`.
-   `origin/Gogh-se`: `6501ebb743dfdd1a237deda4ab2bfb04a6adf7cf`.

Vincent-se supplied the narrow integration idea: a private render-resolution
NR result can feed ordinary DLSS, with the original input retained on NR
failure. It does not implement the separate model-resolution control found
in the older VR Vincent work. Its simpler character/colour and NR-history
handling were not ported. Gogh-se's category and material exclusions are
already represented by this branch's newer shared character machinery;
its earlier post-DLSS placement does not replace the final-scene A route.

The new mono adapter reuses the existing provider transaction, character
preparation/finalization, exact-mask reduced-output compositor and resource
lifecycle. Colour, encoded motion and raw depth are staged at matching
active render dimensions, separate from engine allocation capacity. Native
NR stays 1:1: input, guide, output and crop use that same grid. DLSS retains
its original guides, jitter and render-to-display transform.

Output is private until preparation, inference and selected-pixel
composition succeed. Empty selection and rejected, stale, unavailable or
failed preparation feed the original scene to DLSS. The mono transaction
uses one logical view, including independent physical slots 0/4 for two
regions. Failed mask finalization resolves aborted preparation evidence.
Published route evidence distinguishes applied NR from successful DLSS
consumption; a failed DLSS submission cannot publish committed NR output.

Route C still resets NR every call. DLSS retains temporal history during a
steady original-input or enhanced-input run and resets when those sources
change. The remembered source advances only after successful DLSS. This
also covers disable, empty selection, menu rejection and recovery from NR
failure. Existing mode, size and settings transition resets remain active.

The existing compute-state guard now accepts an optional UAV count. Mono
input preparation preserves and unbinds all four encoder UAV slots,
including encoded motion at u2, then restores shader, resources, b0 and
predication. Existing guard users retain their one-UAV behavior. No HLSL or
colour-filter boundaries changed. DevBench capability, description and
schema advertise flat A/C and one-view character diagnostics; its stereo
implementation cycle remains VR-only.

## Validation and remaining evidence

Passed source checks:

-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`
-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake`
-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_submit_pair_contract_test.cmake`
-   `python tests/neural_color/source_contract_test.py`: 8 tests.

Focused validation compiled only isolated test executables using MSVC
19.51.36256.0 x64 and Ninja. All four tests passed:

-   Pipeline policy, including flat A/C availability and DLSS input-history
    transitions after success, failure, disable and empty selection.
-   Extracted production UI: flat A/C categories, mode selection, FOV
    restrictions and an editable master across upscaler choices.
-   Colour latch: 42 CPU transaction/constant cases, including mono A/C
    with zero/one/two regions, plus reconstruction preflight/route isolation.
-   Shader-free D3D11 WARP state guard: four/default-one UAV coverage,
    untouched higher slots, SRVs, b0 and predication restored. Shader coverage
    is limited to null shader/no class instances; no shader was compiled.

Exact local commands and outputs are retained under
`build/validation/nr-flat-reduced/host`:

```powershell
pwsh -File build/validation/nr-flat-reduced/host/run.ps1
pwsh -File ./tools/cmake.ps1 -P build/validation/nr-flat-reduced/host/verify-extraction.cmake
```

CTest reported 4/4 passed in 0.16 seconds. Assertions were explicitly
enabled; MSVC emitted the expected `/DNDEBUG` override warning for
`/UNDEBUG`. Fresh production extraction hashes matched all four compiled
generated headers. The initial UI test used an outdated item label; after
correcting it to `Reduced resolution before DLSS`, the targeted and full
runs passed. Initial failure evidence remains alongside final results.

Scoped pre-commit and whitespace checks passed. Pinned clang-format 22.1.4
checked changed Upscaling ranges and complete remaining changed code files;
the full-file hook was skipped to avoid unrelated macro formatting. Preset
generation/check passed after refreshing source fingerprints. Preset
values, schema and contract revision 6 are unchanged. These checks do not
establish in-game NR quality or performance.

No product DLL, shader compilation, AIO, game deployment or push is part of
this change. The previous production archive does not contain either flat
extension. Live SE/AE checks still need to cover A/C switching, all
character categories and strengths, empty/nonempty and one/two regions,
changing scale, menus, failure/recovery, colour modes and Lighting
preservation. VR A/B/C regression and flat visual/temporal qualification
remain unmeasured. No performance claim is made.
