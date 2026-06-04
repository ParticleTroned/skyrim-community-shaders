# Shader Development Workflow

## Quick Reference

```bash
# Fast shader-only deployment (recommended for dev iteration)
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS

# Full deployment (DLL + tests + shaders)
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target DEPLOY_ALL

# Prove an HLSL refactor changed no compiled bytecode
pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl
```

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
the base is `merge-base(HEAD, <current branch upstream>)`; on this branch that
keeps comparisons against `origin/cs-1.6-PL-VR`. Pass `-BaseRef <ref>` to compare
against another ref. Requires `fxc.exe` from the Windows SDK. The default sweep is
useful targeted coverage, not the full `.github/configs/shader-validation*.yaml`
matrix; pass `-Permutations` for feature-specific define combinations.

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
