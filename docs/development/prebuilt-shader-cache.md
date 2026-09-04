# Prebuilt Shader Cache Runbook

This is the authoritative maintainer and AI-agent procedure for building,
updating, validating, and shipping CSX' prebuilt shader cache.
Use `tools/build-shader-cache.py` for cache generation and
`tools/build-fomod-package.py` for release AIO assembly. Do not assemble cache
blobs, metadata, or installer mappings by hand.

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

The default `shipped` release profile:

-   merges `package/Shaders` and feature `Shaders` trees;
-   excludes every `Tests` directory;
-   mirrors the AIO hidden-feature contract for SE, including Wetness Effects
    when it is part of the AIO, while retaining the existing VR exclusion of
    the legacy `Wetness Effects` package and `WETNESS_EFFECTS` define;
-   enables `UNIFIED_WATER` globally;
-   enables `WETTERNESS` for `Lighting.hlsl` and `Water.hlsl`;
-   omits the VR feature metadata from an SE cache;
-   compiles optimized release bytecode, without developer/debug defines.

Every shipped SE and VR build compiles standard and Horizon Fix inputs, then
places both compatible Water variants in one managed `ShaderCache`. Runtime
registration selects the exact Water record; the installer no longer asks the
user to choose a Horizon cache. The builder requires the loose inputs to have
identical permutation inventories and rejects bytecode differences outside
Water before packing them. Named profiles compile one input variant into the
same managed layout.

Named tester profiles are opt-in. Omitting `--profile` always selects
`shipped`; release workflows and existing maintainer commands therefore keep
their current behavior.

The builder removes the five debug-profile defines itself. Do not replace that
filter with hlslkit's `--strip-debug-defines`: the pinned implementation also
injects `D3DCOMPILE_AVOID_FLOW_CONTROL`, which does not match the default
runtime compile state.

Generated inventories may include `captured_shader_variants`, written by
`.github/configs/generate-shader-configs.ps1`. The builder verifies that exact
number of entries before compiling so a truncated capture cannot be packaged.
The SE release build then adds the small, reviewed
`CROSS_MODLIST_SHADER_VARIANTS` overlay. It contains known SE-only RunGrass
permutations which a single clean modlist may not exercise; it is never applied
to VR or named profiles.

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
| Named cache profiles                            | `CACHE_PROFILES` in `tools/build-shader-cache.py`   |
| Python dependencies and pinned hlslkit revision | `tools/shader-cache-requirements.txt`               |
| SE permutation inventory                        | `.github/configs/shader-validation.yaml`            |
| VR permutation inventory                        | `.github/configs/shader-validation-vr.yaml`         |
| Runtime content digest                          | `src/Utils/ContentHash.h` and `src/ShaderCache.cpp` |
| Runtime manifest schema and atomic persistence  | `src/Utils/ShaderCacheManifest.h`                   |
| Managed A/B pack format and runtime store       | `src/Utils/ShaderCachePack.*`                       |
| External compatibility ABI                      | `include/VRAPI/CSshadercompatibilityapi.h`          |
| Offline compatibility variants                  | `config/shader-compatibility-variants.json`         |
| Plugin versions written to `Info.ini`           | `CMakePresets.json`                                 |
| Feature versions written to `Info.ini`          | `features/*/Shaders/Features/*.ini`                 |
| Standalone/reusable cache CI                    | `.github/workflows/shader-cache.yaml`               |
| Managed-cache FOMOD assembly                    | `tools/build-fomod-package.py`                      |
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
2. applies the selected cache profile (`shipped` by default);
3. compiles standard and Horizon Fix inputs for each shipped runtime, and one
   input for named profiles, with pinned hlslkit;
4. remaps runtime ImageSpace directories;
5. writes `Manifest.json` from source and recursive-include content;
6. writes `Info.ini` with plugin and feature versions;
7. validates metadata, every manifest entry, every blob, the `DXBC` signature,
   and the bounded Water-only delta between each runtime's inputs;
8. writes optimized and developer A/B packs, verifies every SHA-256 committed
   record and generation, and removes the thousands of loose runtime blobs;
9. packages each runtime's managed cache into a raw archive with no installer
   metadata or automatic runtime detection;
10. publishes output only after every requested runtime has passed the earlier
    stages.

An existing runtime output is replaced only when it has the expected,
non-link cache layout and a readable `[Cache] PluginVersion` ownership field.
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
|   |-- ShaderCache/
|   |   |-- Info.ini
|   |   |-- Manifest.json
|   |   |-- PackManifest.json
|   |   |-- Optimized.A.csxpack
|   |   |-- Optimized.B.csxpack
|   |   |-- Developer.A.csxpack
|   |   `-- Developer.B.csxpack
|-- VR/
|   |-- ShaderCache/
|   |   |-- Info.ini
|   |   |-- Manifest.json
|   |   |-- PackManifest.json
|   |   |-- Optimized.A.csxpack
|   |   |-- Optimized.B.csxpack
|   |   |-- Developer.A.csxpack
|   |   `-- Developer.B.csxpack
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

