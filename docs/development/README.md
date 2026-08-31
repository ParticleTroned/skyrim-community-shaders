# Development Documentation

-   [Build provenance](build-provenance.md) — exact DLL, dependency, and shader-cache identities for reproducible tests and releases.
-   [Developer tooling](tooling.md) — reliable Git hooks, GitHub transport, Codex sandbox, and Windows build setup.
-   [Ghidra MCP integration](ghidra-mcp.md) — pinned extension installation,
    loopback MCP configuration, and Skyrim VR live-dump analysis.
-   [VR depth-culling temporal policy](vr-depth-culling-temporal-policy.md) — bounded recovery for one-frame-late Skyrim VR OBB results.
-   [VR depth-culling evidence](vr-depth-culling-temporal-evidence.md) — source history, live Skyrim VR layout evidence, and exact local validation.
-   [API service registry](api-service-registry.md) — parallel versioned service discovery while retaining the legacy CSAP interface.
-   [Screenshot API and sequences](screenshot-api-and-sequences.md) — asynchronous still/sequence contract, native CSXR discovery, receipts, events, and manifests.
-   [Shader API v1](api-shader-v1.md) — versioned inspection, feature-state, compilation, and cache-lifecycle controls with preflight safety.
-   [Shader compatibility API v1](api-shader-compatibility-v1.md) — startup registration of external shader-facing contracts and narrowly scoped cache identities.

## Getting Started

-   **[VSCode Setup](./vscode-setup.md)** - IDE configuration, extensions, and auto-deploy
-   **[Shader Workflow](./shader-workflow.md)** - Fast shader iteration and deployment
-   **[Prebuilt Shader Cache](./prebuilt-shader-cache.md)** - Release build, update, validation, and AI-agent runbook
-   **[Shader Runtime A/B](./shader-runtime-ab.md)** - RenderDoc same-frame shader equivalence checks
-   **[GPU-Unified Presets](./unified-presets.md)** - One preset path with capability-selected DLSS or FSR
-   **[Render-scale PR qualification](./render-scale-pr-qualification.md)** - Bounded 20-COC, 25-menu-transition, and three-sequence release gate
-   **[Screenshot API and Sequences](./screenshot-api-and-sequences.md)** - Versioned asynchronous still/sequence contract, acknowledgements, manifests, and implementation gates

## Quick Links

### Common Tasks

-   **One-time developer setup:** `pwsh ./tools/setup-dev.ps1`
-   **Tooling diagnostics:** `pwsh ./tools/dev-doctor.ps1 -Network`
-   **Fast shader deployment:** `pwsh ./tools/cmake.ps1 --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS`
-   **Verify shader refactor bytecode:** `pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl`
-   **Runtime A/B shader check:** `tools/taa-renderdoc-ab.py` via RenderDoc embedded Python
-   **Full build with deployment:** `.\BuildRelease.bat ALL-WITH-AUTO-DEPLOYMENT`
-   **Run shader tests:** `pwsh ./tools/cmake.ps1 --build build/ALL --target run_shader_tests`
-   **Create a worktree with submodules + local preset:** `pwsh ./tools/new-worktree.ps1 -Name my-branch`
-   **Install optional git alias:** `pwsh ./tools/install-worktree-alias.ps1`
-   **Install optional Ghidra MCP extension:**
    `pwsh ./tools/setup-ghidra-mcp.ps1 -GhidraInstallDir <path> -JavaHome <path>`
-   **Manage headless Ghidra MCP:**
    `pwsh ./tools/ghidra-mcp-control.ps1 start -GhidraInstallDir <path> -ProgramPath <binary>`

### Build Presets

-   `ALL` - Standard build (no auto-deployment)
-   `ALL-WITH-AUTO-DEPLOYMENT` - Build + deploy to game directory
-   `Dev` - Fast iteration preset (recommended for development)

See `CMakePresets.json` for all available presets.

### Complete Local Validation

The main DLL target does not build every test executable. A clean pull-request validation must build both test groups explicitly before running CTest:

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -DBUILD_CONTROLLER_TESTS=ON -DBUILD_SHADER_TESTS=ON
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
```

Changes to the VR master custom-shader switch also require a headset runtime check before merge. With render scaling both active and inactive, disable custom shaders from the CSX menu and verify that native eye targets are restored before the switch completes, the scene has no stale overlay or deferred attachment, and re-enabling works. Repeat in the main menu and in-world when the submit path changes.

## Worktrees

Use `tools/new-worktree.ps1` when creating a new worktree for development. The script:

-   Creates the worktree under a sibling `<repo>.worktrees/` directory by default
-   Reuses an existing local branch or creates a new one from the explicit
    `-StartPoint`, the repository-local `csx.worktreeStartPoint` setting, or
    `origin/HEAD` in that order; `HEAD` is only the final fallback
-   Runs `git submodule update --init --recursive` in the new worktree
-   Copies `CMakeUserPresets.json` from the main checkout if it exists there
-   Does not overwrite an existing `CMakeUserPresets.json` unless `-ForcePresetCopy` is passed

Examples:

-   `pwsh ./tools/new-worktree.ps1 -Name reproj_fixes`
-   `pwsh ./tools/new-worktree.ps1 -Name vr-debug -StartPoint dev`
-   `pwsh ./tools/new-worktree.ps1 -Name clean-build -NoSubmodules`

Set a rolling integration branch as the repository-local default when feature
branches should consistently start from a maintained local baseline:

-   `pwsh ./tools/git.ps1 config csx.worktreeStartPoint codex/local-main-VR`

Use `-StartPoint origin/main-VR` explicitly for work intended to begin directly
from the upstream integration branch.

If you want a Git-native command, install the optional repo-local alias:

-   `pwsh ./tools/install-worktree-alias.ps1`
-   Then use `git new-worktree reproj_fixes`

The alias is installed into local Git config by default, so it does not affect other users unless they opt in.

## Build Targets

| Target             | Builds DLL | Runs Tests | Copies Shaders | Use Case               |
| ------------------ | ---------- | ---------- | -------------- | ---------------------- |
| `COPY_SHADERS`     | ❌         | ❌         | ✅             | Fast shader iteration  |
| `DEPLOY_ALL`       | ✅         | ✅         | ✅             | Full deployment (auto) |
| `prepare_shaders`  | ❌         | ✅         | ✅ (AIO only)  | CI shader validation   |
| `run_shader_tests` | ❌         | ✅         | ❌             | Test shaders only      |

## Contributing

When adding new features or documentation, please keep development docs organized under `docs/development/`.
