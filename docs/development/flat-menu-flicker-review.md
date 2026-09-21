# Flat-screen menu flicker review

Reviewed against main-VR source
`0ade28157025881b2d6f334cf0b9d62497e3f640` on 2026-09-21.

The reported AE welcome dialog alternated between two text sizes. Opening
the full menu with Trace logging also alternated its position. Production
code executed with real ImGui reproduces both effects; a screenshot alone
does not establish two separate UI instances.

## Findings and corrections

| Finding                                                                                                                   | Correction                                                                                                             | Scope                                                                                 |
| ------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| Welcome sizing divides by the previous frame's retained window font scale, reproducing 42/54 px and 21/27 px alternation. | Normalize the window scale before computing the absolute font size.                                                    | Existing admission excludes this dialog from VR.                                      |
| The settings menu tests its displaced position, then restores itself into the status panel every other frame.             | Recompute displacement from the saved position until that position is clear.                                           | Existing flat-only layout branch.                                                     |
| Review found that retaining that position could prevent a user from dragging the menu away.                               | An explicit drag replaces the saved placement.                                                                         | Flat-only; reproduced by a failing test before correction.                            |
| Applying a theme replaces live ImGui font and DPI state with saved style values.                                          | Preserve live base size, DPI scale and pending next-frame size.                                                        | Explicitly excluded from VR.                                                          |
| Flat `DXGI_PRESENT_TEST` probes run rendering and advance frame accounting.                                               | Forward the probe unchanged before any frame effects.                                                                  | Explicitly excluded from VR; not proven to have caused this reported run's flicker.   |
| The initial Present regression checked effects only in aggregate.                                                         | Assert exact mirror/reset/accepted-frame/overlay/capture/Present order, return propagation and startup blur admission. | Tests cover both runtimes, diagnostics on/off and successful/occluded/failed returns. |

The tests extract the production methods and layout block, rather than
maintaining a second algorithm. One shared generation target owns their
headers, preventing duplicate generation by parallel test builds.

## VR preservation and change boundaries

VR desktop mirroring, HMD overlay rendering, canvas sizing, locked/unlocked
layout, pointer mapping, eye submission and render-scale behavior are
unchanged. VR retains its existing theme assignment and Present behavior,
including status probes. Tests execute the welcome admission rule with no
menu instance to confirm that VR rejects it before accessing flat settings.

No shader, setting value, default, cache format, instrumentation or new
runtime hook was introduced. Menu.cpp is part of the preset source inventory;
the existing metadata refresh was advanced to the corrected source hash.
Contract revision 5 and all generated preset settings remain unchanged.

## Validation

The production DLL and both controller/shader test groups were built using
the universal ALL configuration, with SE/AE/VR enabled and DevBench/Tracy off.

```powershell
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release -N
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
pwsh ./tests/unified_preset_generator_test.ps1
pwsh ./tools/generate-unified-presets.ps1 -Check
```

The DLL build and both test-group builds passed. CTest discovered and ran
142 tests: 142 passed, zero failed, skipped or not built (52.59 seconds).
Both preset checks passed. Scoped pre-commit checks and `git diff --check`
passed. Gersemi checked the two new CMake files and only the added include
line in CMakeLists.txt, avoiding unrelated existing formatting changes.

Local before/after output is retained under
`build/flatrim-ui-flicker-20260921/`, including
`review-drag-before.log`, `review-focused-tests.log`,
`review-production-build.log` and `review-ctest.log`.

Live visual checks using the corrected DLL on SE/AE and VR desktop/HMD
remain pending. Automated tests and unchanged VR paths do not substitute
for those in-game checks.
