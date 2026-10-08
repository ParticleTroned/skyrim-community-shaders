# Parallax strength

Extended Materials exposes **Parallax Strength** in its essential and performance
settings and its advanced Parallax section. The range is `0` to `2`, with `1` preserving
the existing appearance. Changes apply live; use the menu's save operation
to persist them.

The multiplier applies to supported standard meshes, complex materials,
TruePBR meshes (including interlayer parallax), legacy terrain and TruePBR
terrain. It multiplies the existing material depth and curvature correction.
Existing feature switches still determine which materials support parallax.
Water retains its independent Adaptive Balance control.

At `0`, parallax ray marching and self-shadow evaluation are bypassed.
Terrain height blending and TruePBR coat/refraction parameters remain
available. Normal maps, mesh geometry and collision are unchanged. Values
above `1` can expose stretching or seams at shallow viewing angles; strength
is not a sampling-quality or proportional performance control.

## Shader contract

The multiplier scales ray travel after the authored height range is
established. Terrain keeps its original layer heights, normalization and
blend weights, avoiding a change to height blending or clipping the legacy
terrain's normalized search range when strength exceeds `1`.

Mesh, directional-terrain and point-light-terrain shadow sampling distances
use the same multiplier. Zero strength bypasses shadow evaluation, including
mesh and terrain shadow-base samples. Mesh POM keeps the original UV without marching;
terrain uses its existing height-blending fallback without marching. The
shared implementation serves SE, AE and each VR eye.

`ExtendedMaterials::Settings::ParallaxStrength` occupies a former padding
slot at byte `24`. The shared GPU settings structure remains `32` bytes.
Legacy settings without the field default to `1`. Load, save and performance
state restoration clamp finite values to `[0,2]` and replace non-finite values
with `1`. Slider edits are sanitized immediately, including typed input.
Malformed JSON types follow the feature loader's existing logged default
fallback. Performance state capture preserves the selected multiplier.

## DevBench

The `communityshaders.menu` action `set_parallax_strength` requires numeric
`strength` in `[0,2]` and loaded Extended Materials. Invalid, non-finite or
out-of-range input fails before mutation. The action applies on the main
thread, requests the menu's dirty-settings check and returns
`persisted: false`. It neither enables material features nor saves settings.

```json
{ "action": "set_parallax_strength", "strength": 0.5 }
```

`status.parallaxStrength` reports the configured multiplier;
`status.extendedMaterialsLoaded` reports feature availability. Water's value
remains under `status.adaptiveBalanceVisuals`.

## Validation coverage

`ExtendedMaterialsSettings` executes the production settings definition,
serializer and load/save/restore methods outside the engine. It checks old
settings, layout, zero, fractional and boosted strength, bounds, non-finite
values and round trips through performance-state capture.

`TestParallaxStrength.hlsl` runs the production mesh ray marcher and shadow
sampler against analytic heights. It checks neutral displacement, half and
double strength, multiplication with authored depth, zero, opposite view
directions and shadow response. `TestTerrainParallaxStrength.hlsl` checks
terrain ray travel, independent height blending and zero-strength shadows
against analytic samples using the production terrain functions.
Full Lighting shader compilation also needs
standard parallax, complex materials, TruePBR, terrain and VR permutations.

In-game visual acceptance requires those material types at strengths `0`,
`0.5`, `1` and `2`, including both VR eyes, oblique views, terrain boundaries
and directional/point lights. Shader fixtures do not replace that check.

## Review and performance limits

The adversarial review corrected preset-contract integration, performance-panel
reset coverage, immediate input sanitization and unnecessary zero-strength
mesh mip/shadow sampling. The shared slider helper, finite-value utility,
serializer and existing ray/shadow functions avoid parallel implementations.
All three generated presets explicitly retain neutral strength at revision 5.

Strength changes add no textures, buffers, allocations, render passes or
shader permutations. They retain the existing ray-step and shadow-tap budgets;
the multiplier is applied outside the ray loop. Zero bypasses mesh mip lookup,
ray marching and shadow-base sampling; complex materials reuse their existing
sample. Terrain height blending and TruePBR coating retain their own work.
Vanilla multilayer shading and water are outside this Extended Materials
depth control.

FXC disassembly of the standard mesh `Lighting:Pixel:3000001` variant retains
81 texture-sampling instructions. Static instruction slots increase from
5,023 to 5,064 for the uniform checks and arithmetic. This is a code-size
comparison, not a GPU timing result. Different depth can change sampled UVs,
cache locality and ray-hit early termination even at the same step budget.
Identical frame times are not guaranteed or claimed.

The final production shader matrix compiled 18 SE/AE and 22 VR variants with
zero warnings and errors. It covers standard parallax, complex materials,
TruePBR, terrain, deferred rendering, skinned meshes and landscape LOD blending.
Commands used `hlslkit-compile` with the Windows SDK `10.0.28000.0` FXC,
the preserved `SE.yaml`/`VR.yaml` configurations, `--max-warnings 0` and
`--suppress-warnings X1519` (zero suppressed warnings). Compile logs,
configuration inventory, timings and disassembly are preserved locally under
`build/validation/parallax-strength-shaders/` (`*-reviewed*` outputs).

No game deployment, live DevBench action, headset visual check or matched
in-game frame-time measurement was performed; no Skyrim process was running.

## Validation record (2026-09-26)

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/parallax-strength-commit-20260926`
built the universal Release DLL and passed all 153 CTest groups, with none
skipped. This includes the settings regression and complete GPU shader suite.
The runner subsequently failed the preset suite's lock-owner process exit
assertion with empty stderr. A focused rerun of
`pwsh ./tests/unified_preset_generator_test.ps1` passed; the original failure
and retry remain in separate logs. No preset-test implementation was changed.

The remaining stages passed separately: `pwsh ./tools/generate-unified-presets.ps1 -Check`,
`pwsh ./tools/git.ps1 diff --cached --check`, and
`python ./tools/build_provenance.py verify --manifest build/ALL/Release/CSX.BuildManifest.json --artifact build/ALL/Release/CommunityShaders.dll`.
Before/after source snapshots matched the DLL's source and dependency identity;
DLL size and compiler SHA-256 also matched. `completion.json` records the
completed checks without overwriting the runner's failed `summary.json`.

-   Producer Build ID: `013b169ee223ab6c0709c6b877a414a4e524dd55088198561ed897d161c387aa`.
-   Source base: `2108f177be54411b80bc74a8cd85a1fd14b19683`, with the reviewed working changes.
-   Source dirty digest: `0d10694b272fa7903d14089cea347811791f57c7f645354c81b456a42031d2fa`.
-   DLL SHA-256: `65c57ff172296b3150a2020ad679069363c5a6ac80db23e74dbb8e4e27ec9f93`; size: `29070336` bytes.

Scoped pre-commit checks passed. C++ and feature/test HLSL formatting was checked
separately. Whole-file clang-format changes to existing unrelated alignment in
`Lighting.hlsl`/`SharedData.hlsli`, and gersemi changes to existing CMake layout,
were excluded to retain a focused diff; those two hooks are skipped at commit.
This validation-record addition follows the preserved producer identity and
does not change the tested implementation.
