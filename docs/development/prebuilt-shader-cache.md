# Prebuilt SE Shader Cache

This is the maintainer runbook for the distributable SE/AE shader cache. Use
`tools/build-shader-cache.py` from the same checkout as the release core.

## Managed layout

The standalone archive contains one `ShaderCache/` directory with exactly six
files:

| File | Purpose |
| --- | --- |
| `Optimized.A.csxpack` | Initial optimized shader records |
| `Optimized.B.csxpack` | Optimized fallback generation |
| `Developer.A.csxpack` | Initial developer lane, empty in release archives |
| `Developer.B.csxpack` | Developer fallback generation |
| `Info.ini` | Plugin/feature provenance and feature shader ABI state |
| `PackManifest.json` | Pack-set identity, installation baseline, lane and compatibility metadata |

The release AIO places its complete install tree under `Core/`, the six cache
files under `CachePayload/`, and installer metadata under `fomod/`. Its one
installer page offers **Install the prebuilt shader cache** or **Do not install
a prebuilt shader cache**. Both choices install all core files, including the
SE HDR and frame-generation payloads. Selecting the cache maps `CachePayload/`
directly to `Data/ShaderCache/`.

Standard and Horizon Fix water records share the optimized pack. The runtime
selects applicable records using the scoped compatibility registry; the
installer does not ask which Horizon Fix state is active. Switching that
companion plugin does not require reinstalling a different cache profile.

The cache contains engine-managed pixel, vertex and compute DXBC. It is not a
GPU-driver cache and is not vendor-specific. Helpers with independent shader
compilation paths are covered only when they have a declared deterministic
inventory.

## Source contracts

| Concern | Source of truth |
| --- | --- |
| Builder, pack validation and AIO assembly | `tools/build-shader-cache.py` |
| Exact compiler macro handling | `tools/shader_cache_compile.py`, `tools/shader_cache_manifest.py` |
| Structural DXBC validation | `tools/shader_bytecode.py` |
| Python dependencies and hlslkit revision | `tools/shader-cache-requirements.txt` |
| SE captured inventory | `.github/configs/shader-validation.yaml` |
| Global shader ABI | `config/shader-cache-abi.json`, `tools/build_provenance.py` |
| Scoped external compatibility | `config/shader-compatibility-variants.json` |
| Runtime content digest and cache | `src/Utils/ContentHash.h`, `src/Utils/ShaderCacheManifest.h`, `src/Utils/ShaderCachePack.h`, `src/ShaderCache.cpp` |
| Plugin release identity | `cmake/CSXVersion.cmake` |
| Feature provenance | `features/*/Shaders/Features/*.ini` |
| Explicit feature shader ABI | `GetShaderCacheAbiVersion()` in feature headers |
| Standalone/release CI | `.github/workflows/shader-cache.yaml`, `.github/workflows/release-build.yaml` |

Release and feature versions are provenance. Entry validity also includes the
source/include closure, resolved macro names and values, explicit shader ABI,
compile state and applicable external compatibility requirements. A matching
release label alone cannot make different shader inputs compatible.

The builder stages the package shaders and included AIO feature shaders. Hidden
features are derived from `IsHiddenFromUserView()` declarations, matching AIO
packaging. Automatic exclusion applies only to non-core features: packages with
a `CORE` marker, including Reverse Z, remain installed and contribute their
feature state, shader macros and ABI to the cache. Explicit CMake package
exclusions still apply. The shipped profile strips `WETNESS_EFFECTS` from every
permutation and records Wetness Effects disabled, matching the runtime's
forced-off state.
This also excludes its feature ABI from each cache record's compile identity.
Assembly rejects older caches that still mark Wetness Effects enabled;
changing only `Info.ini` cannot repair their compiled shader identities.
`VANILLA_FRESNEL` and `HDR_OUTPUT` permutations remain in the inventory.
The shipped profile enables `UNIFIED_WATER` globally,
`WETTERNESS` for Lighting, Water and RunGrass, and `PBR_GRASS=1` plus
`GRASS_OPTIMIZATIONS` for RunGrass.

