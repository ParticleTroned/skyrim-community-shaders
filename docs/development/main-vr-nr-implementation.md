# Neural Rendering port to the CPU/DLSS regression branch

The subsequent [adversarial review](main-vr-nr-adversarial-review.md)
records the topic fixes, production UI, shared FOV prerequisites and mask
handling, OpenNR comparison, and final validated build identity. The
evidence below describes the initial port and remains historical.

## Scope and provenance

`main-vr-nr` starts at the tip of
`origin/perf/cpu-dlss-regression-20260916`, commit
`60179f5b5289eaf8d42ffe030425f063edf3c6eb`. The worktree is
`build/worktrees/main-vr-nr`; the original `main-VR` checkout is preserved.
The colour-managed source is
`origin/work/face-of-gogh-colour-managed-20260914`, commit
`d69bdb7ebc899ce24971eac59b007503e0dc08bb`. The merge preserves both
histories. Earlier NR reports carried by that history describe their
original builds, not runtime validation of this port.

The port retains the target branch's CPU diagnostic gates, ordinary
upscaling transitions, resource publication identity, screenshot ownership,
and standard Streamline runtime. The source branch's unrelated subsurface
scattering changes are excluded. No performance improvement is claimed.
Special colour calibration for Vincent remains outside this port.

## Feature and execution contracts

The independent **Neural Rendering** feature owns ordinary rendering,
character selection, and colour controls. Its package, feature INI and
registry use `NeuralRendering`. Its saved configuration contains
`schemaVersion`, `rendering` and `colour`. Legacy `NeuralColor` colour
settings and `Upscaling.neural*` settings migrate to that feature.
Assessment overrides remain transient. Renderer execution remains in the
existing upscaling integration to share guide preparation, foveation,
history transitions and compositor ownership.

The modes are `full_resolution`, `foveated`, and `reduced_resolution`.
Full resolution optionally limits the result to the shared FOV masks.
Character selection is orthogonal to the mode and uses the existing
category, actor and ROI admission policies. Reduced-resolution NR runs
before upscaling; the existing upscaler retains temporal ownership.
All routes use the shared colour transport and reconstruction pipeline.
The complete A/B/C plus character matrix is for VR. SE/AE supports the
full-resolution route; VR foveation, reduced-resolution routing and
character-category authoring are rejected explicitly on flat runtimes.
The port preserves their ordinary renderer allocations and shader layout.

`communityshaders.neural_rendering` exposes NR configuration, status,
readiness and stereo implementation-lane cycling. `nr_configure.mode`
selects the A/B/C route; `nr_cycle_modes` cycles stereo implementation
lanes. The existing `communityshaders.nr_color`
interface and legacy NR actions on `communityshaders.renderscale` remain
available for existing capture tools. Mode names and numeric values,
FOV-only controls and other user inputs are validated before mutation.

The standard DLSS runtime remains the target branch's version 2.14.1.
`CSX_LOCAL_DLSSNR_RUNTIME_FILE` optionally supplies `nvngx_dlssnr.dll`.
Staging records a hash-pinned complete DLL inventory and validates the
transaction before mutation. Clearing the option removes only the managed
NR provider from the isolated build. No private provider is distributed by
this change. See the [runtime packaging instructions](../../features/Upscaling/Shaders/Upscaling/Streamline/README.md).

## Integration defects found and corrected

-   Source character-category bits collided with the target's additive
    lighting descriptor. Category bits move together in C++ and HLSL to
    bits 9–10, with exclusion at bit 11.
-   The source OpenVR Submit signature did not match the target's engine
    hook ABI. The target two-argument `void` hook is retained, with NR pair
    handling inside that boundary.
-   Cached NR replay bypassed the target's producer freshness checks.
    Replay now follows the same exact publication, retained depth and
    motion proof as the ordinary path.
-   Source rendering paths treated a deferred upscaler result as a simple
    boolean failure. The target result states and fallback behavior are
    retained explicitly.
