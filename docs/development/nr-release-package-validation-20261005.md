# NR release package validation � 2026-10-05

The `Package-nr` release pipeline passed its local package, UI policy,
provider-admission and artifact identity checks. In-game SE/AE/VR
execution and the GitHub-hosted workflow were not run.

## Resolved adversarial review findings

-   Missing providers could leave a saved NR master preference suppressing
    normal FOV + TAA. A shared effective-enabled predicate now governs NR
    requests, TAA constraints and controls, mask/profile selection,
    blending/isolation eligibility and resource keys. Saved preferences
    remain intact when the provider is absent.
-   Source Streamline metadata could enter the release folder. Release
    installs exclude that source folder and install the exact official SDK
    inventory. The final guard checks hashes and physical contents and
    rejects NR DLLs throughout the installer.
-   Local provider/kernel inputs and skipped SDK verification could weaken
    release packaging. The release policy rejects those inputs and extracts
    the verified SDK archive again on configuration.
-   Duplicated SDK inventories could drift. Packaging and its integration
    test share one inventory; adversarial tests independently exercise
    extra files, renamed/case-varied NR DLLs, changed/missing SDK files,
    private inputs and skipped verification.
-   Discovery errors could propagate through noexcept runtime decisions.
    Optional DLL discovery now logs standard exceptions and returns
    unavailable, with one cached discovery per process.
-   A generic workflow preset selector unnecessarily exposed unsupported
    build-directory combinations. The reusable workflow now has a narrow
    boolean opt-in for `Package-nr`; its existing default is `ALL`.

## Validation

-   Passed: `pwsh ./tools/cmake.ps1 --build --preset Package-nr -- /m:4`.
    Dependencies and the hash-verified SDK archive were reused locally after
    the sandbox blocked initial dependency fetching. Full build logs remain
    under `build/Package-nr/`; user build outputs and caches were preserved.
-   Passed: the eight focused tests below, using Release executables built
    from the reviewed source. `NrReleasePayload` also rejects both private
    payload options and `SKIP_RUNTIME_DOWNLOADS=ON`.

```powershell
ctest --test-dir build/Package-nr -C Release --output-on-failure --no-tests=error -R '^(NeuralRenderingUI|NeuralRenderingControls|NeuralFullResolutionFov|NeuralRenderingRequest|NeuralFeatureSettings|NeuralSettingsKey|NrReleasePayload|StreamlineRuntimePackaging)$'
pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -P tests/neural_rendering_devbench_contract_test.cmake
```

-   Passed: DevBench source/schema contract and scoped pre-commit checks.
    The screenshot schema hook had no applicable files and was skipped.
-   Passed: fresh archive extraction and the release payload guard. All
    eleven installed Streamline files match the verified SDK; NR DLL count
    is zero. The physical producer DLL, AIO DLL and extracted DLL each pass
    `tools/build_provenance.py verify` against their adjacent manifest.
-   Passed: direct `--inspect` provider probes for all five local DLLs,
    using the production loader through `csx_nr_replay`. Each probe checks
    version, required exports and loaded image identity, without GPU
    initialization/evaluation. No developer-mode or hash whitelist is
    required. This demonstrates admission, not successful in-game rendering.
    Replay compilation emitted vendor/deprecation warnings:
    C4065, C4200, C4201, C4996.
-   Not run: game launch/deployment, SE/AE/VR UI/render testing, native NR
    evaluation and the GitHub-hosted manual workflow. No runtime performance
    or visual-fidelity qualification is claimed.

## Preserved compiled identity

The validation binary was compiled before this implementation commit.
Its original source identity remains recorded here and in its manifest.

| Field                              | Value                                                              |
| ---------------------------------- | ------------------------------------------------------------------ |
| Source commit                      | `52f8b9ca2e2cf04051564bf36c3000a2659318dc`                         |
| Source dirty                       | `true`                                                             |
| Source dirty digest                | `11fbc559a6f301a4c97aa16145082e97371d53cbc3a7db574639a28dd4833f98` |
| Build ID                           | `79dd03f878b1189a886ad16fe875742f32bcbdb2c6908ad85b2605416cfb885c` |
| DLL SHA-256                        | `7a52832a830401d76a1e5930aa85ed3ebb1710912d248b313bd2116a51b64d1b` |
| DLL bytes                          | 25352192                                                           |
| Archive                            | `dist/CSX_AIO-2026-10-04T23-55Z.7z`                                |
| Archive SHA-256                    | `ced07efaec14ba8f4c9e631e2179788eaeb4e0d8f14367009fd84f50537b35de` |
| Archive bytes                      | 91370746                                                           |
| Provider-probe Runtime.cpp SHA-256 | `781493be51ac8f4a16c870fdd1cc6e521a28f9fa8bb29786f1dbd0beb4a363bf` |

Evidence remains local in `build/Package-nr/archive-audit/`,
`build/Package-nr/archive-audit-summary.json`,
`build/Package-nr/review-ctest.log`, and
`build/Package-nr-replay/provider-*/review-result/results.json`.

## Local provider admission

Paths are relative to `C:/src/DLSS`. Every file is supplied by the user;
none is present in the release installer.

| Provider                                     | Version | Status | SHA-256                                                            |
| -------------------------------------------- | ------- | ------ | ------------------------------------------------------------------ |
| `all-in-one/nvngx_dlssnr.dll`                | 310.8.0 | ready  | `8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206` |
| `nvngx_dlssnr_310.8.Lecram/nvngx_dlssnr.dll` | 310.8.3 | ready  | `F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC` |
| `nvngx_dlssnr_310.8.SF-v2/nvngx_dlssnr.dll`  | 310.8.2 | ready  | `6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927` |
| `PD/nvngx_dlssnr.dll`                        | 310.8.0 | ready  | `8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206` |
| `streamline/NR/nvngx_dlssnr.dll`             | 310.8.0 | ready  | `CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650` |
