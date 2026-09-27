# main-vr-nr synchronization, 2026-09-27

## Scope and history

Merge `main-VR` source
`711b18a94d702878e0db8a9818ee6b20ec0619ac` into NR base
`12d2bf8252693382aa874ce76a466352249e3b5d`, retaining both histories and
original authorship. The merge base is
`8fd0e7b8bf493ed14042ce5a45797cdb5c85a446`.

All 49 missing source commits fall within the pinned requested window,
2026-09-26 14:35:09 UTC through 2026-09-27 14:35:09 UTC. No additional
older missing history is imported. The source includes the reviewed Open
Shaders ports already integrated above main-VR `2bbecfce6`.

Work occurs in `build/worktrees/main-vr-nr`. The primary checkout remains
on `main-VR`. The original NR head is retained at
`backup/main-vr-nr-before-sync-20260927`. No push or deployment is part of
this synchronization.

## NR preservation and integration

-   Preserve the NR feature implementation, dedicated neural-rendering
    helpers and shaders, character selection, full-image composition,
    exposure/color path, serialized settings, and preset defaults.
-   Preserve the 96-byte NR center-blend constant buffer and existing field
    offsets. Put blend falloff in its former padding at byte 76. Retain
    character masks, target offsets, and full-image bypass behavior.
-   Include effective curve falloff in NR's retained-output settings key.
    Disabled, neutral, and non-VR curves retain neutral behavior.
-   Apply corrected camera-origin reprojection inside NR's exact current
    and previous crop transforms. Preserve per-eye temporal snapshots,
    successful-crop history, motion-vector scaling, and reset behavior.
-   Treat a successful DLSS VRAM-budget warning consistently for return
    values, telemetry, and NR crop-history publication.
-   Retain NR's raw-depth copy helper and crop-specific callers while
    adopting typed R32 depth encoding in the shared per-eye paths.
-   Include the optional NR DLL in the new runtime payload install list
    and install-time hash guard. Keep optional-provider behavior intact.
-   Retain NR's DevBench fixture admission checks and settings contract
    revision 8. Regenerate the three presets with unchanged NR defaults and
    neutral parallax strength 1.0.

The preservation audit found 315 unchanged NR-exclusive paths. The six
adapted NR-exclusive files are one runtime-packaging CMake file and five
test/helper files. Dedicated NR production trees are unchanged. Existing
FOV constraint, NR configuration accessors, and raw-depth helper function
bodies match the original NR source exactly.

## Validation

Evidence is retained under
`build/analysis/main-vr-nr-sync-20260927/` in the primary repository.
Commands below run from the NR worktree.

```powershell
pwsh ./tools/validate-local.ps1 -OutputDirectory ../../analysis/main-vr-nr-sync-20260927/validation-final
pwsh ../../analysis/main-vr-nr-sync-20260927/verify-built.ps1
pwsh ./tools/cmake.ps1 --build build/nr-color-tests --config Release --parallel 2
```

The configured CTest executable additionally ran `build/nr-color-tests`
with `-C Release --output-on-failure --no-tests=error --timeout 300` and
saved `nr-color.xml` in the evidence directory.

| Check                                                            | Result                          |
| ---------------------------------------------------------------- | ------------------------------- |
| Universal SE/AE/VR Release DLL, DevBench on, Tracy off           | Passed                          |
| Complete controller/shader inventory                             | 202 passed, 0 failed, 0 skipped |
| Separate NR color suite, including WARP color/exposure shaders   | 21 passed                       |
| Standalone preset regression and generator `-Check`              | Passed                          |
| DLL manifest, compiler hash, size, and source identity           | Passed                          |
| Four pinned submodules                                           | Clean and matching              |
| Staged diff and changed-line C++/HLSL formatting                 | Passed                          |
| Scoped whitespace, line-ending, YAML and Prettier hooks          | Passed                          |
| Live SE/AE/VR, HMD, NR inference, and render-scale qualification | Not run                         |

