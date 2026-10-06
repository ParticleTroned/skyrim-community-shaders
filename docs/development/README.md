# Development Documentation

These public guides cover working with a fork, building CSX, compiling
shaders, and assembling packages. Feature setup notes, fix investigations,
and detailed test or provenance records stay in ignored local documentation.

## Build and fork setup

-   [Developer tooling](tooling.md): setup, Git wrappers, hooks, and Windows builds.
-   [VSCode setup](vscode-setup.md): editor support and optional shader deployment.
-   [Runtime downloads](runtime-downloads.md): DLL development when runtime payloads are unavailable.
-   [Build identities](build-provenance.md): manifests, artifact hashes, and verification.
-   [Test-build versioning](test-build-versioning.md): reproducible test distributions.
-   [Release distribution](csx-release-distribution.md): complete AIO packaging and fork publication.

Clone a fork with its submodules, then run from the repository root:

```powershell
pwsh ./tools/setup-dev.ps1
pwsh ./tools/cmake.ps1 --preset ALL
pwsh ./tools/cmake.ps1 --build --preset CSmain
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

Validation saves complete evidence locally. Public results state checks,
outcomes, limitations, and source/build identities without machine-specific
paths, usernames, or local evidence locations.
