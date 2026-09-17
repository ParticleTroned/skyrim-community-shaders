# DLSS and Neural Rendering runtimes

Normal DLSS uses the existing Streamline D3D11 integration and the verified
Streamline 2.14.1 runtime selected by `cmake/Streamline-Runtime.cmake`.
Enabling Neural Rendering does not replace that runtime set.

Neural Rendering loads an optional `nvngx_dlssnr.dll` through NGX Feature 18.
It does not require a Streamline NR plugin. Set the CMake file path
`CSX_LOCAL_DLSSNR_RUNTIME_FILE` to a locally supplied provider to include it
in the isolated build. The build fingerprints the file and verifies it
alongside the normal DLSS modules. Clearing the option removes the managed
NR copy from staging. No NVIDIA provider binaries are stored in this source
directory.

The loader checks the provider's 310.8.x version, required exports and
stable file identity. When the provider needs a parameter allocator from
an already loaded driver core, that core must resolve inside DriverStore,
have the expected NVIDIA product metadata and pass offline signature
verification. Ambiguous or unavailable dependencies fail with diagnostics
and retain the ordinary rendering path.

The local provider's applicable terms govern its use and redistribution;
the project's source license does not grant rights to that DLL. Keep
packages containing the experimental provider local.

See `docs/development/main-vr-nr-implementation.md` for feature routing,
color management, compatibility decisions and validation evidence.