-   Source configuration and build modules would replace newer target
    runtime and packaging policies. NR staging is now an additive module;
    the target's provenance and normal runtime packaging remain intact.
-   An NR-only staging manifest would have removed normal DLLs during
    transaction cleanup. The manifest includes the complete normal runtime
    inventory, and a lifecycle test verifies preservation.
-   Character mask preparation assumed two eyes. Allocation, capture,
    reduction and readback now account for an explicit eye count. This does
    not expand the VR-only category-authoring contract to flat runtimes.
-   NR and SSS could install the same draw hook twice. Shared installation
    is guarded with `std::call_once` to preserve the original-call address.
-   Malformed per-eye screenshot evidence could throw while joining
    diagnostics. The join retains received evidence and reports explicit
    unavailable reasons for invalid objects, booleans or eye identities.
-   Persistence could revive transient diagnostic/mask overrides or retry a
    failed backend transition through defaults. Only persistent controls
    migrate and save; a failed transition is reported without that retry.
-   Source profiler macros and several presentation interfaces differed
    from the newer target. The port uses target profiling and presentation
    contracts while retaining NR-specific evidence.
-   An adversarial 9-by-5 GPU case poisoned unused ROI texels with NaN.
    Bilinear sampling at a selected ROI boundary returned NaN instead of
    the expected 0.7. Character mask, baseline and candidate inputs share
    an integer texel grid, so their composite now uses exact texel loads.
    Ordinary scaled sampling is unchanged. Eight odd-dimension cases and
    104 final-LDR cases verify coverage, opposite-eye preservation and
    zero-weight rejection of unproduced candidate pixels.

The [capture notes](main-vr-nr-capture-notes.md),
[route notes](main-vr-nr-routing-notes.md), and
[integration assessment](main-vr-nr-integration-notes.md) record the detailed
ownership, image-domain and adversarial findings.

The final independent review found no remaining source-coordinate issue
in A/B/C character selection: checked visible-rectangle containment makes
the source offset nonnegative and keeps every dispatched texel inside the
shared grid. Reduced-resolution selection occurs before vendor upscaling,
so the fix does not remove required resize filtering. A separate final
audit found no material omission in feature registration, migration,
mode/FOV controls, character composition or DevBench exposure.

## Tooling refresh

DevBench `main` fast-forwarded from
`01991ad710e1a4d306271baaac187034a68b5df7` to
`5734b262f9274606b07dfb17842d1bb7906ad058`.
The VR automation development checkout is at origin/dev
`7221bbbe8f71109dce06d5dee49246474653c1c1`; the separate
`codex/coc-fast-start` checkout was preserved.

The marketplace and installed cache were refreshed from that automation
source, rotating `0.9.0+codex.20260914200143` to
`0.9.0+codex.20260917131350`. All 184 source, marketplace and installed
files matched byte for byte. The plugin manifest SHA-256 is
`B524DF4697F5BF28FAA76AF9C4C45B04AC7FE94F1EEE489FC4467A852110494F`.
The registration selects the new version. The local cache-version
manifests are preserved as uncommitted automation changes.

The automation repository's `AGENTS.md` requires a full Codex host reload
after cache rotation before running a runtime protocol; a new chat is not
a sufficient boundary. No live Skyrim protocol, deployment or release
qualification has been performed in this session. No new runtime
measurement is attributed to this build, and no measurement ledger is
created from dry tests.

## Validation evidence

The universal build uses MSVC 19.51.36252, Visual Studio 18 2026 and Windows
SDK 10.0.28000 with SE, AE, VR and DevBench enabled. Game deployment is
disabled. AIO staging is enabled to assemble shaders inside the isolated
`build/ALL` configuration; no archive or deployment target is requested.

-   `pwsh ./tools/setup-dev.ps1 -SkipHookEnvironments -KeepHttpsRemotes`
    passed in the new worktree.
