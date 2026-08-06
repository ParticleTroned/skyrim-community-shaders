# Prebuilt Shader Cache Runbook

This is the authoritative maintainer and AI-agent procedure for building,
updating, validating, and shipping CSX' prebuilt shader cache.
Use `tools/build-shader-cache.py`; do not assemble cache blobs or metadata by
hand.

## Purpose and scope

The cache distributes compiled DXBC for CSX' engine-managed
pixel, vertex, and compute shader permutations. It is not a GPU-driver cache,
so it is not tied to a particular GPU vendor. It is tied to all of the
following:

-   the SE or VR runtime;
-   the exact CSX plugin version;
-   installed feature versions and enabled states;
-   the shipped shader source and recursive includes;
-   compiler flags and custom shader defines;
-   the permutation inventory in the matching validation YAML.

The current release profile:

-   merges `package/Shaders` and feature `Shaders` trees;
-   excludes every `Tests` directory;
-   excludes the legacy `Wetness Effects` package and `WETNESS_EFFECTS` define;
-   enables `UNIFIED_WATER` globally;
-   enables `WETTERNESS` for `Lighting.hlsl` and `Water.hlsl`;
-   omits the VR feature metadata from an SE cache;
-   compiles optimized release bytecode, without developer/debug defines.

The builder removes the five debug-profile defines itself. Do not replace that
filter with hlslkit's `--strip-debug-defines`: the pinned implementation also
injects `D3DCOMPILE_AVOID_FLOW_CONTROL`, which does not match the default
runtime compile state.

Generated inventories may include `captured_shader_variants`, written by
`.github/configs/generate-shader-configs.ps1`. The builder verifies that exact
number of entries before compiling so a truncated capture cannot be packaged.

This cache does **not** cover feature-specific shaders compiled through
independent `Util::CompileShader` or direct `D3DCompile*` paths. Those can
still compile on first use. A new shader path may be added only after it has a
deterministic cache key and a complete, declared permutation inventory.

Do not promise users that every possible HLSL compilation is eliminated.
The supported claim is that matching engine-managed permutations can load from
the supplied cache.

## Files that define the contract

| Concern                                         | Source of truth                                     |
| ----------------------------------------------- | --------------------------------------------------- |
| Local build, staging, validation, and packaging | `tools/build-shader-cache.py`                       |
| Python dependencies and pinned hlslkit revision | `tools/shader-cache-requirements.txt`               |
| SE permutation inventory                        | `.github/configs/shader-validation.yaml`            |
| VR permutation inventory                        | `.github/configs/shader-validation-vr.yaml`         |
| Runtime content digest                          | `src/Utils/ContentHash.h` and `src/ShaderCache.cpp` |
| Runtime manifest schema and atomic persistence  | `src/Utils/ShaderCacheManifest.h`                   |
| Plugin versions written to `Info.ini`           | `CMakePresets.json`                                 |
| Feature versions written to `Info.ini`          | `features/*/Shaders/Features/*.ini`                 |
| Standalone/reusable cache CI                    | `.github/workflows/shader-cache.yaml`               |
| Release integration                             | `.github/workflows/release-build.yaml`              |

The manifest algorithm exists in C++ and in pinned hlslkit because it must run
both in the plugin and in Python. Treat it as one cross-language contract.

## Prerequisites

Build on Windows. The manifest's include-path ordering intentionally depends on
Windows path semantics so it matches the runtime exactly.

Use a normal, non-elevated PowerShell. An Administrator shell is unnecessary
and makes manually created output owned by the Administrators group. The
builder copies validated candidates into publication staging paths beneath the
selected output root so release files inherit that root's normal ACL even if an
operator accidentally launches an elevated build.

Install:

-   Git;
-   64-bit Python 3.12;
-   the Windows 10 or 11 SDK containing `fxc.exe`;
-   CMake on `PATH` when creating `.7z` archives.

The builder finds `fxc.exe` on `PATH` or under the normal Windows SDK
directories. A separate 7-Zip installation is not required; packaging uses
`cmake -E tar --format=7zip`.

Check the tools from PowerShell:

```powershell
git --version
py -3.12 --version
cmake --version
Get-Command fxc.exe -ErrorAction SilentlyContinue
```

The final command can return nothing when `fxc.exe` is installed in a Windows
SDK directory but is not on `PATH`; the builder searches those directories too.

## One-time Python setup

Keep the virtual environment outside the repository so it cannot pollute the
working tree:

