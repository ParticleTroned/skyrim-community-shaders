# NR integration testing

This branch combines the four independent Neural Rendering changes for testing
against Open Shaders `dev` at
`58396345573534ef89d2b0676a88b89c583ec948`. It includes the complete feature code
and the merge resolutions needed to use the features together. Keep the
upstream PRs in draft until runtime testing is complete.

## Build and source

The combined source branch and downloadable Actions artifacts live in
`ParticleTroned/skyrim-community-shaders`. Only the four independent draft PRs
target `alandtse/open-shaders:dev`.

- [Integration source](https://github.com/ParticleTroned/skyrim-community-shaders/tree/build/os-dev-nr-integration-20261031)
- [Integration build runs and artifacts](https://github.com/ParticleTroned/skyrim-community-shaders/actions/workflows/nr-integration-test.yaml)

The integration merges these branch heads in the same order as their upstream PRs were opened. Each feature branch independently starts at the upstream
`dev` commit above; the PRs are not stacked on each other.

| Order | Implementation | Feature branch | Included head |
| --- | --- | --- | --- |
| 1 | [RGB-guided reconstruction (#1053)](https://github.com/alandtse/open-shaders/pull/1053) | [Guided reconstruction](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-guided-reconstruction-20261031) | `5d35eb601d2c1068bec9a3a2bc52b0d8fa6bb3b9` |
| 2 | [Peripheral compression (#1054)](https://github.com/alandtse/open-shaders/pull/1054) | [Peripheral compression](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-peripheral-compression-20261031) | `dd9c974e75f72d90128f5133b30205b9425da31d` |
| 3 | [Pause NR during combat (#1055)](https://github.com/alandtse/open-shaders/pull/1055) | [Combat pause](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-combat-pause-20261031) | `ae0420524922d658d023b6417761677f55a584fc` |
| 4 | [Reduced-model material selection (#1056)](https://github.com/alandtse/open-shaders/pull/1056) | [Scene selection](https://github.com/ParticleTroned/skyrim-community-shaders/tree/pr/os-dev-nr-scene-selection-20261031) | `7027450d985d4ecdc459619988c695ab1d655e95` |

The source commits being ported are, respectively, `edf95c6253b53aa45e541e6c0b5e730457dc2e68`,
`d7d640ac48553310db01d0a8af6d56e70ffe24c6`,
`e83a0a72bbdb830757b9da03236809c6952c2553`, and
`0a7b11ac2aacbe19bcfc28115548c270adf85088`.

The integration also resolves interactions that cannot exist in an isolated
feature PR:

- Guided and basic reconstruction sample the compressed model coordinates.
  Optional shader fallback is chosen once for both eyes before crop planning.
- Compression at Model Scale 1 still reconstructs a reduced grid. Brightness
  guidance remains available; RGB guidance keeps its Model Scale <= 0.5 limit.
- Mapped crops include the selected filter's support: three model texels for
  Lanczos3 and one for the other filters. Reconstruction taps stay within the
  evaluated crop.
- Material strengths apply in source coordinates after reconstruction, including
  compression-only grids. Native full-size models keep provider protection.

The original rectangular NR coverage, source-space feather and final foveated
upscaler fill/blend remain in use. Compression does not introduce a CSX
visibility mask or replace the upscaler's fill filter.

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
   [upstream NR guide](https://github.com/alandtse/open-shaders/blob/58396345573534ef89d2b0676a88b89c583ec948/docs/development/neural-rendering.md#enable)
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

| Case | Settings | What to verify |
| --- | --- | --- |
| Baseline | Model Scale 1; compression 0 | Existing NR output and native material protection still work. Disabling NR restores ordinary upscaling. |
| Uniform reduction | Model Scale 0.5; compression 0 | Test brightness guidance, RGB Guidance, and guidance off with Bilinear then Lanczos3. Look for stable colour edges, finite output and correct effective mode. |
| Compression only | Model Scale 1; compression 50 | Brightness guidance remains active when selected. With guidance off, test Bilinear and Lanczos3. The centre remains protected and the periphery reconstructs without stale borders. |
| All rendering changes | Model Scale 0.5; compression 50; RGB Guidance on | Change material selection and partial strengths. The original source-resolution categories should control where reconstructed NR gain applies. |
| Filter/crop interaction | Model Scale 0.5; compression 50; guidance off; Lanczos3 | In VR, enable Follow Foveation and vary the existing foveated rectangle. Check crop edges, eye boundaries and head movement for uninitialised strips or cross-eye sampling. |
| Rectangular coverage | Repeat combined case with Follow Foveation on/off | Preserve the existing full-view fill/blend behavior. Compare the upscaler's existing fill settings separately from NR reconstruction settings. |
| Combat gate | Combined rendering case; Pause NR During Combat on | Enter combat: NR suspends for both eyes while ordinary upscaling continues. Leave combat: NR resumes with fresh history after three continuous peaceful, unpaused seconds. |
| Interrupted recovery | Open a menu during the combat recovery delay | The pending peaceful interval is interrupted. Closing the menu requires a fresh continuous recovery interval; a menu during already peaceful operation does not create a combat suspension. |
| Settings and transitions | Save/reload; toggle NR and each added setting | Settings persist, both eyes change together, effective-mode/mapping changes reset history, and no healthy runtime is needlessly recreated by the combat gate. |

Also sample compression 0 and 100 and Model Scale 0.25, 0.5 and 1. If a
configuration is rejected by the bounded-footprint or minimum-savings policy,
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

Pre-publication checks passed on the combined code:

- 25 production-header Catch2 cases, with 1,720,826 assertions.
- 34 DXC Shader Model 6 variants: 32 LowRes combinations covering reconstruction,
  compression, flat/VR and normal/reversed depth, plus two CategoryAlpha variants.
- Translation synchronization/orphan checks, merge ancestry, conflict-marker
  checks and whitespace checks.
- Independent review of cross-feature admission, stereo fallback, crop support,
  material composition, history transitions and preservation of MIT attribution.

The DXC checks are preliminary syntax checks. They omit `COMPUTESHADER` because
of the existing upstream SharedData compatibility limitation and do not replace
the production Windows compiler or GPU execution. The integration workflow
uses Open Shaders' Windows ALL build, full-config and release-parity FXC shader
validation for flat/VR, shader tests, and the C++ test targets. Its actual run
result is authoritative; a source branch or queued job is not a completed build.

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
