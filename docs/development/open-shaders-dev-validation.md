# Open Shaders selective dev sync validation

The [pinned review](open-shaders-dev-sync.md) covers 56 first-parent
entries after v2.15.0 through `af8134814a17073971628f59c132b196939889ce`.
All user decisions are complete. Validation runs in the separate
`codex/pr730-open-shaders-dev-sync` worktree; no deployment, game launch,
merge or push is part of this build record.

The subsequent [adversarial review](open-shaders-dev-adversarial-review.md)
records two separate corrections and a passing universal DevBench-enabled
build with all 160 registered tests. Its exact producer is separate from
the historical runs below.

## Initial full build

Command, from the sync worktree:

```powershell
pwsh ./tools/validate-local.ps1 -OutputDirectory ../../analysis/open-shaders-dev-review-20260926/final-validation-44a3b0adc
```

-   Source: `44a3b0adc897b48ac76cb4cdecd6c78bdd201dbe`, clean.
-   All four submodule checkouts matched their pins and were clean.
-   Configuration: universal `ALL`, Release, with controller and shader
    tests enabled; MSVC 14.51.36231 and CMake 4.4.1.
-   Configuration passed with a FidelityFX CMP0116 deprecation warning.
-   DLL compilation failed. No DLL Build ID or passing test result was
    produced; the runner stopped before compiling/running test groups.
-   Evidence: the directory above contains the stage logs, initial
    provenance and `summary.json`; total elapsed time was 1058.70 seconds.

Two distinct compiler errors were found in the deferred Adaptive Balance
ports. The central feature-buffer assertion still expected 48 bytes after
the atmosphere extension increased the buffer to 80. The weather-color
port also declared a `GetDiagnostics()` override, but `Feature` has no such
virtual method and no caller used that extra method.

The integration correction changes the assertion to the existing 80-byte
CPU/HLSL contract and removes the unused diagnostics method. The registered
`communityshaders.menu` status continues to expose weather-hook availability
through `adaptiveBalanceWeatherColorsAvailable`; documentation now describes
that actual API. Preset fingerprints are refreshed at contract revision 5.
The subsequent successful rebuild and final validation are recorded below.

## DLL rebuild and fixture corrections

The second full run used clean source
`12fe5a0f682c8e8f7fc091f88d647ed0efc89c3c` and evidence directory
`build/analysis/open-shaders-dev-review-20260926/final-validation-12fe5a0f6`
under the primary repository. The universal DLL built successfully with
Build ID `0ed83e928fd0fed081c781fa237ad03e9b507ca6d49893664997f3243d4e2411`
and DLL SHA-256
`f56201518f94c4d9b61161db6ffb930785b950706ba124b66b51e5226b136b9b`.
The runner then failed while compiling tests because the camera
reprojection fixture inherited Windows' `max` macro through DirectX
headers. Defining `NOMINMAX` at the test entry point fixes that fixture's
compile environment without changing production code. The complete
attempt took 491.21 seconds; its `summary.json` and logs remain preserved.

A targeted run of the already-built fixtures passed `AdaptiveBalanceToggle`,
`FovSettings` and `AdaptiveBalanceColorShader`. `FoveatedBlendCurveShader`
initially failed because its warnings-as-errors compilation encountered
X4000 diagnostics in the existing periphery-TAA cooperative cache loads.
Compiling the pre-port shader from `c047c6dde^` reproduced the same warnings.
The source fills all cache slots, synchronizes the group, and returns from
fast paths before accessing the conditionally assigned cache base.

The fixture now accepts only the exact X4000 messages naming
`LoadCachedDepthClamped` or `LoadCachedCurrentColorClamped`, only while
compiling the production periphery-TAA shader. It continues to print those
warnings and rejects other diagnostics. All other shader compilations
retain warnings-as-errors. No production shader was changed to handle this
fixture warning. Rebuilding and rerunning `CameraReprojection` and
`FoveatedBlendCurveShader` passed both tests; this targeted result does not
replace the complete validation run.

Additional local evidence in the review directory:

-   `targeted-12fe5a0f6.log` and `.xml`: the initial four-fixture run.
-   `pr778-baseline-periphery-fxc.log` and `.dxbc`: baseline warning proof.
-   `fixture-corrections-build.log`: the corrected fixture build.
-   `fixture-corrections-tests.log` and `.xml`: the two passing fixtures.

## Complete test run and long-path correction

The third full run used clean source
`0aad183e4b9438a70624e175ddf560f20e99b4e9` and evidence directory
`build/analysis/open-shaders-dev-review-20260926/final-validation-0aad183e4`.
The universal DLL built with Build ID
`8f6073a23f8d6376a7a696964cad1399c589e041ae5dd4653e89289f73834bdd`
and DLL SHA-256
`f7a0b97267f8067ff223ed86c8e34d36002eafd99140a76f6aab71c7917f061d`.
All 160 registered CTest tests ran and passed, with no skipped, disabled,
missing or unbuilt tests. CTest took 83.54 seconds, including 69.97 seconds
for `ShaderTests`. The overall attempt took 421.52 seconds.