```powershell
Set-Location <repo>

$cacheVenv = Join-Path $env:LOCALAPPDATA "CommunityShaders\shader-cache-venv"
py -3.12 -m venv $cacheVenv
$cachePython = Join-Path $cacheVenv "Scripts\python.exe"

& $cachePython -m pip install --upgrade pip
& $cachePython -m pip install -r tools/shader-cache-requirements.txt
```

Always install from `tools/shader-cache-requirements.txt`. Do not install an
arbitrary latest hlslkit: its `shader_digest` implementation is part of the
runtime compatibility contract.

Refresh the environment after the requirements file changes:

```powershell
& $cachePython -m pip install --upgrade --force-reinstall -r tools/shader-cache-requirements.txt
```

## Release preflight

Build release caches from the exact clean commit or tag that supplies the DLL
and shader source. The builder reads the working tree, not only committed
files.

```powershell
Set-Location <repo>

$status = git status --porcelain
if ($status) {
    $status
    throw "Release shader caches must be built from a clean working tree."
}

git rev-parse HEAD
Select-String -Path CMakePresets.json -Pattern "CSX_VERSION"
```

Run the feature-version audit before compiling. Shader or include changes
normally require the owning feature's canonical version to change so existing
user caches invalidate correctly:

```powershell
$auditReport = Join-Path $env:TEMP "community-shaders-feature-version-audit.md"
& $cachePython tools/feature_version_audit.py `
    --output $auditReport `
    --fail-on-actionable
$auditExit = $LASTEXITCODE
Get-Content $auditReport
if ($auditExit -ne 0) {
    throw "Resolve the actionable feature-version audit items before building."
}
```

Review suggested bumps before using `--apply-bumps`; that option edits feature
INI files. After any bump, inspect the diff and rerun the audit.

## Build both release caches

Choose a release label for archive filenames. This is separate from the plugin
versions written to `Info.ini`.

```powershell
$releaseLabel = "v1.7.0"

& $cachePython tools/build-shader-cache.py `
    --runtime both `
    --package `
    --package-label $releaseLabel

if ($LASTEXITCODE -ne 0) {
    throw "Shader-cache build failed."
}
```

This compiles HLSL but does not build the C++ plugin. The builder:

1. assembles the release shader tree in an isolated temporary directory;
2. applies the shipped feature profile;
3. compiles the complete SE and VR inventories with the pinned hlslkit;
4. remaps runtime ImageSpace directories;
5. writes `Manifest.json` from source and recursive-include content;
6. writes `Info.ini` with plugin and feature versions;
7. validates metadata, every manifest entry, every blob, and the `DXBC`
   signature;
8. adds FOMOD metadata that preserves the required `ShaderCache` directory in
   Mod Organizer 2 and prepares install-ready archives;
9. publishes output only after every requested runtime has passed the earlier
   stages.

An existing runtime output is replaced only when it has the expected,
non-link cache layout and a readable `[Cache] PluginVersion` ownership marker.
The tool refuses to replace arbitrary directories. When both runtimes are
requested, it validates every runtime and archive destination before replacing
any of them. It also preserves the old runtime directory if publishing its
replacement fails. The output root may live under the repository (the default
is `dist/shader-cache`), but it must not be inside any shader source tree that
the staging pass copies.

Expected output:

```text
dist/shader-cache/
|-- SE/
|   `-- ShaderCache/
|       |-- Info.ini
|       |-- Manifest.json
|       `-- <shader directories and blobs>
|-- VR/
|   `-- ShaderCache/
|       |-- Info.ini
|       |-- Manifest.json
|       `-- <shader directories and blobs>
|-- ShaderCache-SE-v1.7.0.7z
`-- ShaderCache-VR-v1.7.0.7z
```

Use a unique release label if old archives must remain alongside new ones.
Reusing a label intentionally replaces an ordinary archive file of that name;
the tool refuses linked paths and non-file destinations.

## Refreshing a permutation inventory

Capture from the exact runtime/profile being shipped. Disable any installed
prebuilt shader cache, clear the runtime disk cache, select Debug or Trace log
level, start the game, and wait until the shader compilation counter reaches
zero before exiting. Preserve the completed `CommunityShaders.log`, then run:

```powershell
.\.github\configs\generate-shader-configs.ps1 `
    -LogFile ".tmp\CommunityShaders-clean-trace.log" `
    -OutputDir ".\.github\configs" `
    -OutputName "shader-validation-vr.yaml" `
    -Force
```

Use `shader-validation.yaml` for an SE capture. Always use the wrapper rather
than calling `hlslkit-generate` directly. It normalizes padded logger thread
IDs in a temporary copy and refuses to replace the inventory unless the YAML
entry count equals the clean runtime capture count. The runtime UI can show a
slightly larger total because completed tasks include in-session cache hits;
only source compilation records produce distinct distributable variants.

