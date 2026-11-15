# Shader Development Workflow

## Quick Reference

```bash
# Fast shader-only deployment (recommended for dev iteration)
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS

# Full deployment (DLL + tests + shaders)
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target DEPLOY_ALL

# Prove an HLSL refactor changed no compiled bytecode
pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl

# Runtime A/B for expected bytecode divergence
# See docs/development/shader-runtime-ab.md
exec(open(r"<repo>/tools/taa-renderdoc-ab.py").read())

# Compile only shaders affected by working-tree changes
cmake --build ./build/ALL --target validate_changed

# Build raw SE and VR shader-cache archives for release assembly
python tools/build-shader-cache.py --runtime both --package
```

## Prebuilt shader cache

Use `tools/build-shader-cache.py` for release caches. The authoritative
prerequisites, exact local and CI commands, version/update rules, validation
gates, recovery procedures, scope boundary, and AI-agent instructions are in
the [Prebuilt Shader Cache Runbook](./prebuilt-shader-cache.md).

## Startup source preparation

Blocking startup compilation retains up to 16 MiB of include bytes and
entry payloads across at most 1,024 includes; the entry limit also bounds
container overhead. Reuse checks file identity, size, last-write time,
and change time through an open handle that excludes
writers and replacement. Readers retain immutable snapshots while copying
outside the cache lock. Unsupported metadata or concurrent writer access
falls back to ordinary reads. Source digest validation and compiler flags
are unchanged. Developer compilation and file watching bypass reuse;
entering background mode or finishing startup releases the snapshots.

The legacy loose-file manifest checkpoints every 256 records during
blocking startup and every 25 otherwise. Compilation completion, the end
of the startup wait, and shutdown flush remaining changes. Managed shader
packs retain their existing batched persistence.

`ShaderInclude` exercises bytecode equivalence for flat and VR macros,
same-size edits with preserved timestamps, replacement, mapped writes,
read failures, bounded storage, and concurrent shutdown. `SharedShaderCompile` covers
edits between preprocessing and compilation. `ShaderContentReuse` checks
legacy startup batching, failed-write recovery, concurrent persistence,
and the transition to normal checkpoints using the production policy.

## Optional lighting specialization diagnostics

Advanced > Shaders > Threading has **Resident-only Lighting
Specializations**. Off (default) loads valid cached optional variants
asynchronously during gameplay. A missing, stale, or rejected cache entry
retains general shading and records the variant for a later startup; it
never falls through to optional source compilation. On uses resident variants and retains general
shading on an optional miss. It suppresses new requests after startup,
including background boot once game data is loaded. It neither invalidates
shaders nor cancels queued work. Required general shaders retain their
normal compilation policy. Save settings to persist the choice.

Discovered variants are prepared on the next blocking startup. Pixel
requests must match a base descriptor admitted by that startup's shader
enumeration; stale or unsupported descriptors cannot introduce arbitrary
compiler inputs. Composite requests select only the six supported
interior/exterior combinations. Both use current features, macros and
content-verified cache admission. Startup waits for composite preparation
as well as graphics tasks. Background startup skips this optional source
preparation, retaining discoveries for a later blocking startup. Switching
to background mode prevents further optional compiler invocations; a
compiler invocation already running must finish.

Discovery history is bounded to 8192 unique requests and persisted
atomically by the existing cache writer, outside save/load I/O blocking, in
`Data/SKSE/Plugins/CommunityShaders/ShaderSpecializations-<runtime>.json`.
SE and AE share the `SE` history; VR uses `VR`. This stores descriptors,
not shader bytecode, paths or macros. Malformed or incompatible history is
ignored with a warning. Shutdown flushes remaining discoveries after workers stop.
Failed persistence retains general shading and is
reported through DevBench. Disabling disk caching or cache reads prevents
gameplay cache reuse but does not authorize optional source compilation.