The subsequent standalone preset regression failed because its nested
worktree temporary path exceeded Windows' legacy `MAX_PATH` limit.
The generator's native path-identity helper now passes an absolute
extended-length drive or UNC path to `CreateFileW`. Handle-based identity
resolution, alias detection and returned path normalization are unchanged.
This follows the documented [CreateFileW path contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

The preset regression now explicitly generates and checks a publication
whose report path exceeds 260 characters, verifying that `-Check` leaves
its content unchanged. The focused regression passed; evidence is
`preset-long-path-tests.log` in the review directory. The third run stopped
before preset and DLL provenance verification; the final run below
completes those checks.

## Final validation: passed

Command, from the sync worktree:

```powershell
pwsh ./tools/validate-local.ps1 -OutputDirectory ../../analysis/open-shaders-dev-review-20260926/final-validation-aaca8a2e6
```

-   Compiled source: `aaca8a2e65d71380466827c6430715afad65d598`, clean at
    both the initial and final snapshots, with all submodules matching
    their pins and clean. Later documentation folding preserves this
    exact producer identity; it does not claim a rebuilt DLL.
-   Universal SE/AE/VR `ALL` Release DLL: passed. DevBench bridge and Tracy
    were off. MSVC compiler 19.51.36252.0, toolset 14.51.36231, Windows SDK
    10.0.28000.0, CMake/CTest 4.4.1.
-   Build ID:
    `35ee189815ae1dcb669636bab35d47c44ff570dee332c94e3eb1c9f39336ce88`.
-   DLL SHA-256:
    `e7479d257b7a492db5891058ce774e77458a04e7fba461c26a764065fbee6d24`;
    size 23,526,400 bytes. The physical build artifact matched its manifest.
-   Controller/shader test builds: passed. All 160 registered CTest tests
    executed and passed, with zero failed, skipped, disabled, missing or
    unbuilt tests. This includes the Adaptive Balance, FOV settings,
    FOV curve, camera reprojection and typed per-eye depth fixtures.
-   Standalone preset regression, generated preset `-Check`, diff check,
    manifest verification and unchanged final source snapshot: passed.
-   Total runner time: 855.72 seconds. Full stage timings, commands,
    inventory, JUnit results, logs and manifest are preserved in the
    evidence directory above. Existing FidelityFX CMP0116 and documented
    periphery-cache diagnostic limitations remain visible in the record.

## Hair and True PBR shader matrix: passed

The #758 gate was additionally checked in 64 production `Lighting.hlsl`
variants: flat/VR, Hair Specular off/on, ordinary/True PBR hair,
back-lighting off/on, deferred off/on, and vertex/pixel stages. The matrix
uses the repository's corresponding flat/VR common feature defines, plus
skinned hair and vertex colors. Shader staging passed all 186 HLSL unit
assertions. Hash checks confirmed that the three ported staged shaders
matched the clean validated source above.

Windows SDK 10.0.26100.0 FXC with optimization level 3 compiled all 64
variants: zero warnings, zero suppressed warnings, zero errors. The tool's
initial compiler discovery failed because FXC was absent from PATH; the
successful command supplied its installed absolute path explicitly.

Local evidence under `build/analysis/open-shaders-dev-review-20260926`:

-   `create-hair-matrix.py` and `hair-matrix.json`: exact matrix generation.
-   `prepare-hair-validation.log`: maintained `prepare_shaders` target.
-   `hair-matrix.log`: initial compiler-discovery failure.
-   `hair-matrix-compile.log`, `hair-matrix-timing.json`,
    `hair-matrix-receipt.json` and `hair-matrix-dxbc/`: successful compiler
    diagnostics, per-variant timings, source/config hashes and bytecode.

Reproduction from the sync worktree after `prepare_shaders`:

```powershell
& C:/Users/quartus/AppData/Roaming/Python/Python311/Scripts/hlslkit-compile.exe --fxc 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe' --shader-dir build/ALL/aio/Shaders --output-dir ../../analysis/open-shaders-dev-review-20260926/hair-matrix-dxbc --config ../../analysis/open-shaders-dev-review-20260926/hair-matrix.json --jobs 4 --max-warnings 0 --suppress-warnings X1519 --strip-debug-defines --optimization-level 3 --timing-report ../../analysis/open-shaders-dev-review-20260926/hair-matrix-timing.json
```

## Runtime limits

There is no SE/AE/VR in-game result or physical-HMD qualification result
in this record. In particular, compiled tests cannot establish working sun
glare, absence of live weather-hook regressions, or the quality and stereo
stability of the optional FOV curve. The render-scale-affecting ports still
require `csx-render-scale-pr-v1` qualification and comparable runtime
evidence before those outcomes can be reported.
