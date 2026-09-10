# Runtime FSR shared guide inputs

The VR runtime FSR path can consume the exact D3D11 textures written by
CSX's full-eye guide encode pass through retained D3D12 imports. Depth,
motion vectors, reactive mask and transparency/composition mask each avoid
one staging copy per eye when eligible. The encode shader, guide values,
active render rectangle, temporal parameters and output publication are
unchanged. Both ordinary full-eye and submit-stage full-eye dispatch use
this path. Foveated crops continue through their existing copied guides.

Color input and output still use the existing copies. The fallback staging
textures remain allocated, so this change claims neither reduced VRAM nor
a measured frame-time improvement. A fully direct stereo dispatch issues
two input copies (color), instead of ten, plus the same two output copies.
A partially importable pair uses the direct path per eligible guide and
copies the remainder without changing provider or history ownership.

## Eligibility and ownership

Only the current CSX full-eye guide objects are eligible. Their exact COM
identity, full-display allocation bounds, single mip/slice/sample, default
GPU usage, absence of CPU access and NT-sharing flags must match. The
source device must be the same D3D11 device as the interop owner. Keyed
mutex resources are excluded because this path uses shared fences.

Guide creation requests NT sharing when the runtime FSR route is selected.
A supported ordinary texture is retried if the shared allocation fails on
a healthy D3D11 device. Existing non-shared allocations remain usable and
are not recreated solely to enable this optimization. SE/AE, host FSR and
DLSS retain their existing dispatch paths.

Each guide texture owns its single NT handle. This is required because
`IDXGIResource1::CreateSharedHandle` may succeed only once per resource;
context resets reopen the retained handle rather than creating it again.
The imported wrapper owns both the D3D11 source and its D3D12 view.

There is one bounded import attempt per eye/guide for each runtime shared
resource generation. A rejected source is retained to prevent address
reuse and repeated failing imports. A different source uses copied guides
until the existing fenced runtime teardown clears that slot. Neither an
A/B switch nor a cache miss replaces an import that could still be in
flight. An import is a second API view of an existing allocation; it is
not counted as a new physical texture allocation.

The original D3D11 signal/D3D12 wait and D3D12 signal/D3D11 wait remain in
place. Both APIs return shared guide resources through the same `COMMON`
state boundaries as staged inputs. Source writes remain ordered after
previous vendor reads. A provider fault quarantines its imported source
identities as well as the runtime ownership. Intermediate readiness and
all guide encode paths reject them; host fallback requires replacement
guides, while retained COM owners preserve the quarantined generation.
Mixed-provider stereo rejection and presentation fallback remain active.

## DevBench A/B inspection

The existing render-scale DevBench tool adds `fsr_shared_guides`:

```json
{"action":"fsr_shared_guides"}
{"action":"fsr_shared_guides","enabled":false}
{"action":"fsr_shared_guides","enabled":true}
```

The optional boolean is a session-only diagnostic switch, defaulting to
true. Stop GPU telemetry before changing it, then start a new
`gpu_performance_start` capture and exercise an unchanged full-eye FSR
profile. Disabling direct imports retains their ownership and forces the
reference input copies. Enabling imports does not force resource creation.
Normal public upscaling actions select FSR3/FSR4 and the same quality;
physical provider diagnostics must confirm that runtime FSR is executing.

`gpu_performance_status` and stop receipts report:

-   `item5ActiveFSRCopies.copyCalls` and `activePixels`: actual enqueued
    input copy calls/pixels, excluding color extraction and output copies.
-   `item5ActiveFSRCopies.avoidedPixels`: savings against copying all five
    full-allocation inputs, including inactive rectangles and direct guides.
-   `runtimeFSRSharedGuides.directGuideInputs` and `directGuidePixels`:
    guide copies bypassed by successful direct imports.
-   `runtimeFSRSharedGuides.fallbackGuideCopies`: guides copied due to the
    disabled mode, noneligible source, rejected import or retained old slot.
-   `runtimeFSRSharedGuides.importFailures`: failed import attempts while
    telemetry was active. Cached failures do not retry until fenced reset.

These counters describe submitted input work, even when a subsequent
vendor call fails. Successful dispatch and stereo health remain separate
checks. Compare identical scenes, formats, output dimensions and provider
versions; avoid claiming a GPU-time gain from copy counts alone.

## Validation

`FSRSharedGuidesPolicy` checks bounded import routing and exact copy/pixel
accounting, including partial imports and invalid dimensions.
`FSREyeDispatch` covers rejection of quarantined guides before host
fallback and acceptance after replacement.
`FSRSharedGuideInterop` compiles the production NT-handle accessor and
import constructor, creates the four guide formats on matching hardware
D3D11/D3D12 devices, writes known D3D11 UAV values, fences to D3D12
readback, fences back, destroys the import and reopens the same NT handle.
It checks every pixel for both imports. Unsupported hardware device
creation returns CTest skip code 77; a sharing or data mismatch fails.

These tests do not substitute for the existing VR render-scale release
qualification. Full-eye, submit-stage, foveated fallback, quality changes,
FSR3/FSR4/host fallback, device loss and mixed-stereo failure paths require
runtime evidence before this change can be qualified.

## Acknowledgments

The direct sharing approach was informed by the OptiScaler project's
`dx11_with_dx12.cpp` implementation at commit
[`d36a078`](https://github.com/optiscaler/OptiScaler/blob/d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec/OptiScaler/with_dx12/dx11_with_dx12.cpp).
This is a native CSX implementation using the existing resource wrappers,
encode outputs, generation tracking and fencing. No OptiScaler source was
copied into these changes. OptiScaler is licensed under GPL-3.0; CSX's
existing license and bundled third-party notices remain applicable.

The retained handle lifetime follows
[Microsoft's CreateSharedHandle contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle).
