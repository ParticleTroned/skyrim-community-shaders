# RenderDoc fork headers

This overlay port installs the MIT-licensed application and D3D11 extension
headers from `ParticleTroned/renderdoc`. `pin.cmake` selects an immutable
fork commit and verifies its source archive. CSX includes the extension
header directly so its ABI stays aligned with the bundled runtime.

The runtime is built separately and tracked under
`features/RenderDoc/Renderdoc/`. Updating headers alone cannot update it.
CMake checks the DLL hash against the same pin and downloads the matching
source archive for AIO distribution. See
[the RenderDoc build guide](../../../docs/development/renderdoc.md) for the
update, build, and compatibility checks.
