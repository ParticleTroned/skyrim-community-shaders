# Development Documentation

-   [Build provenance](build-provenance.md) — exact DLL, dependency, and shader-cache identities for reproducible tests and releases.
-   [Developer tooling](tooling.md) — reliable Git hooks, GitHub transport, Codex sandbox, and Windows build setup.

## Getting Started

-   **[VSCode Setup](./vscode-setup.md)** - IDE configuration, extensions, and auto-deploy
-   **[Shader Workflow](./shader-workflow.md)** - Fast shader iteration and deployment
-   **[Prebuilt SE Shader Cache](./prebuilt-shader-cache.md)** - Release cache generation, validation, and packaging

## Quick Links

### Common Tasks

-   **One-time developer setup:** `pwsh ./tools/setup-dev.ps1`
-   **Tooling diagnostics:** `pwsh ./tools/dev-doctor.ps1 -Network`
-   **Fast shader deployment:** `pwsh ./tools/cmake.ps1 --build build/ALL --target COPY_SHADERS`
-   **Verify shader refactor bytecode:** `pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl`
-   **Runtime A/B shader check:** `tools/taa-renderdoc-ab.py` via RenderDoc embedded Python
-   **Release AIO build:** `.\BuildRelease.bat AIO-Release`
-   **Run shader tests:** `pwsh ./tools/cmake.ps1 --build build/ALL --target run_shader_tests`
-   **Create a worktree with submodules + local preset:** `pwsh ./tools/new-worktree.ps1 -Name my-branch`
-   **Install optional git alias:** `pwsh ./tools/install-worktree-alias.ps1`

### Build Presets

-   `ALL` - Standard build (no auto-deployment)
-   `AIO-Release` - Release AIO folder without hidden packaged features
-   `Dev` - Fast iteration preset (recommended for development)

See `CMakePresets.json` for all available presets.

### Complete Local Validation

The main DLL target does not build the shader-test executable. A clean pull-request validation must build both targets explicitly before running CTest:

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -DBUILD_SHADER_TESTS=ON
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders shader_tests -- /m:1
ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300
```

## Worktrees

Use `tools/new-worktree.ps1` when creating a new worktree for development. The script:

-   Creates the worktree under a sibling `<repo>.worktrees/` directory by default
-   Reuses an existing local branch or creates a new one from `HEAD`
-   Runs `git submodule update --init --recursive` in the new worktree
-   Copies `CMakeUserPresets.json` from the main checkout if it exists there
-   Does not overwrite an existing `CMakeUserPresets.json` unless `-ForcePresetCopy` is passed

Examples:

-   `pwsh ./tools/new-worktree.ps1 -Name reproj_fixes`
-   `pwsh ./tools/new-worktree.ps1 -Name shader-debug -StartPoint dev`
-   `pwsh ./tools/new-worktree.ps1 -Name clean-build -NoSubmodules`

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