### Build one runtime

```powershell
& $cachePython tools/build-shader-cache.py `
    --runtime SE `
    --package `
    --package-label "v1.7.0"
```

Use `VR` instead of `SE` for a VR-only cache. Do not distribute an SE cache as
VR or combine the two archives.

### Override `fxc.exe` or worker count

```powershell
& $cachePython tools/build-shader-cache.py `
    --runtime both `
    --package `
    --package-label "v1.7.0" `
    --fxc "C:\Program Files (x86)\Windows Kits\10\bin\<sdk-version>\x64\fxc.exe" `
    --jobs 4
```

`--jobs` must be at least 1.

### Override plugin versions

Normally, do not override these values. The builder derives SE from the
`AIO-Release` preset and VR from `ALL`/`ALL-VS2022` in
`CMakePresets.json`.

For one runtime:

```powershell
& $cachePython tools/build-shader-cache.py `
    --runtime SE `
    --plugin-version "CSX 3.15-SE" `
    --package `
    --package-label "v1.7.0"
```

For both runtimes:

```powershell
& $cachePython tools/build-shader-cache.py `
    --runtime both `
    --plugin-version-se "CSX 3.15-SE" `
    --plugin-version-vr "CSX 3.18-VR" `
    --package `
    --package-label "v1.7.0"
```

`--plugin-version` cannot be used with `--runtime both`. Never use the release
tag as the plugin version unless it is literally the plugin's runtime version
label. A mismatch causes the runtime to invalidate the supplied cache.

## Validation and artifact checks

Successful builder completion already proves:

-   at least one compiled blob exists;
-   every `.pso`, `.vso`, and `.cso` starts with `DXBC`;
-   `Manifest.json` uses the supported schema;
-   every blob has exactly one valid 32-character lowercase digest;
-   the manifest contains no entry without a blob;
-   `Info.ini` contains the requested plugin version;
-   the archive was created and is nonempty;
-   the archive contains `ShaderCache/Info.ini`,
    `ShaderCache/Manifest.json`, and both required FOMOD installer files.

Optional operator checks:

```powershell
cmake -E tar tf "dist/shader-cache/ShaderCache-SE-v1.7.0.7z"
cmake -E tar tf "dist/shader-cache/ShaderCache-VR-v1.7.0.7z"

Get-FileHash "dist/shader-cache/ShaderCache-*-v1.7.0.7z" -Algorithm SHA256
```

Inspect the loose metadata and confirm manifest/blob counts:

```powershell
foreach ($runtime in @("SE", "VR")) {
    $cacheRoot = Join-Path "dist/shader-cache" "$runtime\ShaderCache"
    Get-Content (Join-Path $cacheRoot "Info.ini")

    $manifest = Get-Content (Join-Path $cacheRoot "Manifest.json") -Raw |
        ConvertFrom-Json
    $manifestCount = $manifest.entries.PSObject.Properties.Count
    $blobCount = (
        Get-ChildItem $cacheRoot -Recurse -File |
        Where-Object { $_.Extension -in @(".pso", ".vso", ".cso") }
    ).Count

    if ($manifest.schemaVersion -ne 1 -or $manifestCount -ne $blobCount) {
        throw "$runtime manifest validation failed."
    }
}
```

Do not “repair” a failed artifact by deleting manifest entries, copying blobs
between runtimes, renaming descriptors, or changing timestamps. Fix the source
contract and rerun the supported builder.

## Install and ship

Each archive contains a top-level `ShaderCache` directory and FOMOD metadata.

-   For manual installation, it becomes
    `<Skyrim>\Data\ShaderCache`.
-   In Mod Organizer 2, use the included FOMOD installer. Do not select
    `ShaderCache` and choose **Set data directory** in the manual installer;
    that strips the required directory and incorrectly exposes the cache as
    `Data\Info.ini`, `Data\Lighting`, and so on.
-   In a mod-manager package whose root maps to Skyrim's `Data`, place
    `ShaderCache` alongside `Shaders` and `SKSE`.
-   Offer separate, clearly labelled SE and VR files.
-   Ship the cache with the exact DLL, shaders, and feature versions from the
    same ref.

Do not put both SE and VR caches into one install. Do not package the cache from
one commit with shader source or a DLL from another commit.

For a smoke test, use a clean mod-manager profile, move any existing
`ShaderCache` aside so it can be restored, install the matching artifact, and
start the matching runtime. Check the CSX runtime log (`CommunityShaders.log`) for cache
validation/invalidation and unexpected compilation. Test both runtimes before
publishing a two-runtime release.

## CI and release workflow

`Release: Prebuilt Shader Cache` runs on `windows-2025`, executes the builder
and pinned requirements from the selected target ref, and creates fixed GitHub
artifact names:

-   `ShaderCache-SE`
-   `ShaderCache-VR`

The files inside those artifacts retain the ref/tag label in their filenames.

Run it manually in GitHub Actions, or with GitHub CLI:

```powershell
gh workflow run shader-cache.yaml `
    --ref <branch-containing-the-workflow> `
    -f target_ref=<commit-or-tag> `
    -f runtime=both

$runId = gh run list `
    --workflow shader-cache.yaml `
    --limit 1 `
    --json databaseId `
    --jq ".[0].databaseId"
gh run watch $runId
gh run download $runId -n ShaderCache-SE -D dist/downloaded-cache
gh run download $runId -n ShaderCache-VR -D dist/downloaded-cache
```