Reverse Z compiler inputs come from the selected cache's feature metadata.
Captured `REVERSE_Z` macros are first removed, then enabled only when the
selected `ReverseZ` state is enabled. Utility uses `REVERSE_Z=1`; other shader
classes use the runtime's empty-valued feature macro. The manifest records
the resulting exact macro values and feature state. This preserves standard
depth cache generation when Reverse Z is disabled.

The captured inventory remains unchanged. The builder adds the separately
observed SE grass permutations and the character-category output companions
whose descriptor bits are read from `ShaderCache::GetCharacterCategoryFlag()`.
Missing permutations continue to compile locally when requested.

## Prerequisites

Full generation requires Windows, 64-bit Python 3.12, a Windows SDK containing
`fxc.exe`, and CMake for `.7z` packaging. Keep the Python environment outside the
repository:

```powershell
$cacheVenv = Join-Path $env:LOCALAPPDATA 'CommunityShaders\shader-cache-venv'
py -3.12 -m venv $cacheVenv
$cachePython = Join-Path $cacheVenv 'Scripts\python.exe'
& $cachePython -m pip install -r tools/shader-cache-requirements.txt
```

Use the pinned hlslkit revision. Its source/include digest implementation is
part of the runtime contract. The manifest writer refuses non-Windows builds
because native include-path ordering must agree with the Windows runtime.
Parsing inventories and testing pack/archive contracts can run on Linux.

## Generate and package

Build from the exact clean commit or tag that supplies the DLL and shaders:

```powershell
if (git status --porcelain) {
    throw 'Release shader caches must be built from a clean working tree.'
}
$releaseLabel = '<release-tag>'
& $cachePython tools/build-shader-cache.py `
    --package `
    --package-label $releaseLabel
if ($LASTEXITCODE -ne 0) { throw 'Shader-cache build failed.' }
```

`Info.ini` uses the `CSX_VERSION` declaration from `cmake/CSXVersion.cmake`.
`--plugin-version` accepts `CSX <major>.<minor>-SE` for intentional matching-build
tests. The archive label is independent and belongs in `--package-label`.
`--runtime SE` is optional; no other runtime inventory is supported.

The build performs these steps before publishing output:

1. Stage the merged SE shader tree and validate captured inventory counts.
2. Apply the shipped feature scopes and add the SE grass/category variants.
3. Resolve every compiler task and reject ambiguous output identities.
4. Compile standard and Horizon Fix variants with FXC, preserving explicitly
   empty macro replacements and nonempty values such as `PBR_GRASS=1`.
5. Remap ImageSpace output using SE technique indices; hash source closures,
   feature ABI state and the exact resolved macros into a temporary schema-2
   entry manifest.
6. Check DXBC headers, chunk bounds, shader stage and program lengths. Require
   equivalent variant inventories and water-only Horizon Fix bytecode changes.
7. Build and validate committed managed-pack records, SHA-256 payload/header
   hashes, scoped compatibility identities and the six-file installation set.
8. Validate the optional archive and publish through destination-owned staging.

Expected output is `dist/shader-cache/SE/ShaderCache/` and, with `--package`,
`dist/shader-cache/ShaderCache-SE-<label>.7z`. Temporary loose blobs and
`Manifest.json` are removed from the published managed cache.

For a deliberately standard-only test cache, pass `--no-include-horizon-fix`.
Default release validation requires both standard and Horizon Fix coverage.
Use the same explicit option when assembling a standard-only test AIO.

Useful compiler overrides:

```powershell
& $cachePython tools/build-shader-cache.py `
    --package --package-label $releaseLabel `
    --fxc 'C:\Program Files (x86)\Windows Kits\10\bin\<sdk>\x64\fxc.exe' `
    --jobs 4
```

## Assemble the SE AIO

The existing release workflow's `--assemble-aio` entry point remains available
and does not require FXC or hlslkit. After building the matching core:

```powershell
pwsh ./tools/cmake.ps1 --build --preset AIO-Release
& $cachePython tools/build-shader-cache.py `
    --assemble-aio dist/shader-cache/SE `
    --aio-root build/AIO-Release/aio `
    --out dist/shader-cache `
    --package-label $releaseLabel