The captured SE inventory describes one clean runtime profile. Release builds
supplement it with the known SE-only permutations in
`CROSS_MODLIST_SHADER_VARIANTS`, currently RunGrass Pixel descriptors `1` and
`10006` and Vertex descriptors `5` and `7`. Keep this overlay separately
reviewed so regenerating a capture cannot make the release cache specific to
one modlist or import VR-only descriptors.

### Build one runtime

```powershell
& $cachePython tools/build-shader-cache.py `
    --runtime SE `
    --package `
    --package-label "v1.7.0"
```

Use `VR` instead of `SE` for a VR-only cache. Do not distribute an SE cache as
VR or combine the two archives.

### Build the Patka tester profile

`patka` is a persistent, VR-only projection of Patka's provided
`SettingsUser.json` cache contract. When asked to build a shader cache for
Patka, use:

```powershell
& $cachePython tools/build-shader-cache.py `
    --runtime VR `
    --profile patka `
    --package
```

Without `--profile patka`, build the normal `shipped` cache exactly as before.
The default Patka archive label includes both the derived core identity and
`Patka`; `--package-label` can still provide a release-specific label.

The profile is derived from the cache-relevant fields in the tester snapshot
with SHA-256
`7FB038E6F237E1A397282CF7BE3624729E361CE3E1D1D07D2A088B4C06D9063A`.
It records the following features as disabled:

-   Cloud Shadows, CS Editor, Extended Translucency, Grass Collision, Hair
    Specular, Linear Lighting, Performance Overlay, RenderDoc, Screenshot,
    Terrain Blending, Volumetric Shadows, Weather Picker, and Wetterness;
-   Horizon Fix, because the captured tester setup does not have its external
    plugin active. Activating that plugin later intentionally invalidates this
    profile and lets the runtime rebuild compatible entries.

It keeps the optimized VR compile state, an empty custom Shader Defines value,
Partial Precision off, the absent/default-off Avoid Flow Control setting, and
Unified Water enabled. The tester may use either Info or Off logging because
neither enables Developer Mode; use Info for validation evidence. Debug or
Trace changes the compile state and invalidates these optimized blobs.

The stale `ExponentialHeightFog` and `Skin` entries in the supplied Disable at
Boot object are intentionally ignored because neither is a current cache
feature with canonical metadata.

This is not a copy of every numeric rendering preference. Numeric settings that
do not affect feature enablement, shader defines, or compiler flags remain in
the user's settings and do not belong in cache metadata. If the tester changes
any cache-contract field, update the named profile and rebuild it; never edit
`Info.ini` without recompiling the matching bytecode.

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

Normally, do not override these values. Official releases ship one
multi-runtime core from the `ALL`/`ALL-VS2022` preset, so the builder derives
both cache identities from that same preset. A cache's SE/VR permutation
inventory remains runtime-specific, while its `Info.ini` plugin version
identifies the compatible core. Use an override only when deliberately pairing
a cache with an independently built runtime-specific core.

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
    --plugin-version-vr "CSX 3.19-VR" `
    --package `
    --package-label "v1.7.0"
```

`--plugin-version` cannot be used with `--runtime both`. Never use the release
tag as the plugin version unless it is literally the plugin's runtime version
label. The label and generated marker are the package handshake; packed-record
validity is independently determined from shader ABI, source, compile state,
and applicable external compatibility requirements.

## Validation and artifact checks

Successful builder completion already proves:

-   both compile inputs of every shipped runtime have the same nonempty permutation
    inventory;
-   only Water blobs differ between each runtime's standard and Horizon Fix
    variants;
-   every requested single-cache named profile contains at least one compiled
    blob;
-   every `.pso`, `.vso`, and `.cso` starts with `DXBC`;
-   `Manifest.json` uses the supported schema;
-   every blob has exactly one valid 32-character lowercase digest;
-   the manifest contains no entry without a blob;
-   `Info.ini` contains the requested plugin version;
-   the archive was created and is nonempty;
-   every archive contains `ShaderCache/Info.ini`, `Manifest.json`,
    `PackManifest.json`, and all four managed pack files;
-   raw runtime archives contain no `fomod` installer tree;
-   every pack header, SHA-256 record, commit trailer, record count, lane, and
    A/B generation validates using the same binary layout consumed by C++.

Optional operator checks:

```powershell
cmake -E tar tf "dist/shader-cache/ShaderCache-SE-v1.7.0.7z"
cmake -E tar tf "dist/shader-cache/ShaderCache-VR-v1.7.0.7z"