-   `ctest --test-dir build/nr-color-tests -C Release --output-on-failure`
    passed all 19 tests after formatting (39.44 seconds):
    colour/exposure policy, lifecycle and evidence, WARP framebuffer,
    colour/exposure shaders, source/registration/assets contracts, Streamline
    constants and the assessment/capture workflows.
-   The 12 character/ROI/rectangle/alignment/controller and character-mask
    WARP tests initially passed in `build/ALL` (4.23 seconds).
-   Six screenshot tests passed in `build/nr-capture-tests` (0.10 seconds).
    The NR request parser and feature persistence extraction tests also
    passed independently.
-   The NR capture, evidence, request-parser and persistence tests are also
    registered in the main `controller_tests` target through a shared CMake
    module; the standalone harness uses the same registration.
-   The universal Release DLL and controller-test build passed, including
    the final framebuffer, effective-UI and capture-fingerprint changes.
-   `ctest --test-dir build/ALL -C Release -L ControllerTests -E '^NeuralRenderingRuntimeStaging$' --output-on-failure -j 2`
    passed all 140 tests after the ROI boundary fix (7.10 seconds).
    The runtime-staging fixture was already tested separately below.
-   Full FXC validation passed 3,425 flat and 3,605 VR shader permutations
    with zero errors and zero new warnings. Each matrix suppressed 696
    known X1519 warnings using the repository's validation configuration.
-   After refreshing `prepare_shaders`, the ten optimized VR foveated
    compute variants passed FXC again after the ROI fix, with zero warnings
    and zero errors. This matrix includes the modified composite shader.
-   `python tests/neural_rendering_runtime_test.py` passed the optional
    provider lifecycle, failed-hash transaction, missing-provider and
    outside-build-path rejection scenarios (168.613 seconds while the
    universal build was active).

The full shader commands used `hlslkit-compile`, SDK 10.0.28000 FXC,
`--shader-dir build/ALL/aio/Shaders`, `--max-warnings 0`,
`--suppress-warnings X1519` and `--jobs 4`. Their configurations were
`.github/configs/shader-validation.yaml` and
`.github/configs/shader-validation-vr.yaml`. Compute validation used
`.github/configs/runtime-foveated-compute-shaders-vr.yaml`,
`--shader-dir build/ALL/aio/Shaders/Upscaling`,
`--extra-includes build/ALL/aio/Shaders`, `--optimization-level 3` and
`--jobs 2`. Logs and timing reports remain in the isolated worktree's
`build/ALL/main-vr-nr-*.log` and `build/nr-shader-validation/`.

### Final committed-source build

`pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders -- /m:2`
passed from clean commit `22e4ae29c6bc7f223cb225ab220f9d74a286d97b`
with zero warnings and zero errors (98.93 seconds). The implementation
merge is `19bb0320c7ff3b6cb116e2e86e1321fe36ad7ce4`, with the stated target
and colour-managed source commits as its parents; `22e4ae29c` adds the
adversarial ROI boundary fix. This subsequent report-only commit does not
change the compiled source.

The adjacent `build/ALL/Release/CSX.BuildManifest.json` reports:

-   Build ID: `56b49e82b0eb4a70a0a6e8c311ae8ef28ec47451d597a357b4c97268b7dc7361`.
-   Source: `22e4ae29c6bc7f223cb225ab220f9d74a286d97b`, `dirty: false`.
-   DLL SHA-256: `24956c186345a24902c7dd22ef51b072aaf0c304aa722f87a0a7249cb6e1fb36`.
-   DLL size: 30,336,000 bytes.

The physical Release DLL hash and size were compared with that manifest
and matched. The build log is `build/ALL/main-vr-nr-clean-build.log`.
Scoped commit hooks passed. No DLL was deployed to the game, so this is a
build-artifact identity check, not verification of a running producer.

These checks establish bounded policy, shader and tooling behavior. They
do not establish physical-HMD image quality, NR provider compatibility or
measured runtime performance for this port.