For normal releases, `.github/workflows/release-build.yaml` calls the reusable
cache workflow for both runtimes. The release job is gated on cache success,
downloads both artifacts into `dist`, and attaches their `.7z` files to the
draft release. No separate manual cache run is required for that path.

## When a cache rebuild is required

| Change                                                                                       | Required action                                                                                   |
| -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Shipped `.hlsl` or `.hlsli` content                                                          | Audit/bump the owning feature version as required; rebuild affected SE and VR caches              |
| Feature `[Info] Version` or shipped enabled profile                                          | Rebuild and verify generated `Info.ini`                                                           |
| SE/VR validation YAML or permutation inventory                                               | Rebuild that runtime; rebuild both if shared assumptions changed                                  |
| Profile constants, excluded packages, or ImageSpace mapping in the builder                   | Rebuild both runtimes                                                                             |
| Plugin version label                                                                         | Update `CMakePresets.json`, then rebuild matching runtime caches                                  |
| Compiler flags, macro ordering, cache filename/key, descriptor mapping, or source resolution | Coordinate runtime and builder changes, then rebuild                                              |
| Digest algorithm or manifest shape                                                           | Update both languages, bump the schema, update the pin, then rebuild everything                   |
| Pinned hlslkit revision                                                                      | Perform the compatibility procedure below; never bump blindly                                     |
| Unrelated C++ or documentation only                                                          | A cache rebuild is not intrinsically required, although release CI still produces fresh artifacts |

## Updating shaders or a feature

1. Change the source.
2. Run `tools/feature_version_audit.py`.
3. Review and make any required version change in the canonical
   `features/<Feature>/Shaders/Features/<ShortName>.ini`.
4. Ensure the SE and VR validation YAMLs still enumerate every intended
   permutation.
5. If the release profile changed, update the profile constants in
   `tools/build-shader-cache.py`.
6. Build and validate both runtime caches.
7. Smoke-test the exact packaged source, plugin, and cache together.

When adding a feature, explicitly decide:

-   whether it is included in the shipped cache profile;
-   whether it applies to SE, VR, or both;
-   which global/file-specific defines it needs;
-   which canonical feature INI supplies its version;
-   which validation-config entries enumerate its permutations;
-   which cache directories partial invalidation must remove when it changes.

A missing or malformed feature version is a build error. Do not weaken that
check to a warning.

## Updating the plugin version

1. Update the appropriate `CSX_VERSION` values in
   `CMakePresets.json`.
2. Confirm the compiled plugin's `Plugin::VERSION_LABEL` will be identical.
3. Rebuild the caches; do not reuse archives whose `Info.ini` has the previous
   label.
4. Use the release tag only as `--package-label`.
5. Inspect both generated `Info.ini` files before publishing.

## Updating hlslkit or the manifest contract

The hlslkit revision is pinned once in
`tools/shader-cache-requirements.txt`; local setup and CI both consume that
file.

Before changing the pin:

1. Inspect the candidate `hlslkit.shader_digest` implementation.
2. Compare CRLF normalization, XXH3-128 byte layout, ordered hash combine,
   include parsing, root-first include resolution, Windows path sorting,
   cycle handling, global compile-state text, manifest keys, and ImageSpace
   source mapping with `src/Utils/ContentHash.h` and `src/ShaderCache.cpp`.
3. Keep `tools/build-shader-cache.py` validation aligned.
4. If compatibility changes, increment the manifest schema in hlslkit, the
   builder, and `src/Utils/ShaderCacheManifest.h`.
5. Update this runbook if prerequisites or commands changed.
6. Perform static checks, then an authorized SE+VR build and runtime smoke
   test.

A digest mismatch is safe because the runtime recompiles, but it makes the
prebuilt cache ineffective. “Safe fallback” is not a successful release
validation.

