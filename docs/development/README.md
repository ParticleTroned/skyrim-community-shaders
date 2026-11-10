# Development Documentation

These public guides cover working with a fork, building CSX, compiling
shaders, and assembling packages. Feature setup notes, fix investigations,
and detailed test or provenance records stay in ignored local documentation.

## Build and fork setup

-   [Developer tooling](tooling.md): setup, Git wrappers, hooks, and Windows builds.
-   [VSCode setup](vscode-setup.md): editor support and optional shader deployment.
-   [Runtime downloads](runtime-downloads.md): DLL development when runtime payloads are unavailable.
-   [Build identities](build-provenance.md): manifests, artifact hashes, and verification.
-   [Hardware-counter builds](nsight-perf-build.md): optional NVIDIA SDK integration for developer profiling.
-   [DLL loader diagnostic](dll-load-smoke.md): optional repeated loading of a built DLL in a surrogate host.
-   [Test-build versioning](test-build-versioning.md): reproducible test distributions.
-   [Release distribution](csx-release-distribution.md): complete AIO packaging and fork publication.

Clone a fork with its submodules, then run from the repository root:

```powershell
pwsh ./tools/setup-dev.ps1
pwsh ./tools/cmake.ps1 --preset ALL
pwsh ./tools/cmake.ps1 --build --preset CSmain
```

The AMD Anti-Lag 2 headers are pinned in `extern/AntiLag2-SDK`. Existing
checkouts must initialize that submodule before configuring, including VR
builds of the shared source:

```powershell
pwsh ./tools/git.ps1 submodule update --init extern/AntiLag2-SDK
```

Keep local deployment configuration in the ignored `CMakeUserPresets.json`.
Use `CMakeUserPresets.json.template` as a starting point. `CMakePresets.json`
lists the tracked presets; local presets can add deployment destinations.

## Shader compilation and validation

-   [Shader workflow](shader-workflow.md): targeted compilation, deployment, and bytecode comparisons.
-   [Prebuilt shader cache](prebuilt-shader-cache.md): permutation inventories, cache generation, validation, and FOMOD assembly.
-   [Shader runtime comparison](shader-runtime-ab.md): compare baseline and candidate DXBC when bytecode differs.

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --target prepare_shaders
pwsh ./tools/cmake.ps1 --build build/ALL --target validate_changed
pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl
python tools/build-shader-cache.py --runtime both --package
```

Use matching SE/AE and VR permutation inventories when preparing a universal
package. Preserve existing shader caches unless regeneration is required.

## Worktrees and checks

Create an isolated checkout with submodules and a copied local preset:

```powershell
pwsh ./tools/new-worktree.ps1 -Name my-branch
pwsh ./tools/pre-commit.ps1 run
pwsh ./tools/validate-local.ps1
```

If submodule initialization fails, the worktree helper fetches missing
top-level dependency pins from their absolute `.gitmodules` URLs and retries
recursive initialization once. It preserves branch/tag refs and dependency
pins. Unresolved failures leave the new worktree in place, report an error
and skip preset copying. `-NoSubmodules` bypasses initialization and recovery.

Validation saves complete evidence locally. Public results state checks,
outcomes, limitations, and source/build identities without machine-specific
paths, usernames, or local evidence locations.
