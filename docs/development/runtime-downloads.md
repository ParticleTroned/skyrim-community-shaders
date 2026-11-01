# Builds without upscaler runtime downloads

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
BUILD_HOST_TESTS=OFF
AUTO_PLUGIN_DEPLOYMENT=OFF
```

Use `pwsh ./tools/cmake.ps1 --preset <preset>` with separate `-D` arguments,
for example `-D SKIP_RUNTIME_DOWNLOADS=ON`. Then build only the
`CommunityShaders` target through the same launcher and build preset.
The option applies to the supported flat SE and AE configurations.

Skip mode performs no upscaler runtime downloads or SDK archive extraction.
It reuses FidelityFX DLLs only when their hashes match the pinned values.
Streamline requires a verified cached archive, a matching extraction stamp and
a per-file hash manifest before staging existing production DLLs and original
notices. The manifest is written only after normal verified extraction; modified
SDK DLLs or notices do not qualify even if the archive stamp still matches. Missing
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
`Shaders/Upscaling/FidelityFX`, `Shaders/Upscaling/Streamline` and
`Shaders/Upscaling/StreamlineDX12` contents
untouched, even when the staging tree contains different versions. Shader
cleanup retains those directories as well. SE keeps shader staging separate from
non-shader AIO staging, so those two owners cannot race on runtime files. Existing ownership is
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

The separate StreamlineDX12 payload contains SE DLSS frame-generation support;
FidelityFX includes the framegeneration, loader and upscaler modules. Both native
and DX12 Streamline directories retain the original SDK notices. Skip mode does
not add the optional neural-rendering provider to packages; its distribution
exclusion is validated independently by the NR payload guard.

`tests/streamline_runtime_cache_test.py` runs the actual staging module against a
small local SDK archive fixture. Only the archive URL and digest are replaced by
fixture inputs. It checks both SE runtime directories, notices, verified cache
reuse, tampered SDK files, missing manifests and archives, and preservation of
existing staged files. It does not download an SDK or build a game DLL.
