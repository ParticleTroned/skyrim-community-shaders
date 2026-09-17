# Neural Rendering port: adversarial review

This follow-up reviews `709eb9a1d` on `main-vr-nr` against the original
colour-managed source and the full-resolution, foveated, reduced-resolution
and character-ROI requirements. Fixes are separated by topic. Independent
cross-reviews checked the rendering fixes and configuration changes after
implementation. The original `main-VR` checkout was preserved.

## Findings and topic commits

| Commit      | Defect or unnecessary work                                                                                                                      | Correction                                                                                                                                      |
| ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `92190b167` | Ordinary upscaling reloads could reset independent NR controls; obsolete lane settings could reject migration.                                  | Share serialization/migration, preserve unrelated live settings, keep disk diagnostics transient, and prepare reset candidates before mutation. |
| `af18b0d89` | A/C FOV masks were omitted from the settings key when vendor foveation was off. Saved insertion choices also differed from effective placement. | Include active geometry and effective placement, including geometry edits during preview.                                                       |
| `4a4afa658` | Repeated late hooks could process an already processed/UI-containing image; failed preparation could retry after scene mutation.                | Attempt full-resolution preparation once per fresh world frame, before post-processing; late hooks only consume.                                |
| `0a2717344` | Main late-NR HMD repair used reduced coordinates against reconstructed output-sized depth.                                                      | Require same-frame reconstruction/resource proof and use the output eye grid; missing proof leaves the image intact.                            |
| `43b6185d3` | Successful C/C+D submit composition could select a missing or stale post-upscale float texture.                                                 | Share bridge admission between producer and consumer; C consumes its current completed DLSS image.                                              |
| `923900796` | Full-image C filled an underlay that the completed output immediately overwrote.                                                                | Skip that non-TAA dispatch only after proving complete current coverage; retain partial/FOV/TAA paths.                                          |
| `ef71d5b50` | Production UI exposed experiments and diagnostics at Info, used ineffective placement controls, and mishandled failed resets.                   | Keep ordinary controls readable; gate developer controls at Debug/Trace; use effective settings and explicit, failure-aware recovery.           |
| `73326ebd9` | Renaming the colour feature could reactivate legacy boot-disabled colour processing.                                                            | Migrate only that colour disable state, preserve its preferences and independent NR enablement.                                                 |
| `5e22f337f` | FOV-dependent choices stayed active without FOV; A used the wrong active mask profile and feather.                                              | Share real prerequisites across UI/runtime/DevBench and use shared geometry, offsets, feather and full-coverage semantics.                      |
| `23545e260` | A GPU test depended on helper-thread scheduling within a short production deadline.                                                             | Make timeout and exact-ready cases deterministic; retain a negative control for renewed per-eye deadlines.                                      |

Configuration regression coverage also checks that explicit modern lane
values override an obsolete malformed alias. A failed backend transition
retains prior NR settings. An explicit feature-default reset clears the
session mask/debug modes; an unrelated upscaler edit preserves them.

## Completeness and correctness

| Route                 | Reviewed behavior                                                                                                      |
| --------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| A: full resolution    | Final scene before UI, mono or VR; optional shared FOV mask; fresh guide preparation with one attempt per frame.       |
| B: foveated           | Existing FOV route and insertion choices, preserving ordinary upscaler output and character baseline.                  |
| C: reduced resolution | Feature 18 on the engine render grid before DLSS; matching input/output/guide extents and DLSS temporal ownership.     |
| D with A/B/C          | Existing category/actor admission, ROI and exact mask selection, produced-pixel bounds and coherent stereo completion. |

SE/AE retains full-resolution NR. Foveated, reduced-resolution and
character routes remain explicitly VR-only. The independent feature owns
rendering and colour settings while sharing the renderer's existing
resource, guide, foveation and publication machinery.

The full-resolution review also checked framebuffer selection, the
private colour transaction, alpha preservation, shared masks and character
texel selection. The reduced-route cross-review checked failed inference,
empty character eyes, retained A/B resources, partial preparation and
stereo failure. No additional actionable defect was found in those checks.

FOV-dependent route choices are disabled until Upscaling provides active,
supported foveation with partial center coverage. Existing default mask
geometry is valid; there is no separate setup-complete flag. The reduced
route's forced dispatch allocation cannot satisfy this prerequisite.
Unavailable saved requests remain inactive. The UI keeps controls for
disabling NR or leaving the unavailable mode. DevBench exposes the same
prerequisite and rejects enabled configurations that do not satisfy it.

