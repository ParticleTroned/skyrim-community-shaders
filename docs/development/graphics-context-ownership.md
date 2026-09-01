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
preserving other creation flags. Each successful immediate context is validated
without enabling or disabling `ID3D11Multithread` API protection. This also
covers the upscaling hook's RenderDoc bypass and the flat frame-generation
proxy candidate, before backend initialization uses that candidate.

The policy applies to SE, AE and VR without driver-specific offsets or
diagnostic recording. The renderer initialization hook checks the actual
published context again before installing context hooks or initializing UI.
Unsupported contexts fail creation with released, null output objects; an
incompatible replacement context at renderer initialization reports a fatal
initialization error instead of continuing with unsafe concurrent access.
Deferred contexts and devices created as single-threaded are rejected.

Auxiliary context operations acquire Skyrim's existing renderer critical
section, the same owner used by native rendering and the loading-menu guard.
They do not toggle or restore the API protection flag. Another module's
enabled flag is left intact and remains visible in telemetry.

Screenshot staging takes nonblocking, reentrant renderer ownership for its
resolve/copy transaction. The worker tries that owner for each nonblocking
staging Map, copies the mapped bytes and unmaps before releasing it. Renderer
contention and a pending GPU copy use the existing 500 ms retry deadline;
ownership is released before sleeping, encoding or writing files. Failure
remains an explicit capture failure. An unknown/replaced context is rejected
rather than assigning it the current renderer's lock.

Flowmap generation records copies on its private deferred context. It acquires
renderer ownership around `ExecuteCommandList(..., TRUE)` and separately around
staging capture, preserving immediate-context state. Texture loading, encoding
and filesystem I/O run outside renderer ownership. No new ownership operation
is added to ordinary draw calls. The helpers retain no global COM references.

Microsoft documents that the immediate context requires synchronization and
that `ID3D11Multithread` adds overhead to each protected API call. Inspection
uses `GetMultithreadProtected`; no production caller changes that flag.
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
hook is installed. An unsupported site is reported explicitly, and the
loading-menu transaction guard must not be reported as active or ready.

## Verification through DevBench

`communityshaders.renderscale` accepts the read-only
`graphics_context_status` action. It runs on the main thread and returns
`graphicsContext`; ordinary `status` includes the same object. Inspection
does not enable, disable or repair protection.

The object reports device flags, immediate-context and interface
availability, `multithreadProtected`, the inspection HRESULT, `apiReady`
and `ready`. The policy is `renderer_ownership`; `apiReady` requires a compatible
multi-thread-capable immediate context, independently of its observed API
protection flag. `rendererOwnershipAvailable` requires that context to match
the current renderer and its native critical section. Aggregate `ready`
additionally requires this owner and the applicable loading-menu guard to be
ready. These are implementation readiness checks;
they do not certify runtime stability, complete render-pass isolation or
that an external module cannot subsequently change protection. The policy
has no runtime switch that disables the renderer ownership guards.

`loadingMenuClear` reports the native guard's installation state,
`applicable`, `ready`, and attempted, executed, deferred and missing-renderer
counts. An applicable VR guard must be installed to be ready. Counters are
independently sampled; an in-flight clear can temporarily leave totals
unbalanced.

## Validation and limits

The native WARP test exercises the actual D3D11 interfaces, including
initially unprotected contexts, unchanged external protection, independent
device state, creation outputs, rejected single-threaded/deferred contexts
and failure cleanup. It verifies that validation does not introduce automatic
API locking. Concurrent renderer transactions and state-restoring command
execution preserve another owner's viewport with API locking disabled.
Staging readback tests cover contention, recursion, pixel contents and
unmap/ownership release after a copy exception. The loading-menu
admission test exercises validated native-call signatures, runtime rejection,
installation readiness, critical-section contention, reentrant ownership,
pending work and release on callback failure. Existing screenshot dispatch
and presentation tests cover the affected adjacent contracts.

The originating-machine handover records PR93's merged production source
`1e99de0cebc49c256ffb759b31c616b5154733bc` completing 70 alternating COCs
between WindhelmExterior01 and WhiterunDragonsreach in one VR process:
20 at 10-second waits, 25 at 5-second waits and 25 at 3-second waits. It reports
verified destinations, player-loaded state and DLL identity. Those custom
checks did not prove the earlier hang's cause, matched performance, stereo
fidelity, strict cleanup or SE/AE runtime coverage. Their raw artifacts remain
on that machine; this handover is not validation of the current candidate.

The user's equally instrumented five-build investigation identified additional
D3D11 critical-section execution at PR93. This motivates replacing permanent
API locking while retaining transaction guards. Earlier DLSS regressions and
scene-dependent CPU/GPU changes remain separate investigations. This source
correction publishes no new runtime measurement or performance claim.

The revised ownership candidate still requires exact-build COC and performance
validation. Repeat the 20-transition assay with the retained loading-menu
guard, including screenshot readback; verify no surviving stretch, incomplete
stereo or cleanup debt. Exercise flowmap generation separately. Compare the
same scene, profile, runtime route, resolution and shader state with warm
caches; match tracing between performance lanes. Preserve every fault, pacing
interruption and incomplete run. A successful COC run does not establish that
all possible concurrent native or third-party writers obey the renderer lock.

Driver instrumentation used for the investigation is maintained separately
from this production change. Further tracing is needed only if runtime
validation exposes an unresolved result that requires it to choose a safe
correction.

The [September 15 adversarial review](graphics-context-ownership-review-20260915.md)
records the source-to-test audit, validation commands and remaining runtime
evidence requirements for this correction.
