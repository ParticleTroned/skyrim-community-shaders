# Internal runtime placement

This source directory contains configuration and license notices only. Any DLL
placed here is ignored by Git and excluded from packaging. Runtime staging is
disabled by default, so a clean checkout builds and packages without NVIDIA
runtime binaries while preserving this directory in the output.

Normal DLSS continues to use the existing Streamline D3D11 path compiled from
the Streamline 2.12 headers in `extern/Streamline-DX12`. This experiment stages
a mutually matched Streamline 2.13 core/plugin set plus NVIDIA-signed 310.8
`nvngx_dlss.dll`; unchanged normal DLSS must therefore be proven before testing
Neural Rendering.

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
NVIDIA terms before any use beyond private local testing.

For the current internal runtime, configure with
`CSX_STAGE_LOCAL_DLSS_RUNTIME=ON`, point
`CSX_LOCAL_DLSS_RUNTIME_DIRECTORY` at the directory containing the six matched
normal-DLSS/Streamline DLLs, and point `CSX_LOCAL_DLSSNR_RUNTIME_FILE` at the
separate `nvngx_dlssnr.dll`. Both paths are CMake cache values and may identify
any developer-controlled location. The seven files are hash-checked before
they enter build or package outputs.

An explicit future-facing official SDK path is also retained. Setting
`CSX_FETCH_OFFICIAL_STREAMLINE_RUNTIME=ON` downloads NVIDIA's hash-pinned
official SDK release and stages its six normal-DLSS runtime files. It does not
provide `nvngx_dlssnr.dll`, so Neural Rendering remains unavailable until an
official NR runtime can be supplied under an applicable SDK contract. Official
fetch and local staging are mutually exclusive, and neither happens by
default. Updating the official SDK requires an intentional version and archive
SHA-256 change, six reviewed payload SHA-256 changes, and a matching
Streamline submodule/header update.

To supply binaries after compiling without runtime staging, copy them into the
final `Shaders/Upscaling/Streamline` output beside this file. Do not copy them
into the source directory before packaging: source-tree DLLs are deliberately
scrubbed so they cannot enter Git or an archive accidentally.
