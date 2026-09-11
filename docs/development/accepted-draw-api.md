# Accepted scene draw API v1

Target: ParticleTroned's `main-VR`. This is a native CSX provider, not an
OCU signature adapter. It does not add player ownership rules to CSX.

## Discovery and lifecycle

The Windows x64 contract is `include/VRAPI/CSacceptedDrawapi.h`.
Consumers can query `CSX_GetAcceptedDrawAPI(1, sizeof(API))` or discover
`csx.render.accepted_draw` major 1 through the existing service registry.
An unsupported version, oversized minimum table, or non-VR runtime returns
null from the export. The registry advertises the service only on VR.

The table is process-lifetime. Registration returns `NotReady` until both
geometry attribution and the immediate-context indexed draw hooks are
installed. Installation runs during renderer initialization, not when an
observer registers or from a scene draw. Eight observer slots are available.
No subscriber means no per-draw resource inspection or callback iteration.

Call unregister outside every accepted-draw callback. Successful return
means all callbacks for that subscription have completed, and the consumer
can release its callback state. Registration and unregistration are rejected
inside callbacks to prevent lifecycle deadlocks. Never unload CSX while
holding its API table. Consumer exceptions deactivate that observer and
increment the diagnostic fault count; they cannot unwind across this ABI.

## Draw coverage and state

Coverage is indexed and indexed-instanced **lighting/effect geometry** in
Skyrim VR 1.4.15, not arbitrary shader families or compute dispatches.
CSX's shader setup/restore pairs provide the live native `BSGeometry`.
Nested scopes preserve the parent identity; incomplete setup, restoration,
overflow, and mismatched pairing cannot reuse an unrelated geometry.

The two immediate-context draw entry points reuse CSX's D3D hook-bank
bookkeeping. Enabling the API does not enable developer-mode menu tracing
or install that tracer's other D3D hooks. Later tracing recognizes the
already-installed indexed hooks and does not install duplicate observers.

Events occur after the native indexed call, before the wrapper changes
pipeline state. The context must be the registered immediate context and
the bound DSV must refer to the renderer's current `kMAIN` depth texture.
The depth texture must be single-sample, single-array-slice 2D packed stereo.
Redirected menu capture is explicitly suppressed; skipped draws never reach
native submission. Shadow/reflection depth targets do not qualify.
Draw IDs increase on the one bound rendering thread. Off-thread submissions
are filtered and counted, not delivered with an incompatible lifetime.

Each event borrows its geometry, depth, context, and arguments for the
synchronous callback only. Consumers must not retain them. CSX does not
decide whether geometry belongs to a player, an equipped weapon, a HIGGS
object, or SpellWheel; that remains the consumer's classification.

## Isolated replay

`draw.replay(draw.replayToken)` is valid only inside that observer callback,
on that same thread, once. Stale tokens, duplicate calls and cross-thread
calls are rejected. Replay invokes the original D3D draw trampoline with
the same indexed arguments, rather than reentering CSX's scene/menu routing.
Nested callback delivery is prohibited.

The consumer may temporarily bind its mask output state before replay.
Replay must use that state, not forcibly restore the original scene outputs.
The consumer must restore every changed D3D state before returning, including
on failure. Do not issue unrelated drawing from the callback. This is a
cooperative native ABI, not a sandbox for faulty third-party GPU code.

No new GPU resources, GPU readbacks, blocking GPU waits, frame pacing, or
render-scale changes are introduced by the provider. Per-draw CPU overhead
still needs measurement with a real consumer and representative scenes.

## Diagnostics

With `DEVBENCH_BRIDGE=ON`, call `communityshaders.profiler` with
`{"action":"accepted_draws"}`. This does not enable profiler capture or
per-draw logging. It reports readiness, subscribers, events, callbacks,
successful/rejected replays, observer faults, filtered draws, wrong-thread
draws, and geometry-scope errors. `expectedBuildId` can pin the producing
DLL using the existing DevBench provenance mechanism.

## Validation and game qualification

`accepted_draw_registry_test` covers replay arguments, stale/duplicate token
rejection, cross-thread rejection, nested dispatch, callback lifecycle,
quiescent unregister, bounded registration, exception isolation, nested
geometry scopes, mismatched restores, and scope overflow.

The local OCU integration harness compiles the unmodified OCU API client
against this provider's registry in separate translation units. Its inputs
are synthetic draw events, not captured Skyrim frames. Passing it verifies
ABI communication and callback/replay lifecycle, not real scene coverage.

Before deployment or an upstream claim of compatibility:

1. Confirm the installed DLL's hash/build ID and API registration in OCU's
   startup log. OCU must select API mode before installing private mask hooks.
2. Capture normal indexed and instanced lighting/effect draws in RenderDoc;
   verify that mask replay does not change the scene color/depth outputs.
3. Test equipped weapons, VRIK body, decorated/alpha-tested meshes, HIGGS-held
   objects, SpellWheel, and menus with strafing, stick turning, and hand motion.
4. Verify no menu-capture, shadow, or reflection draw enters the main-scene
   mask; compare DevBench scope/fault counters before and after the run.
5. Compare frame timing in the same scene with the API inactive and active,
   with developer tracing off, then repeat with tracing on for duplicate checks.

These live-game and GPU-capture checks are not replaced by a successful
DLL build or a registry unit test. Existing CSX releases without this API
are not made compatible simply by building this branch.
