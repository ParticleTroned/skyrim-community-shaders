# NR lighting-preservation sync and integration

## Synced histories

On 2026-09-17, `origin` was fetched and the remote regression tip was also
checked directly with `git ls-remote`. `perf/cpu-dlss-regression-20260916`
remains `60179f5b5289eaf8d42ffe030425f063edf3c6eb`, already an ancestor of
`main-vr-nr`; there were no newer commits on that branch.

The colour-managed NR source advanced from `d69bdb7eb` to
`3bf719807`, comprising `4ef15f932` (lighting preservation) and
`3bf719807` (bright/dark findings). The integration merge includes both while
preserving their original ancestry, the independent Neural Rendering
feature, the regression renderer and the previous port fixes. The source
shader/runtime feature contract is now **NeuralRendering 1.3.0**. Merge
conflicts were resolved for the renamed feature, package and asset checks;
settings tests use the independent feature's nested colour envelope.
A non-UTF-8 dash in the merged colour documentation was normalized to UTF-8.

The VR automation development checkout fast-forwarded from
`7221bbbe8f71109dce06d5dee49246474653c1c1` to
`b544333c96fb3a73777dd6d3f1246151147e49c6`, matching `origin/dev`.
Both local manifest version edits (`0.9.0+codex.20260917131350`) were retained
on the updated manifests; exact originals are backed up in
`C:/src/skyrim-community-shaders/build/automation-sync-20260917-b544333`. The separate
`codex/coc-fast-start` worktree remains clean at `567c6dfc`. This was a
repository sync; no installed plugin cache, MO2 or game state was changed.

## User control and route coverage

Open **Display > Neural Rendering > Colour processing**, select **Preserve
source**, and use **Lighting preservation**. The clamped slider accepts
0–100 percent and defaults to 100. At 100 it suppresses the smooth neural
brightness residual; at 0 it admits that residual within the existing
gain and edge limits. Intermediate values proportionally suppress it.
Fine detail can remain at either end. Neural appearance mix independently
blends in the reconstructed model result.

| NR path                        | Application                                                                                                 |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| A: full resolution             | Shared reconstruction before the final scene composite.                                                     |
| A with FOV restriction         | Same reconstruction, then the existing shared FOV geometry/feather selection.                               |
| B: through FOV                 | Same reconstruction at either supported insertion point.                                                    |
| C: reduced resolution          | Same reconstruction on the render grid before the selected image reaches DLSS.                              |
| Character selection with A/B/C | Same setting for every prepared eye/ROI; final mask selection retains the original outside selected pixels. |

The slider is available at ordinary log levels. It remains visible but is
disabled with a specific notice when colour processing is off, the colour
mode is not Preserve source, neural appearance mix is 1, or detail strength
or maximum gain is zero. Its value is retained. The controls needed to
change those conditions remain available. Missing FOV setup still greys
out FOV-dependent NR settings and shows the existing setup notice; users
can leave that mode or turn NR off. Diagnostics and assessment experiments
remain behind Debug/Trace.

Ordinary feature save/load persists `colour.lightingPreservation` as 0–1;
legacy saves without it default to 1. The `communityshaders.nr_color`
`configure` action exposes `settings.lightingPreservation` with the same
range and compare-and-set revision contract. Invalid types, nonfinite
numbers and out-of-range JSON values are rejected before mutation,
including values that would round into range as floats.

## Integration and robustness

All routes already use the common renderer and colour pipeline, so the
new control did not need duplicate route-specific shader logic. The
constant buffer now has 64 bytes with the control at byte 48 and initialized
padding. The original 48-byte prefix is preserved. Preparation,
reconstruction, delayed measurements and capture companions use the same
frozen configuration. An edit between eyes or regions cannot split their
settings; the next transaction observes it.

Lighting-preservation edits change the configuration revision without
changing input history epochs or adding an inference-history reset. C's
existing per-evaluation reset remains part of that route, independent of
the slider. Empty character selections retain their existing bypass, and
failed inference retains the established fallback.

## Adversarial review and fixes

The follow-up review checked scope, completeness, correctness, robustness,
and DRY across the feature, shared renderer, shader constants, UI,
persistence, capture evidence and packaging. Findings were folded into
this integration merge as requested.