The first complete run passed 201/202 tests. Its sole failure was the
text-based NR menu contract, which still expected the menu expression
replaced by NR commit `b0d9b5f0a` on September 25. The production menu
function is unchanged by this merge, and `VRMenuLayerLifetime` passed.
Update the contract to require explicit menu context and its existing
combined guard; both focused tests and the subsequent complete suite pass.
Preserve that original failure under `validation/` and its focused
recheck in `menu-contract-recheck.log`.

The rebuilt run in `validation-final/` passed 201/202 tests, including
the corrected contract and aggregate shaders, but the unchanged bounds GPU
fixture missed its left-copy readiness deadline. That fixture passed in
the first run and again unchanged in `bounds-recheck.log`. Keep this
intermittent timing failure visible; no production deadline or assertion
was relaxed. Reuse the completed build and run the entire 202-test suite
again, then finish preset, manifest, compiler, and source checks using the
maintained runner's stage function. This continuation passes and is saved
under `verification-final/`. Neither failed full-run summary is rewritten
as a pass. The bounds fixture's timing sensitivity remains a limitation.

The subsequent [adversarial review](main-vr-nr-adversarial-review-20260927.md)
corrects this fixture setup dependency and records ten consecutive passes.
The original validation results and compiled producer below are unchanged.

Added checks exercise asymmetric current/previous stereo crop reprojection,
curve-dependent NR history keys, NR composite layout, and successful NR
provider installation followed by rejection of changed staged DLL bytes.
The runtime packaging test uses fixture bytes. No optional local NR runtime
provider was configured for the DLL build, and no live inference claim is
made.

Diagnostics include the existing FidelityFX CMake policy deprecation and
Release test assertion-option overrides. Full-file clang-format and
gersemi hooks were skipped to preserve existing formatting; changed-line
C++/HLSL formatting was checked separately. Final verification wall time
was 214.06 seconds.

## Compiled producer

The DLL was compiled from the resolved merge before its commit and before
adding this documentation. This producer identity remains authoritative;
the later merge commit is not substituted into the build manifest.

-   Source base: `12d2bf8252693382aa874ce76a466352249e3b5d`
-   Dirty source digest: `a6464032e73e9964bcebc71dac87320d29b93e7f37cf0e1b950877c6300184ce`
-   Build ID: `135f94bf93e1d211cbdf8b52bb827438a4034414dfe3ee777b11d4856a149ff2`
-   DLL SHA-256: `b6cbb61bdae86771e8541d8b73d8886faa68479a2cb6c976797059645a9d1f55`
-   DLL size: 31,014,912 bytes

The validation runner confirms identical source/submodule snapshots before
and after the build and test run. Only this report is added after that
validated snapshot for the initial synchronization commit.

## Materials version follow-up

While validation was running, main-VR advanced to
`1a4262a7d12dc8e11cb5f12b48965744786beaaf`. Merge that descendant above
the completed NR synchronization `ff0e9916e0b8b59a9152dd2496b2ce6839eeb122`
as well. The total imported main-VR history is now 50 previously missing
commits, including this additional same-day correction.

The follow-up changes only Extended Materials from 1.3.0 to 1.4.0 in its
feature INI and compiled version registry. NR's version entry and every
other existing entry remain intact. This aligns shader-cache versioning
with the imported parallax-strength change.

```powershell
python ./tools/feature_version_audit.py --base csx3.19.2 --output ../../analysis/main-vr-nr-sync-20260927/materials-version-audit.md --fail-on-actionable
```

The audit exits 0 with no version-bump or metadata issues. Its report also
lists Neural Rendering and Performance Tuning as new features relative to
the release baseline; those informational suggestions are retained.
The staged diff and scoped hooks pass. No runtime or shader algorithm is
changed by this follow-up, and no additional build is run for it. The DLL
and complete test evidence above describe the initial integration's
compiled producer, before this version-metadata update; they must not be
relabeled as a build of the final branch tip.
