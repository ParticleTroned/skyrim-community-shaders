# Graphics context ownership

Community Shaders and Skyrim share one D3D11 immediate context. Concurrent
native command writers were observed during VR loading transitions, across
viewport and pixel-shader resource operations. A completed viewport packet
was replaced before the first observed publication in the retained capture.
The exact overwriting instruction sequence remains unproven. The evidence
supports enforcing the shared ownership contract; it does not establish
that this observed sequence caused the earlier malformed-command hang.

## Device and context protection

Both device-creation hooks remove `D3D11_CREATE_DEVICE_SINGLETHREADED` while
preserving other creation flags. Each successful context receives verified
`ID3D11Multithread` protection before being returned to its caller. This also
covers the upscaling hook's RenderDoc bypass and the flat frame-generation
proxy candidate, before backend initialization uses that candidate.

The policy applies to SE, AE and VR without driver-specific offsets or
diagnostic recording. The renderer initialization hook checks the actual
published context again before installing context hooks or initializing UI.
Unsupported contexts fail creation with released, null output objects; an
incompatible replacement context at renderer initialization reports a fatal
initialization error instead of continuing with unsafe concurrent access.
Deferred contexts and devices created as single-threaded are rejected.

Protection belongs to the device lifetime. Screenshot readback and flowmap
generation use the same helper and never restore an independently saved
protection flag. Screenshot completion no longer changes the game's context
protection; captured pixels, image formats and capture scheduling are
unchanged. Flowmap generation records copies on its private deferred context
and submits them with `ExecuteCommandList(..., TRUE)`, preserving immediate
context state. Its subsequent staging readback operates on private textures.
Per-call protection permits those operations without holding the context
lock across texture loading, image encoding or filesystem I/O. The helper
retains no global COM references.

Microsoft documents that the immediate context requires synchronization and
that `ID3D11Multithread` adds overhead to each protected API call. Its setter
returns the previous state; the implementation checks
`GetMultithreadProtected` to verify the resulting state.
See [D3D11 threading protection](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nn-d3d11_4-id3d11multithread)
and [device creation flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag).

## Loading-menu transaction

Disassembly of Skyrim VR 1.4.15 identifies a loading-menu show handler that
can call the native UI-target clear routine outside the render thread. That
routine changes the
engine's cached render target and dirty state while clearing pending FADERUI
and WORLDUI targets. Per-call D3D protection alone does not serialize this
multi-call transaction or the shared CPU-side renderer state.

The loading-menu call uses the same native renderer critical section as
ordinary rendering. It attempts entry without waiting. If rendering owns
the lock, the callback leaves all pending clear flags and graphics state
untouched. The normal render path calls the original clear routine before UI
presentation while holding the renderer lock, and performs the pending work.
Reentrant ownership on the rendering thread is permitted. This avoids a
blocking UI-to-render lock dependency and adds no new lock operation to each
ordinary draw call. SE and AE do not install this VR call-site hook.

The call site and target must match the validated runtime bytes before the
hook is installed. An unsupported site is reported explicitly; permanent
context protection still applies, but the loading-menu transaction guard
must not be reported as active.

## Verification through DevBench

`communityshaders.renderscale` accepts the read-only
`graphics_context_status` action. It runs on the main thread and returns
`graphicsContext`; ordinary `status` includes the same object. Inspection
does not enable, disable or repair protection.

The object reports device flags, immediate-context and interface
availability, `multithreadProtected`, the inspection HRESULT, `apiReady`
and `ready`. `apiReady` requires a compatible device and active API
protection. Aggregate `ready` additionally requires the applicable
loading-menu guard to be ready. These are implementation readiness checks;
they do not certify runtime stability, complete render-pass isolation or
that an external module cannot subsequently change protection. The policy
has no runtime off switch.

`loadingMenuClear` reports the native guard's installation state,
`applicable`, `ready`, and attempted, executed, deferred and missing-renderer
counts. An applicable VR guard must be installed to be ready. Counters are
independently sampled; an in-flight clear can temporarily leave totals
unbalanced.

## Validation and limits

The native WARP test exercises the actual D3D11 interfaces, including
initially unprotected contexts, independent device state, automatic locking
of an API call against `Enter`, repeated users, creation outputs, rejected
single-threaded/deferred contexts and failure cleanup. A concurrent command
list test checks that state-restoring execution preserves another thread's
viewport without an enclosing context transaction. The loading-menu
admission test exercises validated native-call signatures, runtime rejection,
installation readiness, critical-section contention, reentrant ownership,
pending work and release on callback failure. Existing screenshot dispatch
and presentation tests cover the affected adjacent contracts.

Earlier checks in the investigation workspace are local investigation
evidence, not build verification of this extracted production candidate.
Candidate build and test results must be recorded separately before
publication. Local evidence is retained under
`artifacts/graphics-context-protection-20260912`.

In-game stability and performance remain pending qualification. The PR stays
a draft until those checks are verified. **Performance neutrality is
unverified.** Permanent API protection can add CPU cost even though the
loading-menu guard itself runs only on the menu event. Compare the previous
and candidate DLLs in the same scene, profile, runtime route, resolution and
shader state, with warm caches and repeated fresh frame samples. Report
whole-frame and CPU/GPU measurements separately; a CSX profiler total alone
does not measure all engine calls. Keep diagnostic writer tracing disabled
in both performance lanes. Run the 20-transition COC stability assay
separately and preserve any fault, pacing interruption or incomplete run.

Driver instrumentation used for the investigation is maintained separately
from this production change. Further tracing is needed only if runtime
validation exposes an unresolved result that requires it to choose a safe
correction.
