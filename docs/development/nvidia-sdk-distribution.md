# NVIDIA SDK build inputs and SE packaging

## Pinned inputs

The existing Streamline source submodule supplies NGX declarations and its
static dispatcher library. The separate NR parameter header comes from the
official SDK package referenced by the pinned NVIDIA Remix revision in
[NeuralRenderingSDKPin.cmake](../../cmake/NeuralRenderingSDKPin.cmake).
That file records the archive, parameter header and original license hashes.
The reference runtime hash records package identity; it is not an allowlist
for installed user runtimes.

[NeuralRendering-Runtime.cmake](../../cmake/NeuralRendering-Runtime.cmake)
verifies those inputs before compilation. It extracts only `include/*` and
`LICENSE.txt` into the build cache. SDK headers and runtime binaries are not
checked into this repository. The complete original license is retained,
byte-for-byte, at
[extern/NeuralRenderingSDK/LICENSE.txt](../../extern/NeuralRenderingSDK/LICENSE.txt).

The SE mono runtime uses NGX D3D12 project initialization, capabilities,
scratch allocation, feature creation/evaluation and teardown. It shares
CSX's NVIDIA project identity with Streamline. Runtime availability remains
subject to NGX capability checks and the existing version policy: compatible
310.8+ inputs are accepted, with normal rendering retained when unavailable.

## Runtime and notices

NR object code remains user-supplied. Core/AIO staging, installation and
packaging reject `nvngx_dlssnr*.dll` and `sl.dlss_nr*.dll`. The verified NR SDK
cache is a compilation input, never a runtime packaging directory.
Users install their compatible runtime separately under
`Data/Shaders/Upscaling/Streamline/`, under the applicable NVIDIA terms.

The original `nvngx_dlssnr.license.txt` is staged beside the native Streamline
files. Both native Streamline and SE's separate StreamlineDX12 frame-generation
directory retain the seven original notices from the Streamline archive.
The original SDK terms govern those components; consult the supplied license
for its requirements. A successful build or package check establishes file
identity and technical packaging boundaries only.

## Offline builds and tests

Configure once with `SKIP_RUNTIME_DOWNLOADS=OFF` to populate the verified NR
SDK cache. Offline DLL development can reuse that archive and header cache;
missing or modified inputs fail configuration and remain untouched. An absent
user NR runtime does not block compilation or packaging.

The portable contract tests use the original NR parameter definitions through
`CSX_NR_SDK_INCLUDE`, with the pinned header hash checked during configuration.
The package tests use local synthetic archives, run the actual CMake modules
and verify that extraction/staging cannot include NR binaries. They also
preserve SE's independent frame-generation runtime inventory.
