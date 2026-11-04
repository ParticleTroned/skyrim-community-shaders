# NR integration testing

This branch combines the four independent Neural Rendering changes for testing
against Open Shaders `dev` at
`a975fd4091d6c468f9073c7ae648ec5996676dff`. It includes the complete feature code
and the merge resolutions needed to use the features together. Keep the
upstream PRs in draft until runtime testing is complete.

## Build and source

The combined source branch and downloadable Actions artifacts live in
`ParticleTroned/skyrim-community-shaders`. Only the four independent draft PRs
target `alandtse/open-shaders:dev`.

-   [Integration source](https://github.com/ParticleTroned/skyrim-community-shaders/tree/build/os-dev-nr-integration-20261031)
-   [Integration build runs and artifacts](https://github.com/ParticleTroned/skyrim-community-shaders/actions/workflows/nr-integration-test.yaml)

The integration merges these branch heads in the same order as their upstream
PRs were opened. Each feature branch independently incorporates the upstream
`dev` commit above; the PRs are not stacked on each other.

| Order | Implementation                                                                                 | Feature branch                                                                                                                         | Included head                              |
| ----- | ---------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| 1     | [RGB-guided reconstruction (#1053)](https://github.com/alandtse/open-shaders/pull/1053)        | [Guided reconstruction](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-guided-reconstruction-20261031)   | `3038a54688b37542d26fb8ff00561b2af7b0ed16` |
| 2     | [Peripheral compression (#1054)](https://github.com/alandtse/open-shaders/pull/1054)           | [Peripheral compression](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-peripheral-compression-20261031) | `95f1a62463d8cb19277fc5e0e271712d2dc0137d` |
| 3     | [Pause NR during combat (#1055)](https://github.com/alandtse/open-shaders/pull/1055)           | [Combat pause](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-combat-pause-20261031)                     | `37b881f0a5e84aa5c04796e55e809aa6ccdf4ccb` |
| 4     | [Reduced-model material selection (#1056)](https://github.com/alandtse/open-shaders/pull/1056) | [Scene selection](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-scene-selection-20261031)               | `c4dd3bd5294f35348d0f42eba4cd0f0ecb4d5f71` |

The refresh uses `main-VR` at
`75799ca034562ea124b9aa19e94e531a639a9ec3` as its source baseline. It brings
reconstruction and compression improvements into their existing PR scopes;
combat pause and material selection retain their original behavior with
current-dev compatibility. Face-detail protection and NR-confidence controls
are excluded. Existing provider/network defaults remain unchanged.

The integration also resolves interactions that cannot exist in an isolated
feature PR:

-   Guided and basic reconstruction sample the compressed model coordinates.
    Optional shader fallback is chosen once for both eyes before crop planning.
-   Compression at Model Scale 1 still reconstructs a reduced grid. Bilinear,
    brightness-guided and RGB-guided reconstruction all follow that actual grid,
    including aligned grids with one unchanged axis. Native full-size grids do
    not activate reconstruction.
-   Mapped crops include one model-texel halo for bilinear, brightness-guided and
    RGB-guided reconstruction. All reconstruction taps stay within the evaluated
    crop; the optional mode is frozen before that crop is planned.
-   Material strengths apply in source coordinates after reconstruction, including
    compression-only grids. Native full-size models keep provider protection.

The original rectangular NR coverage, source-space feather and final foveated
upscaler fill/blend remain in use. Follow Foveation uses the current per-eye
rectangles, including live gaze placement. Compression retains the full-eye
rectangular mapping and Open Shaders' multiplicative luminance gain.

Aligned compact dimensions have a fixed source-density floor before bounded
footprint admission. Lowering Model Scale must not jump to a larger grid when
compression falls back. Four inverse-mapped subcells share colour and depth
support, and nonlinear motion is anchored to the chosen depth donor. Finite
reconstruction saturates to the existing half-float range; corrupt data retains
the original source pixel and alpha.

## Download and install

1. Open the integration workflow and choose the run for the commit you want to
   test. Wait for the run and its packaging job to succeed.
2. Download **`nr-integration-test-build`** from that run's Artifacts section.
   GitHub may require sign-in. Artifacts are retained for 30 days.
3. Install the ZIP as a separate mod overriding the existing Open Shaders files.
   Its `SKSE/` and `Shaders/` directories belong at the game `Data/` root. Keep
   the plugin DLL and matching shader sources together.
4. Supply your NR runtime locally at
   `Data/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll`, using a validated build
   accepted by the existing NR runtime checks. See the
   [upstream NR guide](https://github.com/alandtse/open-shaders/blob/a975fd4091d6c468f9073c7ae648ec5996676dff/docs/development/neural-rendering.md#enable)
   for version requirements.
5. Enable Neural Rendering in the Upscaling panel. Enable Developer Mode for
   Model Scale, compression and reconstruction controls. Start with a normal
   full-scene context and the baseline below.

**No NR DLL is committed or redistributed by these PRs or this integration
build.** The build checks files and archives before uploading the
compiler distribution artifacts, and checks the final test package again.
An NR runtime filename or an archive that cannot be inspected fails the upload
gate. The ordinary DLSS/FSR runtime files from Open Shaders' existing AIO
packaging remain included.

The test ZIP contains the universal SE/AE/VR `CommunityShaders.dll`, its PDB,
the complete AIO shader/data payload, the compaction MIT notice, this guide,
and `NR-INTEGRATION-MANIFEST.json`. The manifest records the exact integration
commit, all four included heads, build run, plugin hash and verified payload
hashes. The DevBench bridge is enabled. Shader sources compile through the
normal runtime path; this package does not require a separate precompiled cache.

To roll back, disable the integration mod and restore the previous matching
Open Shaders package and your saved configuration.

## Test matrix

Run on VR and at least one flat variant (SE or AE). Use the same save, camera,
resolution, upscaler and NR runtime when comparing modes. Record the runtime
build and effective NR diagnostics so a fallback cannot masquerade as a test
of the requested feature.

Start with NR enabled, Model Scale 1, compression 0, combat pause off, all
materials selected, and no actor crop, calibration, mirrored-eye or diagnostic
input transforms. Keep **Use resolution-scaled NR motion** enabled, diagnostic
options off, and conversion, exposure and composite modes at their production
settings. In VR, start with Separate eyes, then repeat the combined cases with
Parallel eyes and Side by side. Alternate eyes intentionally retains
the ordinary sampling route and is not the mode for testing compression.

| Case                            | Settings                                                                                                          | What to verify                                                                                                                                                                                                                                                 |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Baseline                        | Model Scale 1; compression 0                                                                                      | Existing NR output and native material protection still work. Disabling NR restores ordinary upscaling.                                                                                                                                                        |
| Uniform reduction               | Model Scale 0.5, then 0.75 and 0.99; compression 0                                                                | Test guidance off (bilinear), brightness guidance and RGB Guidance. Look for stable colour edges, finite output and correct effective mode at each reduced grid.                                                                                               |
| Compression only                | Model Scale 1; compression 50                                                                                     | Test bilinear, brightness guidance and RGB Guidance on the actual compact grid. RGB must remain available at nominal scale 1 when compression is active. The centre remains protected and the periphery reconstructs without stale borders.                    |
| All rendering changes           | Model Scale 0.5; compression 50; RGB Guidance on                                                                  | Change material selection and partial strengths. The original source-resolution categories should control where reconstructed NR gain applies.                                                                                                                 |
| Reconstruction/crop interaction | Model Scale 0.5 and 1; compression 50; all three reconstruction modes                                             | In VR, enable Follow Foveation and vary the existing foveated rectangles. Check the one-texel model halo, crop edges, eye boundaries and head movement for uninitialised strips or cross-eye sampling.                                                         |
| Rectangular coverage            | Repeat combined case with Follow Foveation on/off; enable Follow eye gaze in VR when available                    | Actual per-eye gaze rectangles must drive the NR crop. Move gaze across crop boundaries, interrupt tracking and verify the saved rectangle resumes correctly. Preserve the existing full-view fill/blend; compare its settings separately from reconstruction. |
| Density and alignment           | Hold compression fixed and lower Model Scale in small steps, including low compression near an alignment boundary | Reported model dimensions must not grow on fallback. Check admission reasons, the fixed source-density floor and rounded single-axis savings.                                                                                                                  |
| Motion donors                   | Move foreground geometry across a distant background with compression active                                      | Colour, depth and motion should describe the same source support. Check periphery and depth edges for sliding or inconsistent motion; repeat supported forward/reverse-Z configurations and bright/dark scenes.                                                |
| Combat gate                     | Combined rendering case; Pause NR During Combat on                                                                | Enter combat: NR suspends for both eyes while ordinary upscaling continues. Leave combat: NR resumes with fresh history after three continuous peaceful, unpaused seconds.                                                                                     |
| Interrupted recovery            | Open a menu during the combat recovery delay                                                                      | The pending peaceful interval is interrupted. Closing the menu requires a fresh continuous recovery interval; a menu during already peaceful operation does not create a combat suspension.                                                                    |
| Settings and transitions        | Save/reload; toggle NR and each added setting                                                                     | Settings persist, both eyes change together, effective-mode/mapping changes reset history, and no healthy runtime is needlessly recreated by the combat gate.                                                                                                  |

Also sample compression 0, 6, 50 and 100 and Model Scale 0.25, 0.5, 0.75,
0.99 and 1. If a configuration is rejected by the bounded-footprint or
minimum-savings policy,
verify the reported reason and the ordinary-model fallback. Actor crops,
calibration, Alternate eyes, mirrored inputs and incompatible diagnostic routes
are intentionally outside compression admission.

For material selection, test all selected, one excluded category, partial
strengths, and edge softness 0 and 4. All selected should match normal NR; a
retained native provider binding reuses an identity mask. Selecting fewer
materials changes where NR appears, not the amount of model evaluation work.
A rejected native material-protection path must not disable a valid reduced-model
compositor.

## Evidence and remaining validation

Focused local checks passed for the refreshed implementations:

-   61 production-header Catch2 policy and interaction cases passed, with
    3,977,437 assertions covering actual-grid RGB admission, density/alignment
    fallback, combat recovery and material provider/compositor behavior.
-   39 actual shader-test entry points compiled under strict flags, including finite
    half-float output, shared subcell coverage and forward/reverse-Z donor
    selection, with the pinned ShaderTestFramework.
-   32 LowRes and four CategoryAlpha permutations compiled with Linux DXC,
    covering reconstruction, compression, flat/VR and forward/reverse depth.
-   Translation synchronization/orphan checks, formatting and diff checks, plus
    compatibility review of upstream scene settings, runtime capabilities and
    current rectangular gaze/FOV behavior.

The Linux DXC checks are preliminary Shader Model 6 compilation checks without
GPU execution. LowRes checks omit `COMPUTESHADER` because of the existing
upstream SharedData compatibility limitation; CategoryAlpha includes it.
Production Windows compiler and GPU validation remain required. The integration
workflow uses Open Shaders' Windows ALL build, full-config and release-parity
FXC shader validation for flat/VR, shader tests and the C++ test targets.

Use the linked workflow for the Windows build and final package-verification
results for the exact integration commit. A downloadable test build is ready
only after that run succeeds. Earlier successful runs cover only their recorded
commits; the current source branch or a queued job is not a completed build.

In-game validation remains pending until recorded by a tester. Passing the
combined build does not establish standalone in-game validation for each PR.
No measured performance improvement is claimed.

Record the integration commit from the manifest, game edition, GPU/driver,
resolution/upscaler, NR runtime version, settings, effective reconstruction,
compression admission reason/model size, material availability and combat phase.
The existing `neuralRenderingStatus` query and Upscaling diagnostics expose
these fields. Include `OpenShaders.log`, paired captures and reproducible steps
with any report. Use the same scene and GPU per-pass timings for performance
comparisons.

## Attribution

Peripheral compaction retains Yuri Grib (BeliyG3)'s copyright, full MIT license
and source provenance in
`SKSE/Plugins/CommunityShaders/Licenses/NeuralRendering/PeripheralCompression.txt`.
The package verifies that this notice matches the integration checkout.
