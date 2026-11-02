# main-SE native validation, 2026-11-02

Validated `main-SE` at `39ca16cda0e3108500c7d93e8ae7794236b94961`
with the corrections described below. No changes were imported from
`main-VR`. Local evidence is retained under
`build/main-se-head-validation-20261102-39ca16cda/`.

## Corrections found by validation

-   The optional DevBench build called a VR-only upscaling accessor. It now
    uses the existing SE/AE accessor, preserving RenderDoc and DLSS
    availability handling.
-   MSVC rejected shadowed names in the neural startup and upscaling depth
    fixtures, and a signed slot constant in the Reverse Z context fixture.
    The fixture names and constant type were corrected without weakening
    warnings or assertions.
-   The preset export fixture narrowed a Unicode Windows path before
    appending temporary suffixes. It now appends to the native filesystem
    path, retaining the existing atomic-write failure and recovery checks.
-   Automatic hidden-feature exclusion omitted Reverse Z even though it
    is core. CMake packaging and the shader-cache builder now retain
    packages with the existing `CORE` marker. Hidden non-core features
    remain excluded, and explicit CMake exclusions still take precedence.
    Regression coverage compares the actual CMake selection with the cache
    selector and derives compiler feature states from staged metadata.

The initial complete cache compile produced 4,182 records, but the final
feature-state check rejected that package because Reverse Z was absent.
That cache remains in the evidence directory; it is not the corrected
deliverable. Changing metadata alone would not repair its compiler
macros or feature ABI identities.

## Native build and tests

Used Visual Studio 2026 / MSVC 19.51.36252, Windows SDK FXC
10.0.28000.0, and the pinned shader-cache Python environment. Configuration
enabled SE and AE, disabled VR and automatic deployment, and enabled
`BUILD_TESTING`, `BUILD_HOST_TESTS` and `BUILD_SHADER_TESTS`.

From a VS amd64 developer shell with `VCPKG_ROOT=C:/src/vcpkg`:

```powershell
$validation = 'build/main-se-head-validation-20261102-39ca16cda'
$shaderPython = 'C:/Users/quartus/AppData/Local/CommunityShaders/shader-cache-venv-682751d1/Scripts/python.exe'
pwsh ./tools/cmake.ps1 --preset AIO-Release -B "$validation/native" -D BUILD_TESTING=ON -D BUILD_HOST_TESTS=ON -D BUILD_SHADER_TESTS=ON -D CSX_DEVBENCH_BRIDGE=OFF -D "Python3_EXECUTABLE:FILEPATH=$shaderPython"
pwsh ./tools/cmake.ps1 --build "$validation/native" --config Release --parallel 4 -- /nologo /v:minimal
ctest --test-dir "$validation/native" -C Release -E '^ShaderTests$' --parallel 4 --output-on-failure
ctest --test-dir "$validation/native" -C Release -R '^ShaderTests$' --output-on-failure --verbose
```

-   Normal Release build: passed. The final build log is
    `native-core-packaging.log`; the final DLL SHA-256 is
    `9f4abff291d16703748a68afa539383a0e36de5e3b6313c7a345ee257c17a7f2`.
-   Optional `CSX_DEVBENCH_BRIDGE=ON` Release build: passed after the
    accessor correction (`native-devbench-fixed.log`). Its DLL and manifest
    are preserved separately under `devbench-dll/`.
-   Host suite: 128/128 CTest entries passed in 26.97 seconds
    (`host-tests-core-packaging.log` and XML).
-   Native `ShaderTests`: 270,902 assertions in nine test cases passed;
    144 HLSL test functions were discovered. FXC and D3D11 WARP were used
    (`shader-tests.log` and XML, 155.43 seconds).
-   Focused package tests: 31 passed. SE cache tests: 13 passed, one skipped
    because Windows symlink creation requires unavailable privileges.
    The staged Reverse Z regression failed before the packaging correction
    and passed after it (`core-packaging-red.log`,
    `core-packaging-tests.log`, `core-inventory-tests.log`).