Full-resolution FOV-only now uses the existing shared visible mask:
FOV-only center or FOV+TAA outer boundary, horizontal expansion, resolved
eye offsets, and the shared `FoveatedCommon::kCenterFeather` of 0.05. The
same pinned feather reaches the final composite. Vendor reconstruction
support and the independent NR blend slider cannot widen this mask.
This preserves the shared shader-feature mask contract; vendor-center
blend sliders continue to control their existing upscaler transition.
When the shared FOV+TAA outer boundary has full coverage, A uses the same
full-image result as the existing shared mask consumers, without rounded
corner remnants.

Detailed evidence is in the [full-resolution lifecycle review](main-vr-nr-full-resolution-lifecycle-review.md)
and [depth presentation review](main-vr-nr-depth-presentation-review.md).

## Production UI

Info and ordinary production levels expose the NR master switch, A/B/C
selection, FOV restriction, character selection, image presets and
strengths, applicable feathering, and the colour mode/detail controls.
Labels describe their image effect. Unsupported runtime choices explain
their requirements. Character selection can be cleared while NR is off,
including recovery from an incompatible saved SE/AE configuration.

The existing `State::IsDeveloperMode()` gate restricts execution-lane
comparisons, technical ROI experiments, forced masks, previews, exposure
and domain experiments, transport bypass, A/B display overrides, capture
and measurement controls, and detailed evidence to Debug/Trace.
Rendering admission itself does not require developer mode.

An active hidden image override produces a compact warning with an
explicit recovery action. Merely drawing the UI does not cancel a
deliberate automation experiment or rewrite preferences. Failed settings
transitions roll back; failed runtime retirement does not falsely clear
dependent masks/history. Normal colour controls remain available.

The UI test executes the production colour UI and actual developer-level
gate with an ImGui harness. It covers Info-and-higher exclusion,
Debug/Trace inclusion, explicit recovery, retained normal controls and
rejected changes. The larger upscaling UI was source-reviewed and compiled;
its live layout was not inspected in Skyrim. The expanded UI test also
executes the production FOV prerequisite helpers and checks colour-control
mutation blocking across all A/B/C, FOV-only and availability combinations.

## OpenNR, performance and reuse

