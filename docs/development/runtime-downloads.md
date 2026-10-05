# Builds without upscaler runtime downloads

## NR release package

Use the `Package-nr` configure and build presets for a release AIO with
Neural Rendering support and no redistributed NR DLLs:

```powershell
pwsh ./tools/cmake.ps1 --preset Package-nr
pwsh ./tools/cmake.ps1 --build --preset Package-nr
```

`BuildRelease.bat Package-nr` runs both steps. The manual GitHub Actions
workflow **Package-nr** uses the same presets and uploads a `Package-nr`
artifact containing the complete `CSX_AIO-*.7z` installer. It does not
publish a release. Local output is under `build/Package-nr/aio` and `dist`.

The Streamline folder contains only six production DLLs and five original
notices from the SHA-256-pinned official
[Streamline 2.14.1 SDK](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1).
Each release configure extracts the verified archive again. Installation
checks the exact file inventory and hashes, and rejects NR DLLs anywhere
in the package. Local NR runtime paths, private kernel payloads and
`SKIP_RUNTIME_DOWNLOADS=ON` are rejected for this release preset.

Neural Rendering is registered and visible like the other core features.
Without `nvngx_dlssnr.dll`, its controls are greyed out with the full DLL
name and installation instructions. A saved enabled preference cannot
request NR rendering or require Render Scale while the DLL is absent.
Normal FOV + TAA remains available, and its saved mask settings are retained.

Users supply **`nvngx_dlssnr.dll`** in
**`Data/Shaders/Upscaling/Streamline/`**, beside `nvngx_dlss.dll` and the
other Streamline DLLs, then restart the game. `sl.dlss_nr.dll` is not
required: CSX calls the NR provider directly. Keep the official bundled
Streamline DLLs in place. Installing the provider makes NR controls
available; users still choose whether to enable NR.

The loader accepts compatible 310.8 variants, including modified or
unsigned files with unlisted hashes. Admission still verifies the file
version, required exports and loaded image identity; initialization or
evaluation failures retain the existing fallback behavior. Developer mode
is not required. With DevBench enabled in a development build,
`communityshaders.neural_rendering` action `nr_status` exposes
`runtime.installed`, `runtime.requiredDll` and `runtime.installationNotice`.

See the [NR release validation and review record](nr-release-package-validation-20261005.md)
for exact build identity, package checks and local provider-admission results.

## DLL development without runtime downloads

`SKIP_RUNTIME_DOWNLOADS=ON` allows DLL development when the FidelityFX or
Streamline runtime payloads are unavailable. It defaults to `OFF`. This
option does not supply other dependencies: the compiler, SDK headers,
submodules and vcpkg dependencies must still be available.

For a DLL-only configuration, set these CMake cache values on your usual
preset:

```text
SKIP_RUNTIME_DOWNLOADS=ON
ZIP_TO_DIST=OFF
AIO_ZIP_TO_DIST=OFF
BUILD_SHADER_TESTS=OFF
BUILD_CONTROLLER_TESTS=OFF
AUTO_PLUGIN_DEPLOYMENT=OFF
```

Use `pwsh ./tools/cmake.ps1 --preset <preset>` with separate `-D` arguments,
for example `-D SKIP_RUNTIME_DOWNLOADS=ON`. Then build only the
`CommunityShaders` target through the same launcher and build preset.
The option applies equally to SE, AE and VR configurations.

Skip mode performs no upscaler runtime downloads or SDK archive extraction.
It reuses FidelityFX DLLs only when their hashes match the pinned values.
Streamline requires a verified cached archive and a matching extraction
stamp before staging existing production DLLs and original notices. Missing
or invalid caches remain on disk and are reported as unavailable payloads.
Only available files become staging inputs or build dependencies.

If required payloads are unavailable, either automatic zip option fails
configuration. Full installation and runtime-component installation fail
before modifying the destination, including before AIO staging is cleared.
Installing the SKSE-only component remains available. Reconfigure with
`SKIP_RUNTIME_DOWNLOADS=OFF` to restore normal downloads and packaging.
Installation rechecks every selected runtime payload against its configured
SHA-256 before staging changes. Removing or modifying a cached input after
configuration therefore fails before the AIO reset, while SKSE-only
installation remains independent of runtime payloads.

If auto-deployment is enabled separately, skip mode leaves deployed
`Shaders/Upscaling/FidelityFX` and `Shaders/Upscaling/Streamline` contents
untouched, even when the staging tree contains different versions. Shader
and AIO cleanup retain those directories as well. Existing ownership is
retained only for unchanged files already owned by this build's deployment
manifest. Normal stale-file cleanup resumes when skip mode is disabled;
modified and unowned files keep the existing preservation behavior.

The script-only regression suite exercises cache verification, incomplete
payloads, install failure ordering, both staging cleanup modes and the
deployment ownership transition. Run it without compiling targets:

```powershell
pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D TEST_ROOT=build/runtime-download-policy -P tests/runtime_download_policy_test.cmake
```

Each run uses a new fixture directory under `TEST_ROOT`; it does not deploy
to a game or use the real runtime cache. Full project configuration, DLL
builds and actual package validation are separate checks.