-   Fresh CMake install to `aio-corrected/`: passed. Reverse Z metadata is
    present; Skin and Exponential Height Fog metadata are absent.

The existing D9025 test-target warnings override `/DNDEBUG` with
`/UNDEBUG` deliberately so Release tests retain assertions. No compiler
source warnings were suppressed to obtain these results.

Formatting checks passed for the changed C++ lines, complete neural startup
fixture, new report and preset JSON; whitespace and line-ending hooks also
passed. Whole-file Gersemi and Prettier checks changed unrelated existing
formatting in `CMakeLists.txt` and the cache runbook. Those unrelated changes
were reverted, and whole-file formatting was skipped in the remaining hook
run (`pre-commit-initial.log`, `pre-commit-scoped.log`).

## Corrected shader cache

The complete corrected run compiled 4,182 variants in 41 minutes 3 seconds
with zero new warnings, zero suppressed warnings and zero errors. All
4,182 DXBC blobs and manifest entries passed validation. The managed pack
contains 4,182 unique optimized records and 45 feature sections.

Reverse Z and Wetterness are enabled in its metadata; Wetness Effects and
Horizon Fix are disabled. The global shader ABI matches the final DLL.
The published cache is under `shader-cache-reverse-z/SE/ShaderCache/`;
the standalone archive is
`shader-cache-reverse-z/ShaderCache-SE-main-SE-native-validated.7z`.

```powershell
$fxc = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.28000.0/x64/fxc.exe'
& $shaderPython -u tools/build-shader-cache.py --runtime SE --out "$validation/shader-cache-reverse-z" --jobs 12 --fxc $fxc --package --package-label main-SE-native-validated --no-include-horizon-fix
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src /I "$validation/native/vcpkg_installed/x64-windows-static-md/include" "$validation/runtime-pack-current/main.cpp" src/Utils/ShaderCachePack.cpp src/Utils/CryptoHash.cpp "/Fo$validation/runtime-pack-current/" "/Fe$validation/runtime-pack-current/runtime-pack-smoke.exe" /link bcrypt.lib
& $shaderPython "$validation/runtime-pack-current/verify_cache.py" "$validation/shader-cache-reverse-z/SE/ShaderCache"
```

The native smoke executable was compiled from the current production C++
pack reader and hashing implementation with MSVC `/W4 /WX`. Against an
isolated cache copy, it admitted the native manifest and retrieved every
record through both exact and compatible lookups: 4,182 hits each, zero
corrupt-tail bytes, and an
empty developer lane. Hashes of all six cache files remained unchanged in
both the published tree and the isolated copy.

Evidence: `cache-build-reverse-z.log`, `compiled-inventory-reverse-z.yaml`,
`runtime-pack-current-build.log`, `runtime-pack-current.log` and
`runtime-pack-current/result.json`.

The final AIO assembly passed and produced
`package/CSX_AIO-main-SE-native-validated.7z` (91,530,122 bytes). It combines
the corrected `aio-corrected/` install tree with the validated cache. Its
SHA-256 is
`9ba7a78a953e77cd63ac800a01bc7cc644cf36ef65164af9cda87127e7766c50`.

```powershell
& $shaderPython -u tools/build-shader-cache.py --assemble-aio "$validation/shader-cache-reverse-z/SE" --aio-root "$validation/aio-corrected" --out "$validation/package" --package-label main-SE-native-validated --no-include-horizon-fix
```

Assembly evidence is in `aio-assembly.log`. Build manifests identify the
pre-commit source revision plus its dirty-source digest; the binaries were
built from these corrections before the local commit.

## Limits

No game was launched and no installed mod or live shader cache was
changed. These results do not establish in-game SE/AE behavior, GPU
performance, or real-session cache hit rates. Native pack-reader checks
and offline shader tests are distinct from an in-game smoke test.
