# CSX RenderDoc runtime

This package contains the Windows x64 Release build of
[ParticleTroned/renderdoc](https://github.com/ParticleTroned/renderdoc),
version **1.47+pt.0.0.0.dev.49a6a5e57c93**, from commit
`49a6a5e57c9324922be3e86e843d0d66f6c73281`.

The fork has not tagged a release. This version identifies the exact source
used; it does not claim an official RenderDoc release.

The DLL exposes D3D11 state compatibility version 2 and external-write
version 1. In CSX, supported Skyrim VR startup can retain DLSS with RenderDoc
loaded after vendor passthrough and device-creation checks succeed. SE/AE
and failed compatibility checks still block upscaling. Frame Generation
remains unavailable with RenderDoc.

DLSS/NR captures contain opaque vendor-output boundaries. Replay restores
the captured output; it does not rerun inference or measure vendor kernels.
Use replay tools built from this same fork revision. Upstream RenderDoc
cannot replay the fork capture format. Enable CSX capture, save settings,
and restart to load this DLL; an externally injected DLL takes precedence.

`LICENSE.md` preserves upstream MIT notices. `COPYING` covers fork additions
under GPL-3.0-or-later. `Source/` contains the corresponding source archive,
original generated build properties, and rebuild instructions.