The comparison pins [OpenNR](https://github.com/olekspa/OpenNR/tree/29d9218f17daf323cf67996d40b0210a7ea6e6b8)
at `29d9218f17daf323cf67996d40b0210a7ea6e6b8`. Its smaller-model spatial
proxy/reconstruction mode and its pre-upscale mode are distinct designs.
CSX C retains its render-grid NR-then-DLSS design; it does not silently
add another downsample or change to post-upscale NR on failure.

The [OpenNR review](main-vr-nr-opennr-review.md) records primary-source
links and the lessons about motion-vector scaling, separate guide extents,
history, reconstruction and failure behavior. No OpenNR implementation or
colour algorithm was copied. Dedicated Vincent colour calibration remains
deferred as requested in the original port scope.

The guarded underlay omission removes one structurally redundant dispatch.
It requires a completed current DLSS producer, full visible/output
rectangles, exact source allocation and full-image C without TAA or FOV
restriction. Its blend path overwrites every pixel without reading the
old destination. Poisoned-destination GPU tests check that invariant.
No measured frame-time or VR-budget improvement is claimed.

The fixes reuse the existing developer gate, framebuffer transaction,
colour pipeline, FOV masks and final composite. Serialization and bridge
selection each have one shared implementation. Coverage uses the existing
rectangle type. Debug-only evidence is not formatted on the production
colour page.

## Validation evidence

All commands run from `build/worktrees/main-vr-nr`. Git ancestry checks
passed for both the target tip `60179f5b5` and colour-managed source
`d69bdb7eb`; the original checkout remains on `main-VR`, with its existing
untracked `.xmake/` directory untouched. Every topic commit passed normal
staged-file repository hooks. There were no HLSL changes in this review.

The focused actual shared-mask planner and DevBench descriptor contract
passed 2/2. Production colour UI/FOV prerequisite cases and the legacy
colour migration cases each passed independently before the final build.
The colour suite passed 19/19 in 14.09 seconds after rebuilding its Release
targets, including framebuffer WARP, colour shader and exposure tests:

```powershell
pwsh ./tools/cmake.ps1 --build build/nr-color-tests --config Release
ctest --test-dir build/nr-color-tests -C Release --output-on-failure
```

That colour-suite run preceded the additional FOV UI/geometry and
boot-disable fixes; those later changes are covered by the focused tests
and final controller run. The colour-suite sources were unchanged afterward.
The unchanged runtime-staging fixture is excluded from the repeated
controller run; its earlier 168.613-second pass belongs to the initial port
report. No repeated packaging run or GPU compilation matrix is claimed.

The initial final controller run recorded 145/146 passes and failed
`character_mask_bounds_shaders`: its sleeping helper thread was not
scheduled in time during concurrent compiler work. The original
`build/ALL/main-vr-nr-review-final-controllers.log` is preserved. The test
now holds both copies behind a real GPU gate, checks timeout destination
preservation, waits for fixture readiness separately, and checks exact
completed results. A controlled fence with a negative control detects
incorrect per-eye deadline renewal without depending on worker scheduling.
Five focused repetitions passed. The production reader's wait policy and
budget were unchanged.

The first strict aggregate build also exposed MSVC C4456 in the new UI
matrix (a snapshot variable shadowed an earlier local), promoted to C2220
by warnings-as-errors. The standalone harness had not enabled that policy.
The snapshot was renamed, and the strict target then built and passed.
The failed aggregate build is preserved in
`build/ALL/main-vr-nr-final-fov-build.log`; subsequent validation uses the
corrected committed source.

The final aggregate build passed from clean source
`23545e26097a004fe49352b17dca49adef031bcc`, with SE, AE, VR and DevBench
support enabled and automatic deployment disabled:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests CommunityShaders -- /m:2
ctest --test-dir build/ALL -C Release -L ControllerTests -E '^NeuralRenderingRuntimeStaging$' --output-on-failure -j 2
python tools/build_provenance.py verify --manifest build/ALL/Release/CSX.BuildManifest.json --artifact build/ALL/Release/CommunityShaders.dll
```

The build invocation additionally supplied MSBuild's `/flp` argument to
preserve `build/ALL/main-vr-nr-validated-build.log`, and redirected console
output to `build/ALL/main-vr-nr-validated-build-console.log`. The DLL build
reported **0 compiler warnings, 0 errors**, with an MSBuild elapsed time
of **100.30 seconds**. An earlier reconfiguration retained the existing
third-party FidelityFX `CMP0116` deprecation warning; it was not a compiler
warning.
The final controller run passed **147/147 in 7.88 seconds**, recorded in
`build/ALL/main-vr-nr-validated-controllers.log`, after compilation ended.
This includes the previously failing bounds test, full-resolution FOV
planner, UI, settings, final-LDR/character WARP and lifecycle/depth tests.

| Verified artifact property | Value                                                              |
| -------------------------- | ------------------------------------------------------------------ |
| Build ID                   | `2ce43da081bacdf50168d46323c707bd9fa47768a39deef5afecb6349c2345ee` |
| Compiled source            | `23545e26097a004fe49352b17dca49adef031bcc` (clean)                 |
| DLL SHA-256                | `82be79794597f30ef6a0693444b7b7f1342a33783e31af1ac41a160e521d99b8` |
| DLL size                   | 30,343,680 bytes                                                   |
| Manifest                   | `build/ALL/Release/CSX.BuildManifest.json`                         |
| DLL                        | `build/ALL/Release/CommunityShaders.dll`                           |

Manifest identity and artifact verification passed. The subsequent report
commit changes documentation only; the compiled identity above remains
authoritative. This verifies the isolated build artifact, not an enabled
MO2 deployment or running Skyrim producer.

## Validation limits

No game deployment, live NVIDIA Feature 18 inference, physical-headset
quality review, moving-scene stability assay or comparative performance
measurement ran. The private NR provider's compatibility and temporal
quality therefore remain runtime validation items, not established passes.
Shader source did not change in this follow-up; the existing broad FXC
results remain historical evidence, with the affected behavior exercised
by controller and WARP tests here.

The refreshed automation still requires a full host reload under
`C:/src/skyrim-community-shaders/build/pr597-automation-dev-20260912/AGENTS.md:87`:
"fully reload the Codex host; a new chat alone is not a safe pickup boundary."
No runtime measurement ledger was manufactured from source or GPU-unit tests.