Get-FileHash "dist/shader-cache/ShaderCache-*-v1.7.0.7z" -Algorithm SHA256
```

Inspect the pack metadata and confirm the optimized/developer record counts:

```powershell
foreach ($runtime in @("SE", "VR")) {
    $cacheRoot = Join-Path "dist/shader-cache" "$runtime\ShaderCache"
    Get-Content (Join-Path $cacheRoot "Info.ini")

    $manifest = Get-Content (Join-Path $cacheRoot "PackManifest.json") -Raw |
        ConvertFrom-Json
    $packs = Get-ChildItem $cacheRoot -File -Filter "*.csxpack"
    if ($manifest.schemaVersion -ne 1 -or $packs.Count -ne 4 -or
        $manifest.optimizedRecordCount -le 0) {
        throw "$runtime manifest validation failed."
    }
}
```

Do not “repair” a failed artifact by deleting manifest entries, copying blobs
between runtimes, renaming descriptors, or changing timestamps. Fix the source
contract and rerun the supported builder.

## Install and ship

The standalone SE and VR archives are validated release inputs and optional
manual-install artifacts. Each contains one top-level managed `ShaderCache`
directory. Its optimized pack contains both the standard and Horizon-compatible
Water records; runtime compatibility registration selects the exact record.

For manual installation, copy the matching runtime's `ShaderCache` directory
to `<Skyrim>\\Data\\ShaderCache`. Never merge the SE/AE and VR caches.

The normal release path bundles the AIO and both runtime caches into one FOMOD.
Its only selection page offers **Skyrim VR**, **Skyrim SE/AE**, or
**No prebuilt shader cache**. It performs no automatic game, DLL, marker,
settings, load-order, or mod-manager detection. The selected runtime maps
exactly one staged source to `Data/ShaderCache`:

```text
ShaderCache-VR/ShaderCache
ShaderCache-SE-AE/ShaderCache
```

Assembly requires both cache packs to declare the same shader-cache ABI as the
core AIO's `SKSE/Plugins/CSX.BuildManifest.json`. A disagreement fails the
release before the FOMOD archive can replace the plain AIO.

The plugin independently validates cache identity, shader ABI, source content,
and registered compatibility requirements. A missing or mismatched record is
compiled locally. Enabling or disabling Horizon Fix does not require reinstalling
the cache because both compatible Water records coexist in the managed pack.

Ship the caches, DLL, shaders, compatibility manifest, and feature metadata from
the same ref. Do not package a cache from one commit with the AIO from another.

For a smoke test, use a clean mod-manager profile, move any existing
`ShaderCache` aside so it can be restored, and test the VR, SE/AE, and
no-cache installer paths. For each runtime, test once with Horizon Fix disabled
and once enabled; inspect `CommunityShaders.log` for pack validation,
compatibility selection, fallback compilation, and unexpected invalidation.

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
downloads both artifacts into `dist`, and extracts them beside the plain AIO.
`tools/build-fomod-package.py` validates and stages both managed runtime caches,
writes the one-page manual FOMOD, and replaces the plain AIO archive only after
the replacement is nonempty and contains every required payload. Artifact
attestation and draft-release publication happen after that replacement. The
standalone runtime archives remain attached for operators and manual installs.
No separate manual cache run is required for that path.

The final AIO archive contains:

```text
Core/
fomod/ModuleConfig.xml
fomod/info.xml
ShaderCache-VR/ShaderCache/
ShaderCache-SE-AE/ShaderCache/
```

## When a cache rebuild is required

| Change                                                                                       | Required action                                                                                   |
| -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Shipped `.hlsl` or `.hlsli` content                                                          | Audit/bump the owning feature version as required; rebuild affected SE and VR caches              |
| Feature `[Info] Version` or shipped enabled profile                                          | Rebuild and verify generated `Info.ini`                                                           |
| SE/VR validation YAML or permutation inventory                                               | Rebuild that runtime; rebuild both if shared assumptions changed                                  |
| Selected profile constants, excluded packages, or ImageSpace mapping in the builder          | Rebuild every affected profile/runtime                                                            |
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
5. If a cache profile changed, update its profile constants in
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

When all four managed files are present, runtime lookup uses an exact record
identity: logical shader path, recursive source/compile-state contract, and the
SHA-256 canonical requirement set supplied by applicable external providers.
An identity miss compiles and appends only that shader. Standard optimized and
developer/debug records use separate lanes, so diagnostic compilation neither
evicts nor masks release bytecode.

Each lane has fixed A and B files supplied by the cache mod. Runtime never
creates, renames, copies, or deletes them. Records become visible only after a
validated commit trailer and payload SHA-256; an incomplete tail is ignored and
truncated before the next append. Before the main menu, compaction runs only
when active-generation superseded bytes and fragmentation cross their bounded
thresholds. It writes current logical records into the inactive file at a
higher generation and leaves the old generation searchable as fallback.

If any managed file is missing, the whole pack feature is unavailable and CSX
retains the previous loose-cache behavior. `Manifest.json` remains part of that
compatibility path. An explicit clear resets existing pack files in place;
normal source, feature, and external-contract changes never rotate or blanket
delete the managed cache.

Users can still compile local variants when:

-   a required exact source, feature, or external compatibility identity is absent;
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
10. Use `--profile patka` only when explicitly asked for Patka's cache; omit
    `--profile` for normal release caches.
11. Report the exact scope boundary and any validation not performed.

When builds are forbidden, the minimum static validation is:

```powershell
& $cachePython -c "import ast, pathlib; [ast.parse(pathlib.Path(p).read_text(encoding='utf-8')) for p in ('tools/build-shader-cache.py', 'tools/build-fomod-package.py')]; print('Python AST OK')"
& $cachePython tools/build-shader-cache.py --help
& $cachePython tools/build-fomod-package.py --help

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