## Runtime behavior and user expectations

At runtime, `Manifest.json` content digests are preferred over timestamps.
This avoids false invalidation caused by download, extraction, or copy times.
A missing/malformed manifest or a blob without an entry falls back to the older
timestamp/file-watcher path.

The runtime records newly compiled blobs through one save path and updates the
manifest in batches and at lifecycle boundaries. It prunes manifest entries
after partial invalidation and resets or reloads in-memory metadata when active
and rollback cache directories are deleted or swapped. If a replaced blob's
source digest cannot be calculated, the runtime removes any older entry for
that blob so validation safely returns to the timestamp/file-watcher path.
Each compilation also carries one immutable snapshot of its custom defines,
flags, cache path, and digest. Cache-generation guards prevent work started
before a clear, deletion, or directory swap from writing into the new active
disk-cache generation.

Users can still compile local variants when:

-   their plugin or feature metadata does not match;
-   features are enabled/disabled differently from the shipped profile;
-   shader source/includes differ;
-   Developer Mode is active;
-   custom Shader Defines are present;
-   Partial Precision or Avoid Flow Control is enabled;
-   a shader uses an independent feature-specific compilation path;
-   a required prebuilt permutation is absent.

That behavior is intentional. Never force-load a blob whose inputs do not
match.

## Failure recovery

| Failure                                    | Response                                                                                                               |
| ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------- |
| `fxc.exe was not found`                    | Install the Windows SDK or pass the exact x64 `--fxc` path                                                             |
| Python import error                        | Reinstall `tools/shader-cache-requirements.txt` with the same Python executable used to run the builder                |
| Manifest schema mismatch                   | Restore the pinned requirements or coordinate a schema update across Python and C++                                    |
| Missing feature version                    | Add/fix the canonical `[Info] Version`; do not skip it                                                                 |
| Missing manifest entry or unexpected entry | Fix source-name/ImageSpace mapping or permutation output, then rebuild                                                 |
| Non-DXBC blob                              | Treat compilation/output as failed; never distribute it                                                                |
| Refusal to replace output                  | Choose an empty `--out` path or manually inspect the existing directory; do not add a force-delete option              |
| Archive packaging failure                  | Fix CMake/permissions/disk space and rerun; prior published output is not replaced during compilation/validation       |
| Runtime invalidates every entry            | Check runtime, plugin label, feature versions/state, source files, custom defines, flags, and digest-contract parity   |
| Only some shaders compile                  | Check profile differences, a missing permutation, partial invalidation, and independent feature-specific compile paths |

Keep the prior release artifact until the new one has passed both automatic
validation and runtime smoke testing. If a shipped cache causes regressions,
withdraw that cache artifact; users can safely fall back to local compilation.

## AI-agent operating contract

An AI agent maintaining this system must:

1. Read this runbook and inspect the contract files listed above.
2. Check `git status --short` first and preserve unrelated user changes.
3. Treat “no builds” as prohibiting CMake builds, HLSL compilation, plugin
   compilation, and runtime execution. Do only read-only analysis, source/doc
   edits, and static checks in that case.
4. Run the supported builder only when shader compilation is explicitly
   authorized.
5. Never hand-create, rename, merge, or delete cache blobs to make validation
   pass.
6. Never add a “skip compile” or “force replace/delete” path.
7. Keep the runtime digest, pinned hlslkit digest, builder validation, schema,
   ImageSpace mapping, compile-state string, and workflow in sync.
8. Keep dependency pins in `tools/shader-cache-requirements.txt` rather than
   duplicating them in docs or workflows.
9. Distinguish the plugin version in `Info.ini` from the archive/package label.
10. Report the exact scope boundary and any validation not performed.

When builds are forbidden, the minimum static validation is:

```powershell
& $cachePython -c "import ast, pathlib; ast.parse(pathlib.Path('tools/build-shader-cache.py').read_text(encoding='utf-8')); print('Python AST OK')"
& $cachePython tools/build-shader-cache.py --help

& $cachePython -c "import pathlib, yaml; [yaml.safe_load(pathlib.Path(p).read_text(encoding='utf-8')) for p in ('.github/workflows/shader-cache.yaml', '.github/workflows/release-build.yaml')]; print('Workflow YAML OK')"

git diff --check HEAD
git status --short
```

Also inspect all `D3DWriteBlobToFile` call sites:

```powershell
rg -n "D3DWriteBlobToFile" src
```

There should be one runtime disk-cache save helper. If C++ was changed while
builds were forbidden, state clearly that compile/link validation remains for
an authorized build environment.
