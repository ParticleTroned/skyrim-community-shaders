# Prebuilt SE Shader Cache

This is the maintainer runbook for the distributable shader cache on the
`cs-1.7-PL-SE` branch. Use `tools/build-shader-cache.py`; do not rename or merge
cache blobs by hand.

## Scope

The archive supplies optimized DXBC for engine-managed SE pixel, vertex, and
compute permutations. It contains:

```text
ShaderCache/
|-- Info.ini
|-- Manifest.json
`-- <shader directories and .pso/.vso/.cso blobs>
ShaderCache-HorizonFix/
|-- Info.ini
|-- Manifest.json
`-- <shader directories and .pso/.vso/.cso blobs>
fomod/
|-- info.xml
`-- ModuleConfig.xml
```

The release AIO places its normal install tree under `Core/` and adds both
cache profiles plus the same FOMOD at archive root. The installer always
installs `Core`, then asks whether to install a prebuilt cache. When selected,
it asks whether Horizon Fix is active.

The cache is tied to the plugin version, feature versions and enabled states,
shader and recursive-include content, compile flags, custom defines, and the SE
permutation inventory. It is not a GPU-driver cache and is not vendor-specific.

The shipped profile merges `package/Shaders` with the feature shader trees from
the AIO-Release package and omits `Tests`. Hidden AIO features are discovered
from their C++ `IsHiddenFromUserView()` contracts and excluded exactly as they
are from AIO-Release; this currently excludes Exponential Height Fog and Skin.
Wetness Effects remains included and enabled. The profile also enables
`UNIFIED_WATER` globally and `WETTERNESS` for lighting and water.

Every build produces two complete, independently validated cache profiles. The
standard `ShaderCache` records `HorizonFix` disabled and compiles Water without
`HORIZON_FIX`. `ShaderCache-HorizonFix` records it enabled and compiles Water
with the shader define derived from the feature's C++ contract. Validation
requires an identical permutation inventory and rejects any changed blob outside
the Water shader directory. Feature-specific shaders compiled through
independent helpers are not covered unless they have a declared deterministic
inventory.

## Contract files

| Concern                             | Source of truth                                                                     |
| ----------------------------------- | ----------------------------------------------------------------------------------- |
| Builder, validation, and packaging  | `tools/build-shader-cache.py`                                                       |
| Python and hlslkit pin              | `tools/shader-cache-requirements.txt`                                               |
| SE permutation inventory            | `.github/configs/shader-validation.yaml`                                            |
| Runtime digest and manifest         | `src/Utils/ContentHash.h`, `src/Utils/ShaderCacheManifest.h`, `src/ShaderCache.cpp` |
| CSX/plugin version                  | `CSX_VERSION` in `cmake/CSXVersion.cmake`                                           |
| Generated core compatibility marker | `CMakeLists.txt` and `cmake/CSXVersion.marker.in`                                   |
| Feature versions                    | `features/*/Shaders/Features/*.ini`                                                 |
| Standalone CI                       | `.github/workflows/shader-cache.yaml`                                               |
| Release integration                 | `.github/workflows/release-build.yaml`                                              |

The Python and C++ digest implementations form one compatibility contract:
CRLF-normalized source and recursively resolved includes are hashed, then
combined with the compile-state digest. A missing or malformed manifest safely
falls back to the older timestamp validation path.

## Prerequisites

Build on Windows with Git, 64-bit Python 3.12, a Windows SDK containing
`fxc.exe`, and CMake when producing the `.7z` archive. Keep the virtual
environment outside the repository:

```powershell
$cacheVenv = Join-Path $env:LOCALAPPDATA "CommunityShaders\shader-cache-venv"
py -3.12 -m venv $cacheVenv
$cachePython = Join-Path $cacheVenv "Scripts\python.exe"
& $cachePython -m pip install --upgrade pip
& $cachePython -m pip install -r tools/shader-cache-requirements.txt
```

Always use the pinned requirements. The hlslkit manifest writer must remain
byte-compatible with the runtime.

## Release build

Build from the exact clean commit or tag that supplies the DLL and shaders:

```powershell
if (git status --porcelain) {
    throw "Release shader caches must be built from a clean working tree."
}

$releaseLabel = git tag --points-at HEAD |
    Where-Object { $_ -like "SE-RC*" } |
    Select-Object -First 1
if (-not $releaseLabel) {
    throw "The release commit must have an SE-RC tag."
}
& $cachePython tools/build-shader-cache.py `
    --package `
    --package-label $releaseLabel
if ($LASTEXITCODE -ne 0) {
    throw "Shader-cache build failed."
}
```

The builder derives the runtime `Info.ini` identity from the single
`CSX_VERSION` declaration in `cmake/CSXVersion.cmake`. The DLL startup log,
saved settings, runtime-generated cache metadata, prebuilt cache metadata,
Windows product metadata, generated compatibility marker, and FOMOD gate all
use that same CSX identity. Use `--plugin-version` only when intentionally
testing another `CSX <major>.<minor>-SE` build. The release tag belongs in
`--package-label`, not `Info.ini`.

The builder stages a merged shader tree, applies the shipped profile, validates
any `captured_shader_variants` guard, compiles the standard and Horizon Fix SE
inventories, remaps ImageSpace output directories, writes each profile's digest
manifest and feature metadata, checks every blob for a DXBC signature, verifies
that only Water bytecode differs between profiles, creates FOMOD metadata,
validates the archive, and only then publishes output.

The captured validation inventory describes one clean runtime profile. The
release builder supplements it with the known SE-only permutations in
`CROSS_MODLIST_SHADER_VARIANTS`; this keeps the distributed cache useful across
different modlists without mixing VR-only descriptors into the SE package.
Runtime requests that are absent even from this union remain safe: matching
entries load from disk and only the unseen permutation compiles from source.

Expected output:

```text
dist/shader-cache/
|-- SE/ShaderCache/...
|-- SE/ShaderCache-HorizonFix/...
`-- ShaderCache-SE-<release-label>.7z
```

After building the matching AIO-Release tree, assemble the final archive
without recompiling shaders:

```powershell
pwsh ./tools/cmake.ps1 --build --preset AIO-Release
& $cachePython tools/build-shader-cache.py `
    --assemble-aio dist/shader-cache/SE `
    --aio-root build/AIO-Release/aio `
    --out dist/shader-cache `
    --package-label $releaseLabel
```

Pass the directory containing `ShaderCache/` and `ShaderCache-HorizonFix/` to
`--assemble-aio`. `--aio-root` must be the extracted or built AIO root containing
`SKSE/Plugins/CommunityShaders.dll` and its generated compatibility marker.
Both roots are validated against the version derived from
`cmake/CSXVersion.cmake`. The combined output is
`CSX_AIO-<release-label>.7z`.

Publication refuses arbitrary directories, links, and malformed existing
outputs. Candidate files are copied through destination-owned staging so final
ACLs follow the output root. A failed replacement preserves the prior cache.

Useful overrides:

```powershell
& $cachePython tools/build-shader-cache.py `
    --package `
    --package-label $releaseLabel `
    --fxc "C:\Program Files (x86)\Windows Kits\10\bin\<sdk>\x64\fxc.exe" `
    --jobs 4
```

## Refreshing the SE inventory

Use a clean SE profile, remove the installed prebuilt cache, clear the runtime
disk cache, enable Debug or Trace logging, launch the game, and wait until
shader compilation finishes before exiting. Then use the wrapper:

```powershell
.\.github\configs\generate-shader-configs.ps1 `
    -LogFile ".tmp\CommunityShaders-clean-trace.log" `
    -OutputDir ".\.github\configs" `
    -OutputName "shader-validation.yaml" `
    -Force
```

Do not call `hlslkit-generate` directly. The wrapper normalizes padded logger
thread IDs in a temporary file, verifies that generated entries exactly equal
captured source compilations, records `captured_shader_variants`, and replaces
the inventory only after validation succeeds.

## Runtime behavior

Matching entries load from disk and are reported as cache loading rather than
source compilation. Newly compiled blobs update `Manifest.json` through the
same save path. Manifest state is flushed, pruned, discarded, or reloaded when
the active cache is partially invalidated, cleared, backed up, or restored.
Generation guards prevent a compile started before a cache transition from
writing into the new cache generation.

Smart Clear captures active shader permutations over bounded frames and clears
only those entries when safe. It excludes queued or in-flight work, forgets
only completed bookkeeping for evicted permutations, and falls back to a full
clear when active capture is unavailable or the setting is disabled. Features
may additionally clear their own scoped cache state through
`ClearShaderCacheScoped`.

Local compilation remains expected when metadata, sources, enabled features,
custom defines, partial-precision/flow-control flags, or the requested
permutation do not match the shipped profile. Never force-load mismatched
bytecode.

## Validation and release

Successful builder completion verifies:

-   both profiles have the same non-empty permutation inventory and every blob
    begins with `DXBC`;
-   every blob has one lowercase 128-bit manifest digest and no entry is
    orphaned;
-   both `Info.ini` files contain the expected AIO feature set and differ only
    in the `HorizonFix/Enabled` state;
-   only Water blobs differ between the standard and Horizon Fix profiles;
-   the archive contains both cache roots and both FOMOD files.

Inspect an archive with:

```powershell
$cacheArchive = "dist/shader-cache/ShaderCache-SE-$releaseLabel.7z"
cmake -E tar tf $cacheArchive
Get-FileHash $cacheArchive -Algorithm SHA256
```

The standalone workflow uploads one fixed intermediate artifact,
`ShaderCache-SE`. The release workflow requires that job to succeed, combines
its two profiles with the matching AIO artifact, and attaches the integrated
AIO. The standalone cache and unintegrated AIO archives are removed from the
release staging directory after the combined archive validates.

```powershell
gh workflow run shader-cache.yaml `
    --ref <branch-containing-the-workflow> `
    -f target_ref=<commit-or-tag>
```

Install the archive with its included FOMOD. The first page asks whether to
install the prebuilt shader cache. Choosing no cache still installs the complete
core AIO and lets the game compile all required permutations locally.

### Mod Organizer 2

No MO2 plugin setting or virtual-file dependency check is required. The FOMOD
deliberately leaves the cache choice to the user so MO2 cannot reject a valid
installation or silently infer the wrong active mod state.

When the cache is selected, the second page defaults to **With Horizon Fix**.
Choose **Without Horizon Fix** manually when Horizon Fix is not active. If
Horizon Fix is enabled or disabled later, reinstall the AIO FOMOD and select the
matching cache profile. The warning appears in both profile descriptions. A
wrong choice is safe but causes the mismatched cache to be rejected and shaders
to compile locally; the runtime never force-loads entries whose feature metadata
does not match.

Manually setting the `ShaderCache` folder itself as the data directory bypasses
the installer gate, flattens the layout, and is invalid.

## Change checklist

Rebuild the cache after shipped shader/include changes, feature metadata or
profile changes, inventory changes, plugin version changes, cache key or
descriptor changes, compile-state changes, ImageSpace mapping changes, or a
digest/hlslkit contract change. For shader changes, run the feature-version
audit and update the owning canonical feature version when required.

Before changing the hlslkit pin, compare source normalization, include parsing
and ordering, XXH3-128 representation, hash combination, global compile-state
text, manifest keys, schema version, and ImageSpace source mapping against the
C++ runtime. Coordinate incompatible changes in both languages.

Minimum static validation when a full shader build is unavailable:

```powershell
& $cachePython -c "import ast, pathlib; ast.parse(pathlib.Path('tools/build-shader-cache.py').read_text(encoding='utf-8')); print('Python AST OK')"
& $cachePython tools/build-shader-cache.py --help
& $cachePython -c "import pathlib, yaml; [yaml.safe_load(pathlib.Path(p).read_text(encoding='utf-8')) for p in ('.github/workflows/shader-cache.yaml', '.github/workflows/release-build.yaml')]; print('Workflow YAML OK')"
rg -n "D3DWriteBlobToFile" src
git diff --check HEAD
```

There should be exactly one runtime `D3DWriteBlobToFile` call, inside the
manifest-aware save helper. Report explicitly when C++ compilation, full HLSL
generation, or runtime smoke testing was not performed.
