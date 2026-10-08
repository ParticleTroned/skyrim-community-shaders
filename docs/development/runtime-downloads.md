# Builds without upscaler runtime downloads

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