```

`--assemble-aio` points to the directory containing `ShaderCache/`.
`--aio-root` points to the complete core with `SKSE/Plugins/CommunityShaders.dll`
and its generated CSX compatibility marker. Assembly checks feature versions,
feature sets, the source ABI and every pack before copying either input. It
rejects links, partial caches and previously integrated staging trees.

The core is copied as a complete tree. A SHA-256 inventory verifies every core
file after staging and again after extracting the resulting archive. The same
archive check verifies the cache and the install/no-cache FOMOD mappings. This
preserves presentation, HDR and frame-generation assets without selecting or
rewriting their files.

The combined output is `CSX_AIO-<label>.7z`. The release workflow requires the
cache job to succeed, extracts both artifacts, calls this assembler, and only
then replaces the original AIO archive. The separate intermediate cache archive
is removed from release staging after successful assembly.

Publication refuses arbitrary output trees and linked destinations. Candidates
are copied through destination-owned staging. Replacement retains the prior
cache until the validated candidate is installed; a failed replacement restores
that prior cache. An interrupted recovery directory blocks subsequent
publication until it is resolved, preserving its contents for recovery.

## Installation and runtime behavior

Install the integrated AIO using its FOMOD. The default choice installs the
cache; the other choice installs the entire core and lets the game compile all
required permutations locally. No mod-manager file probing is needed.

A standalone cache archive has a normal `ShaderCache/` directory at archive
root and can be installed directly as a Data payload. Do not choose the
`ShaderCache` directory itself as the data root: that would flatten the six
files into the wrong location.

The runtime validates each applicable record before use. Changed sources,
feature shader ABIs, macro values, compilation flags or external compatibility
requirements cause local compilation for the affected requests. Shader input
mismatches must never be bypassed merely to reuse cached bytecode.

Optimized and developer records have separate managed lanes. Runtime writes,
compaction and cache resets can advance generations beyond the installation
baseline recorded in `PackManifest.json`; the shared manifest contract permits
those valid transitions. Malformed records or an incomplete managed layout
are rejected safely. Legacy loose-cache behavior is separate from managed pack
validation.

## Refresh the captured inventory

Capture a clean SE session after clearing its shader cache, with Debug or Trace
logging enabled, and wait for compilation to finish before exiting. Use the
repository wrapper:

```powershell
.\.github\configs\generate-shader-configs.ps1 `
    -LogFile '.tmp\CommunityShaders-clean-trace.log' `
    -OutputDir '.\.github\configs' `
    -OutputName 'shader-validation.yaml' `
    -Force
```

The wrapper normalizes padded logger thread IDs, verifies generated entries
against captured source compilations and records `captured_shader_variants`
before replacing the inventory. Keep release-only additions in the builder's
SE overlay so recapturing a single modlist does not remove them.

## Validation

With the pinned requirements installed and CMake on `PATH`:

```powershell
& $cachePython tests/shader_cache_pack_builder_test.py
& $cachePython tests/shader_cache_packaging_test.py
& $cachePython tests/shader_cache_se_test.py
& $cachePython tools/build-shader-cache.py --help
```

These checks exercise the shared pack-contract corpus, structural DXBC
validation, compatibility coverage, malformed records, publication retries and
rollback, real archive packing/extraction, SE macro/inventory preservation and
the release AIO command line. The AIO fixtures explicitly include HDR and
frame-generation paths and compare their bytes after assembly. They are not
compiled game shaders and do not establish rendering correctness.

The standalone cache workflow runs these tests before compiling the release
inventory. Archive tests require CMake. Inventory/compiler-adapter tests require
the pinned hlslkit revision; unavailable optional tools are reported as skips.
A Windows FXC build and in-game SE/AE smoke test remain necessary to verify the
actual shader bytecode and runtime cache hits.

Before changing the compiler pin or cache format, compare source normalization,
include ordering, XXH3-128 combination, resolved macro encoding, feature ABI
state, ImageSpace mapping and pack identities against the C++ runtime. Rebuild
release caches after changes to any of those inputs or the shipped shader and
compatibility contracts.