This covers dry Wetterness, inactive Snow Cover, absent mesh variation,
absent local lights, disabled contact shadows and unused legacy subsurface
scattering. Dry Wetterness follows the uploaded dry state, retaining the
existing drying timeline, debug overrides and persistent shoreline
darkening.

The same policy covers ambient world Effect lighting and deferred
composite specializations for IBL luminance ratio, linear lighting, or
both. Eligibility uses the effective settings uploaded for the current
frame. A missing optional shader retains the general shader while
preparation is allowed, or for the whole miss when resident-only is on.

At Info level, `[ShaderSpecialization]` reports each missing descriptor
once per graphics-cache revision and policy (`request=allowed` or
`request=suppressed`). These are resident misses, not counts of source
compilation. Miss reporting is bounded to 8192 descriptor/policy pairs per
revision and explicitly reports when further misses are omitted.
Miss logs use the existing background pool to avoid disk flushes on the
draw thread, so their output can lag preparation logs.
`prepared` entries report the bytecode source, `source_compiles` (actual
compiler invocations, including retries), publication success and elapsed
preparation time. Disk or shared-bytecode reuse can report zero source
compiles while still creating a D3D shader. Elapsed time includes cache
work and object creation; it is not a frame-time or stutter measurement.
`source=deferred_until_startup` records an optional cache miss; it is not a
compiler failure. The graphics task finishes without a shader while its
general fallback remains available.
Composite entries use `composite_revision`, variant and interior/exterior
identity instead of a pixel descriptor. Their miss reports are bounded to
the six variants and two request policies per composite-cache revision.

DevBench `communityshaders.shader_api`, action `specializationPolicy`, reads
the setting; supplying boolean `residentOnly` changes it for the session.
The normal settings save can persist that choice. The snapshot reports
`compilation.residentOnlyLightingSpecializations` and
`compilation.specializationPreparation`. The latter reports the gameplay
policy, current source-compilation permission, discovery count,
persistence failures and outstanding composite workers. The policy action
returns the same data as `preparation`. Use the same compiled
revision and cache conditions for comparisons; a cache warmed in the first
condition cannot measure cold first-use behavior in the second condition.

## Verifying refactors

`tools/verify-shader-refactor.ps1` (bash wrapper: `tools/verify-shader-refactor.sh`)
compiles a shader from a base git ref and from the working tree across the `VR`
x `HDR_OUTPUT` permutations, then compares the compiled DXBC bytecode. The base
ref's full include tree is materialized with `git archive`, so shared `.hlsli`
changes are compared against their base-ref versions instead of being masked by
working-tree headers.

-   **IDENTICAL** SHA-256 of the `.cso` means the compiled GPU program is
    byte-for-byte identical.
-   **DIFFERS** dumps `/Fc` assembly differences for review.

Exit codes: `0` all identical, `2` some differ, `1` compile error. By default,
the base is `merge-base(HEAD, <current branch upstream>)`. Pass
`-BaseRef <ref>` to compare against another ref. Requires `fxc.exe` from the
Windows SDK. The default sweep is
useful targeted coverage, not the full `.github/configs/shader-validation*.yaml`
matrix; pass `-Permutations` for feature-specific define combinations.

When a refactor intentionally changes compiled bytecode, validate the behavior
with the RenderDoc runtime A/B harness in `tools/taa-renderdoc-ab.py`. Capture
one frame, swap in baseline and candidate DXBC on that same frame, and judge the
candidate against the baseline noise floor. See
`docs/development/shader-runtime-ab.md` for the operator runbook and the
RenderDoc/upscaling caveats on this branch.

## Incremental Shader Validation

Validating the full shader suite recompiles thousands of variants per config,
which is slow for a small HLSL change. Incremental validation compiles only the
entry-point shaders that transitively `#include` a changed file, derived from an
include dependency graph. A leaf shader used by one entry point validates in
seconds; a shared `Common/*.hlsli` fans out to every shader that includes it.

