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

## Optional shader preparation

Advanced > Shaders > Threading has **Optimised shader preparation**:

-   **Next restart** (default) records discoveries and uses optimisations
    already resident in memory. New cache reads, compilation and D3D creation
    wait for a normal blocking process startup, even with valid disk bytecode.
-   **Transitions (experimental)** also admits preparation during confirmed
    loading and engine sleep/wait intervals. Opening a selection menu alone
    does not grant permission. Loading admission does not depend on
    Stabilizer, synchronisation or an upscaling profile change.
-   **Gameplay (experimental)** also admits asynchronous preparation while
    playing. This can affect frame times.

The policies are cumulative; none disables resident optimisations. Live
weather, material and lighting conditions still select the shader. Detail
Maps and required/general shader compilation retain their existing policy.
The scope includes optional Lighting and Effect pixel shaders and the six
IBL/Linear Lighting Composite variants. Runtime work shares **Background
Compiler Threads**, the existing total worker accounting and cooperative
priority; required queued work takes precedence.

Closing a transition or lowering permission stops new stage admissions.
An active compiler invocation cannot be cancelled and may finish after play
resumes. Bytecode awaiting D3D creation keeps its continuation for another
eligible window, releasing its worker. Publication uses the current
configuration/source generation and retained device, after lifecycle
protection ends. A general shader remains available throughout preparation.
Initial world entry and save/load persistence protection remain separate.
Reflection runs on the worker. Owner-thread publication is bounded per frame
by Background Compiler Threads and does not create device objects. Ready and
completed queues avoid rescanning the optional backlog on every frame.

Both values of the old **Resident-only Lighting Specializations** checkbox
migrate to **Next restart**. The old OFF state allowed cache-only gameplay
preparation; the new default is stricter. A valid explicit new enum wins.
Missing or invalid values select Next restart. Save settings to persist a
session change. Saving/reloading a game is not a process restart.
**Background Compile on Boot** remains independent and skips optional
blocking-startup preparation without changing the selected policy.

Discovery uses the existing bounded, deduplicated history (8192 requests,
512 KiB input limit) and atomic deferred writer outside persistence holds:
`Data/SKSE/Plugins/CommunityShaders/ShaderSpecializations-<runtime>.json`.
SE/AE share `SE`; VR uses `VR`. History records family/descriptor pairs,
not save state, weather, macros or bytecode. Startup replay validates its
enumerated bases; live replay validates requests against a current base.
Startup enumeration prepares general pixel descriptors only, stripping
optional specialization flags injected by draw-time lookup. Optional startup
variants come from validated discovery history instead of duplicating every
enumerated deferred descriptor. Shader mathematics and runtime selection are
unchanged. Persistence failures leave resident/general shading usable and
appear in status.

DevBench `communityshaders.shader_api`, action `specializationPolicy`, reads
or sets integer `preparationMode`: 0 restart, 1 transitions, 2 gameplay.
The deprecated `residentOnly` input maps either value to 0; an explicit enum
wins. Its legacy output is true only for mode 0. `preparation` and snapshot
`compilation.specializationPreparation` report the mode, admission reason,
window generation/kinds, shared budget, queued/deferred/in-flight and
awaiting-publication counts,
cache/source/device stage admissions, cache hits, compiler invocations,
post-window completions, discovery count and persistence failure.
Stage counts describe admissions rather than instantaneous physical API
boundaries. Frame-time or performance claims require controlled runtime
measurements with comparable cache, scene and headset conditions.

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
