# Building CSX with RenderDoc

CSX's VR capture integration requires a compatible build of the
[RenderDoc fork](https://github.com/ParticleTroned/renderdoc). Build its
`Release|x64` DLL following the fork's compilation instructions. Preserve
the matching replay tools: captures from a development build should be
opened with that same RenderDoc build.

The DLL must export `RENDERDOC_GetD3D11StateCompatibilityVersion` and return
at least 2. This reports corrected predicate-wrapper ownership, bounded
dynamic shader-instance getters, and native context-state lifetime handling.
It is a fork capability, separate from
the upstream capture API version. CSX also requires capture API 1.4 for
discarding incomplete stereo captures.

Build CSX normally with the required upscaling runtime payloads. Place the
compatible `renderdoc.dll` in `Data/Renderdoc/`, enable RenderDoc on its CSX
settings page, save, and restart. External injection is also recognized at
startup; CSX configures NvAPI passthrough before vendor SDK initialization.
When CSX capture is disabled, externally configured capture paths, overlay,
and hotkeys are preserved.

## Integration contract

The compatibility path validates Skyrim VR 1.4.15's D3D11 creation import
thunk before installing a private call slot. This preserves CSX's backend
initialization and RenderDoc's device wrapping when later vendor-library
loads refresh imports. Unsupported executables, missing capabilities, or
failed validation retain the upscaling blocker. SE/AE retain that blocker;
Frame Generation and performance-cost measurement remain unavailable under
RenderDoc.

Under the compatible DLL, shared isolated-pass guards use manual D3D11.1
binding snapshots. They restore engine bookkeeping after graphics state,
retain prepared isolated state between passes, and preserve constant-buffer
windows and UAV counters. NR already uses its own compute-state guard.

Use the CSX capture button or CSX capture hotkeys for VR stereo captures.
These requests begin after successful pose acquisition and finish after
the matching native submit call has returned both eyes, including DLSS,
Render Scale, and NR work scheduled inside those submissions. An incomplete
pair is discarded at the next pose boundary. Existing externally owned
captures are left alone; external capture controls keep their own frame
boundaries. Disabling CSX capture cancels queued requests; an already active
stereo pair finishes normally. Idle frames do not query the capture API.

## Validation

Build the controller tests and run `DeviceContextState`,
`DeviceContextManualState`, `DeviceContextHook*`, `ReverseZState`, and
`RenderDocStereoCapture`. The state test can additionally load a specified
DLL and produce a standalone capture:

```powershell
./device_context_state_test.exe --manual <absolute-renderdoc-dll> <capture-prefix>
./device_context_state_test.exe --native <absolute-renderdoc-dll> <capture-prefix>
```

These tests cover state ownership and capture scheduling, not vendor
evaluation or headset output. Runtime verification must check successful
per-eye DLSS evaluation with Render Scale active, any requested NR mode,
and a complete replayable stereo capture. Loading vendor SDKs alone does
not establish that the submit path works.

With a DevBench build, `communityshaders.upscaling_api` action `renderdoc`
reports startup compatibility and pending, active, completed, and failed
stereo capture counts. Use the existing upscaling and NR diagnostics to
verify actual evaluation. Shader and render-scale changes still follow
their normal validation requirements.