### Local

```bash
# Compile only shaders affected by working-tree changes vs HEAD
cmake --build ./build/ALL --target validate_changed

# Override the config (defaults to Flatrim / shader-validation.yaml)
cmake -DVALIDATE_CHANGED_CONFIG=.github/configs/shader-validation-vr.yaml -S . -B build/ALL
cmake --build ./build/ALL --target validate_changed
```

The target depends on `prepare_shaders`, so the AIO shader tree is assembled
first. Both targets are available even with `AUTO_PLUGIN_DEPLOYMENT` and
`AIO_ZIP_TO_DIST` disabled. Staging does not build the game DLL; when
`BUILD_SHADER_TESTS` is enabled, it runs the shader unit tests first.
Under the hood it runs `tools/validate_changed_shaders.py`, which maps
changed `package/Shaders/**` and `features/**/Shaders/**` paths into the AIO
layout and passes them to `hlslkit-compile --changed-files`. Local use requires
Python and an `hlslkit-compile` version with `--changed-files` support on PATH.

### CI

The PR shader-validation job feeds the PR changed-file list from
`tj-actions/changed-files` into the same wrapper. Validation is narrowed only
when it is provably safe. These cases force a full run:

-   a validation config (`.github/configs/**`), CMake, or submodule change,
    because these can redefine the entry-point/define set;
-   a changed shader path outside the known shader roots;
-   push/release builds, which do not provide a PR change set.

`hlslkit` applies the same safety net independently: any changed path it cannot
find in the shader tree falls back to full validation. Preprocessor guards are
ignored when scanning `#include`s, so the affected set is always a conservative
superset.

## Overview

Two deployment targets for different workflows:

-   **`COPY_SHADERS`** - Fast shader-only deployment (seconds)
-   **`DEPLOY_ALL`** - Full build + tests + deployment (minutes)

### Requirements

-   Must have `AUTO_PLUGIN_DEPLOYMENT=ON` in your CMake preset
-   Must have `CommunityShadersOutputDir` environment variable set to your Skyrim directory

### Usage

#### Manual

```bash
# Fast iteration: Only copy changed shaders to game directory
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS

# Or in Visual Studio: Right-click "COPY_SHADERS" target -> Build

# Full deployment (same as running cmake --build with no target):
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target DEPLOY_ALL
```

#### Automatic (VSCode)

You can configure VSCode to automatically deploy shaders when you save `.hlsl` or `.hlsli` files using the [RunOnSave](https://marketplace.visualstudio.com/items?itemName=emeraldwalk.RunOnSave) extension.

**See [VSCode Setup](../development/vscode-setup.md) for complete configuration instructions.**

### Prerequisites

1. Run `cmake --preset ALL-WITH-AUTO-DEPLOYMENT` at least once to create build directory
2. Set `CommunityShadersOutputDir` environment variable to your Skyrim `Data` directory
3. Ensure `AUTO_PLUGIN_DEPLOYMENT=ON` in your CMake preset

### What COPY_SHADERS does now

1. ✅ Transforms shaders from source layout → game layout (via AIO staging)
2. ✅ Copies only changed shader files (incremental robocopy)
3. ✅ Deploys to `$CommunityShadersOutputDir/Shaders`
4. ❌ Does NOT build the DLL
5. ❌ Does NOT run shader tests
6. ❌ Does NOT deploy non-shader files

### Target Comparison

| Target            | Builds DLL | Runs Tests | Copies Shaders | Use Case              |
| ----------------- | ---------- | ---------- | -------------- | --------------------- |
| `COPY_SHADERS`    | ❌         | ❌         | ✅             | Fast shader iteration |
| `DEPLOY_ALL`      | ✅         | ✅         | ✅             | Full deployment       |
| `prepare_shaders` | ❌         | ✅         | ✅ (AIO only)  | CI validation         |
