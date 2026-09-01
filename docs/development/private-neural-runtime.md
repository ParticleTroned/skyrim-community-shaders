# Private Neural Rendering runtime

The Neural Rendering development runtime is opt-in and local-only. A normal
configure never reads a private DLL and all ordinary AIO, feature, install, and
`dist` targets continue to use the archive-pinned official Streamline 2.12
runtime. In particular, NVIDIA frame generation remains in
`Shaders/Upscaling/StreamlineDX12` on Streamline 2.12.

Private builds replace only `Shaders/Upscaling/Streamline` with the six pinned
Streamline 2.13 DLSS files and the pinned `nvngx_dlssnr.dll`. Configuration
fails closed unless every filename, SHA-256, x64 PE machine, and embedded file
version matches `cmake/PrivateNvidiaRuntime.json`. No vendor DLL is copied into
the source tree.

Keep machine paths in the ignored `CMakeUserPresets.json`, or provide them as
local cache values:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "private-nr",
            "inherits": "AIO-Release",
            "cacheVariables": {
                "CSX_ENABLE_PRIVATE_NVIDIA_RUNTIME": "ON",
                "CSX_PRIVATE_STREAMLINE_213_DIR": "$env{CSX_LOCAL_STREAMLINE_DIR}",
                "CSX_PRIVATE_DLSSNR_DLL": "$env{CSX_LOCAL_DLSSNR_FILE}",
                "CSX_PRIVATE_SHADER_CACHE_ROOT": "$env{CSX_SHARED_SHADER_CACHE_ROOT}"
            }
        }
    ]
}
```

`CSX_PRIVATE_SHADER_CACHE_ROOT` is optional for a development archive. Point it
at an already validated SE cache root containing `ShaderCache/` and
`ShaderCache-HorizonFix/` to create the final FOMOD archive without compiling
shaders again. The same cache root can be used for both SE arrangements because
their shader payload is identical.

Configure and build the explicit, non-ALL target:

```powershell
cmake --preset private-nr
cmake --build build/private-nr --config Release `
  --target Package-Private-AIO-Vincent-se
```

The archive is written beneath the configured build tree in
`local-artifacts/`, never `dist/`. It omits PDBs, includes a no-redistribution
notice, preserves the exact official 2.12 DLSS-G file set, verifies the linked
DLL against its path-free build manifest, and scans staged inputs for local
profile/source paths and credential-shaped data. Do not publish or redistribute
the result.