| Finding                                                                                    | Resolution                                                                                                                                                                                                                                 |
| ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Independent NR was absent from the unified-preset source inventory and generated settings. | Add the feature and its serialization/default policy dependencies to the fingerprint. Include all 33 persistent rendering defaults and all colour defaults in the base and three generated tiers. NR stays disabled and available at boot. |
| The old preset contract did not describe the independent NR envelope.                      | Advance runtime and generator settings-contract revision to 6; regenerate all three tiers and the evidence report. Older marked revision-5 presets must be regenerated; unmarked legacy/user settings still migrate.                       |
| Evidence compared float32 bytes and rejected equivalent positive/negative zero.            | Accept runtime numeric equality before checking float32 equivalence; retain rejection of distinct nonzero/subnormal values. Test both zero signs and mixed-sign stereo evidence.                                                           |
| UI tests used default preservation values and could miss preference loss.                  | Test nondefault retention across inactive modes, FOV prerequisites, off/on changes, actual colour-mode edits, passive redraw and failed writes with retry. Preserve the value through legacy boot-disable migration.                       |
| Synthetic route coverage was overstated and omitted explicit mono/FOV cases.               | Name 37 CPU transaction/constant cases explicitly and run the actual reconstruction preflight with each missing prerequisite and stale revision. This is shared-stage proof, not end-to-end rendering.                                     |
| Shader edge coverage was incomplete at lower preservation values.                          | Extend the existing WARP harness for Managed/Legacy invariance, partial appearance interpolation, negative-source fallback and finite HDR. Reuse its analytic oracle and storage checks.                                                   |

The HDR additions exposed a test-oracle limitation: the fixed SDR sRGB
error multiplier underestimates the derivative above the SDR interval.
Only the HDR working-domain error bound now uses decoded endpoints of
the existing encoded tolerance. Stored-RGB tolerances were not relaxed;
no production shader change was required. Initial failure logs are retained
beside the passing colour logs.

No additional rendering defect was found in the shared slider pipeline,
constant-buffer lifetime, transaction latching, failure fallback or
inference-history behavior. The slider adds no dispatch or texture; its
existing constant buffer grows from 48 to 64 bytes. This is a source-level
cost assessment, not a measured performance claim.

## Validation and limits

The initial complete validation record,
`build/validation/20260917T214200455Z-86748d10`, built the DLL/controllers/
shaders and passed **154/154 CTests** in 78.87 seconds. The overall run
**failed** at preset generation because the new feature was missing from
its declared inventory. That failure led to the preset fix above; it is
not counted as a complete pass.

Focused follow-up validation passed:

-   `NeuralRenderingUI` and `NeuralFeatureSettings`: **2/2**, Release,
    0.05 seconds, using the production settings/UI excerpts.
-   `NeuralColorRouteLatch`: **1/1**, RelWithDebInfo, including 37 explicit
    CPU transaction/constant cases and reconstruction rejection preflight.
-   Colour shader WARP: **1/1**, 4.92 seconds; remaining colour suite:
    **19/19**, 7.27 seconds. The Python assessment suite passed **17/17**.
    Logs are `build/nr-color-tests/main-vr-nr-lighting-adversarial-gpu-pass.log`
    and `build/nr-color-tests/main-vr-nr-lighting-adversarial-other.log`.
-   `pwsh ./tests/unified_preset_generator_test.ps1`: passed, including
    rejection of a missing lighting-preservation default without publishing
    partial settings. The final inventory also pins character JSON parsing.

The final committed-source record uses:

```powershell
pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-lighting-adversarial-final-20260917
```

The complete run **passed** in 319.94 seconds from clean compiled snapshot
`feb34598c8b59564c568cd5700b8d50d081402f8`: universal DLL build, **154/154
CTest tests** with no missing/disabled/skipped tests, preset regression
tests, generated-preset check and physical DLL/manifest verification. The
CTest stage took 81.50 seconds and the preset tests took 20.13 seconds.
The final amendment changes only this report; compiled sources, tests
and presets remain byte-identical to that tested snapshot.

-   Build ID: `4fc8e6ed4f3fa57d92473ee5a8afc2eed2f3c697824277b32d43f19e471ee409`.
-   DLL SHA-256: `4b50aaed04477840ac6777f293b95cfcdb114f2718d17d006fc0a779fdc233cb`.
-   DLL size: 30,348,800 bytes.

The directory's `summary.json` and adjacent logs preserve the verdict,
source identity, test inventory, stage timings and manifest verification.
The DLL remains an undeployed build artifact; this is not a running-game
producer check. Raw run artifacts remain local. The root checkout remains
on `main-VR`; all port work is isolated on `main-vr-nr`.

The preserved upstream bright/dark reports describe their own source
build and FOV Upscaled Centre route without character selection. They do
not qualify A, C, character combinations or this integrated build. Their
limited colour-retention findings and evidence exclusions remain intact.
No new live Skyrim, Feature 18 inference, headset-image assessment or
comparative runtime performance measurement has been made here. Dedicated
reduced-resolution colour calibration remains deferred as previously
requested. Source/test coverage establishes control wiring and arithmetic;
it does not establish equal visual quality across different resolutions.
