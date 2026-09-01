# Internal runtime placement

This source directory contains configuration and license notices only. Any DLL
placed here is ignored by Git and excluded from packaging. The internal test
build instead copies an exact hash-pinned runtime set from the user-supplied
runtime directory into its build payload and AIO output.

Normal DLSS continues to use the existing Streamline D3D11 path compiled from
the Streamline 2.12 headers in `extern/Streamline-DX12`. This experiment stages
a mutually matched Streamline 2.13 core/plugin set plus NVIDIA-signed 310.8
`nvngx_dlss.dll`; unchanged normal DLSS must therefore be proven before testing
Neural Rendering. No runtime is downloaded.

The Neural Rendering experiment loads `nvngx_dlssnr.dll` directly through the
NGX Feature 18 exports. It does not use or require `sl.dlss_nr.dll` or a
Streamline Neural Rendering plugin. CMake automatically selects the patched
310.8 `8270...206` identity and rejects it if its SHA-256 differs from the
pinned value. Its embedded signature reports `HashMismatch`; its exact pinned
hash is accepted independently of the Community Shaders log level.

If that runtime does not export the NGX parameter allocator, the experiment
selects one already loaded driver core before NGX initialization. Its locked
file must resolve below the Windows DriverStore, have the basename `nvngx.dll`
or NVIDIA's DriverStore alias `_nvngx.dll`, pass offline Authenticode
verification, and identify itself in its version resource as
`NVIDIA Corporation` / `NGX` / `nvngx.dll`. The current DriverStore core can
carry a Microsoft WHCP signature, so its NVIDIA product metadata is checked
separately from signature validity; version metadata does not prove the
signer's publisher. Sibling `nvngx.dll` and `_nvngx.dll` files beside the plugin
are rejected before the first initialization call. Ambiguity or any failed
check is reported by the DevBench `nr_status` action and in the standard log.

Developer-supplied NVIDIA binaries are not covered by this project's GPL
license, and this repository grants no right to redistribute them. These
branches and their caller-path substitution are internal experiments only.
Do not commit or publish the DLLs or the generated AIO; review the applicable
NVIDIA terms before any use beyond private local testing. Set
`CSX_STAGE_LOCAL_DLSS_RUNTIME=OFF` to build without them.
